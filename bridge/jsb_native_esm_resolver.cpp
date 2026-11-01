#include "jsb_native_esm_resolver.h"

#if JSB_NATIVE_ESM && JSB_WITH_V8

#include "jsb_bridge_helper.h"
#include "jsb_class_info.h"
#include "jsb_environment.h"

#include "../internal/jsb_path_util.h"
#include "../internal/jsb_source_reader.h"

namespace jsb
{
    namespace
    {
        // 转发模块里存放 CommonJS exports 的变量名，取一个正常代码里不会出现的名字
        constexpr char kBridgeVar[] = "__jsb_esm_cjs";

        // 不能拿来当导出名用的 JS 保留字
        bool is_reserved_word(const String& p_name)
        {
            static constexpr char const* const kReserved[] = {
                "break", "case", "catch", "class", "const", "continue", "debugger", "default",
                "delete", "do", "else", "enum", "export", "extends", "false", "finally", "for",
                "function", "if", "import", "in", "instanceof", "new", "null", "return", "super",
                "switch", "this", "throw", "true", "try", "typeof", "var", "void", "while",
                "with", "yield", "let", "static", "await",
            };

            for (const char* const keyword : kReserved)
            {
                if (p_name == keyword)
                {
                    return true;
                }
            }
            return false;
        }

        // 按 JS 字符串字面量的写法转义
        String to_string_literal(const String& p_value)
        {
            String result = "\"";

            for (int i = 0; i < p_value.length(); ++i)
            {
                const char32_t c = p_value[i];
                switch (c)
                {
                    case '"': result += "\\\""; break;
                    case '\\': result += "\\\\"; break;
                    case '\n': result += "\\n"; break;
                    case '\r': result += "\\r"; break;
                    case '\t': result += "\\t"; break;
                    default:
                        if (c < 0x20)
                        {
                            result += jsb_format("\\u%04x", (uint32_t) c);
                        }
                        else
                        {
                            result += c;
                        }
                }
            }

            return result + "\"";
        }

        v8::Local<v8::Module> compile_module_source(Environment* p_env, const String& p_resource_name, const String& p_source)
        {
            v8::Isolate* isolate = p_env->get_isolate();
            const v8::Local<v8::String> source_string = impl::Helper::new_string(isolate, p_source);
            v8::ScriptOrigin origin(
                impl::Helper::new_string(isolate, p_resource_name),
                /* resource_line_offset */ 0,
                /* resource_column_offset */ 0,
                /* resource_is_shared_cross_origin */ false,
                /* script_id */ -1,
                /* source_map_url */ v8::Local<v8::Value>(),
                /* resource_is_opaque */ false,
                /* is_wasm */ false,
                /* is_module */ true);
            v8::ScriptCompiler::Source script_source(source_string, origin);

            v8::Local<v8::Module> module;
            if (!v8::ScriptCompiler::CompileModule(isolate, &script_source).ToLocal(&module))
            {
                // 编译错误已经挂在 isolate 上，由调用方负责报告
                return v8::Local<v8::Module>();
            }
            return module;
        }

        // `import.meta.resolve(specifier)`：按引用方的位置解析成资源路径，解析不到就抛错
        void esm_meta_resolve(const v8::FunctionCallbackInfo<v8::Value>& p_info)
        {
            v8::Isolate* isolate = p_info.GetIsolate();
            const v8::Local<v8::Context> context = isolate->GetCurrentContext();

            if (p_info.Length() != 1 || !p_info[0]->IsString())
            {
                impl::Helper::throw_error(isolate, "import.meta.resolve() needs one string argument");
                return;
            }

            // 创建函数时把引用方的路径放在 Data 里带进来
            const String parent_id = impl::Helper::to_string(isolate, p_info.Data());
            const String specifier = impl::Helper::to_string(isolate, p_info[0]);

            String module_id = specifier;
            if (specifier.begins_with("./") || specifier.begins_with("../"))
            {
                module_id = internal::PathUtil::combine(internal::PathUtil::dirname(parent_id), specifier);
                if (internal::PathUtil::extract(module_id, module_id) != OK || module_id.is_empty())
                {
                    impl::Helper::throw_error(isolate, jsb_format("cannot resolve '%s'", specifier));
                    return;
                }
            }

            ModuleSourceInfo source_info;
            if (Environment* env = Environment::wrap(context); env->find_module_resolver(module_id, source_info))
            {
                p_info.GetReturnValue().Set(impl::Helper::new_string(isolate, source_info.source_filepath));
                return;
            }

            impl::Helper::throw_error(isolate, jsb_format("cannot resolve '%s' from '%s'", specifier, parent_id));
        }

        // 离开作用域时把本次依赖图编译出来的模块从待处理列表里摘掉，出错提前返回时也一样
        struct GraphModulesScope
        {
            Vector<JavaScriptModule*>& list;
            const int begin;

            GraphModulesScope(Vector<JavaScriptModule*>& p_list, int p_begin)
                : list(p_list), begin(p_begin) {}

            ~GraphModulesScope() { list.resize(begin); }
        };

        // 把还在等顶层 await 的模块列出来，报错时能指出卡在哪个文件
        String describe_stalled_awaits(Environment* p_env, v8::Local<v8::Module> p_root)
        {
            const auto stalled = p_root->GetStalledTopLevelAwaitMessages(p_env->get_isolate());
            String result;

            for (const v8::Local<v8::Module>& module : stalled.first)
            {
                if (module->GetStatus() == v8::Module::kErrored)
                {
                    continue;
                }

                const StringName module_id = p_env->find_esm_module_id_by_script_id(module->ScriptId());
                result += jsb_format("\n\twaiting for a top-level await in %s", module_id);
            }
            return result;
        }
    }

    bool NativeESMModuleResolver::get_source_info(const String& p_module_id, ModuleSourceInfo& r_source_info)
    {
        // 搜索路径沿用 DefaultModuleResolver 的，只是把结果限制在 `.mjs`
        if (!DefaultModuleResolver::get_source_info(p_module_id, r_source_info))
        {
            return false;
        }

        if (!r_source_info.source_filepath.ends_with("." JSB_MODULE_EXT))
        {
            r_source_info = {};
            return false;
        }
        return true;
    }

    bool NativeESMModuleResolver::load(Environment* p_env, const String& p_asset_path, JavaScriptModule& p_module, bool p_defer_esm_evaluation)
    {
        internal::FileAccessSourceReader reader(p_asset_path);
        if (reader.is_null() || reader.get_length() == 0)
        {
            impl::Helper::throw_error(p_env->get_isolate(), jsb_format("failed to read the source of %s", p_asset_path));
            p_module.esm_load_failed = true;
            return false;
        }

#if JSB_SUPPORT_RELOAD && defined(TOOLS_ENABLED)
        p_module.time_modified = reader.get_time_modified();
        p_module.hash = reader.get_hash();
#endif

        const uint64_t source_len = reader.get_length();
        Vector<uint8_t> source_bytes;
        source_bytes.resize((size_t) source_len + 1);
        source_bytes.write[(size_t) source_len] = 0; // 留个结尾的 0，方便按 C 字符串处理
        reader.get_buffer(source_bytes.ptrw(), source_len);

        if (!compile_module(p_env, p_asset_path, String::utf8((const char*) source_bytes.ptr(), (int) source_len), p_module))
        {
            p_module.esm_load_failed = true;
            return false;
        }

        if (p_defer_esm_evaluation)
        {
            // V8 正在实例化依赖图，这里只把编译好的模块交出去，
            // 求值由最外层那次调用统一做，循环依赖才不会断
            graph_modules_.push_back(&p_module);
            return true;
        }

        if (!instantiate_and_evaluate(p_env, p_module))
        {
            p_module.esm_load_failed = true;
            return false;
        }
        return true;
    }

    bool NativeESMModuleResolver::compile_module(Environment* p_env, const String& p_asset_path, const String& p_source, JavaScriptModule& p_module)
    {
        v8::Isolate* isolate = p_env->get_isolate();
        const v8::Local<v8::Context> context = isolate->GetCurrentContext();

        const v8::Local<v8::Module> module = compile_module_source(p_env, p_asset_path, p_source);
        if (module.IsEmpty())
        {
            return false;
        }

        // script_id => 模块 id 要在实例化之前记好，resolve 回调要靠它认出引用方是谁
        p_env->register_esm_module_script_id(module->ScriptId(), p_module.id);
        p_module.esm_module.Reset(isolate, module);

        const v8::Local<v8::Object> module_obj = p_module.module.Get(isolate);
        module_obj->Set(context, jsb_name(p_env, filename), impl::Helper::new_string(isolate, p_asset_path)).Check();
        module_obj->Set(context, jsb_name(p_env, path), impl::Helper::new_string(isolate, internal::PathUtil::dirname(p_asset_path))).Check();
        return true;
    }

    bool NativeESMModuleResolver::instantiate_and_evaluate(Environment* p_env, JavaScriptModule& p_root)
    {
        v8::Isolate* isolate = p_env->get_isolate();
        const v8::Local<v8::Context> context = isolate->GetCurrentContext();
        const v8::Local<v8::Module> root_module = p_root.esm_module.Get(isolate);

        // 本次一起求值的模块：根模块 + 实例化过程中编译出来的依赖
        const int graph_begin = graph_modules_.size();
        graph_modules_.push_back(&p_root);
        const GraphModulesScope graph_scope(graph_modules_, graph_begin);

        const v8::Maybe<bool> instantiated = root_module->InstantiateModule(context, &resolve_module_callback);
        if (instantiated.IsNothing() || !instantiated.FromJust())
        {
            // 解析或连接依赖时的异常还挂在 isolate 上，别再盖掉它
            if (instantiated.IsNothing())
            {
                return false;
            }
            impl::Helper::throw_error(isolate, jsb_format("failed to instantiate the esm module: %s", p_root.source_info.source_filepath));
            return false;
        }

        // 实例化之后才能拿命名空间。导出指向命名空间，绑定是活的，求值前后读到的都是最新值
        for (int i = graph_begin; i < graph_modules_.size(); ++i)
        {
            JavaScriptModule* module = graph_modules_[i];
            const v8::Local<v8::Value> module_namespace = module->esm_module.Get(isolate)->GetModuleNamespace();
            module->exports.Reset(isolate, module_namespace);
            module->module.Get(isolate)->Set(context, jsb_name(p_env, exports), module_namespace).Check();
        }

        v8::Local<v8::Value> evaluation_result;
        if (!root_module->Evaluate(context).ToLocal(&evaluation_result))
        {
            // 模块体里抛出的异常还挂在 isolate 上
            return false;
        }

        // 顶层 await 留下的微任务要跑完，否则导出的绑定还没填上
        isolate->PerformMicrotaskCheckpoint();

        if (root_module->GetStatus() == v8::Module::kErrored)
        {
            isolate->ThrowException(root_module->GetException());
            return false;
        }

        if (evaluation_result->IsPromise())
        {
            const v8::Local<v8::Promise> promise = evaluation_result.As<v8::Promise>();
            if (promise->State() == v8::Promise::kRejected)
            {
                isolate->ThrowException(promise->Result());
                return false;
            }
            if (promise->State() == v8::Promise::kPending)
            {
                // 还在等真实的异步操作，同步拿不到导出。V8 不允许对求值中的模块再求值，
                // 所以这个模块之后也不能再用，标记下来，免得下次被当成加载成功
                p_root.esm_evaluation_stalled = true;
                impl::Helper::throw_error(isolate, jsb_format("the esm module '%s' is still waiting for a top-level await%s",
                        p_root.id, describe_stalled_awaits(p_env, root_module)));
                return false;
            }
        }

        // 到这里依赖图求值完了，读各模块的导出才安全：登记脚本类
        for (int i = graph_begin; i < graph_modules_.size(); ++i)
        {
            JavaScriptModule* module = graph_modules_[i];
            const impl::TryCatch try_catch_run(isolate);
            {
                v8::HandleScope parse_scope(isolate);
                ScriptClassInfo::_parse_script_class(context, *module);
            }
            if (try_catch_run.has_caught())
            {
                JSB_LOG(Error, "something wrong when parsing '%s'\n%s", module->id, BridgeHelper::get_exception(try_catch_run));
            }
        }

        return true;
    }

    v8::MaybeLocal<v8::Module> NativeESMModuleResolver::resolve_module_callback(
        v8::Local<v8::Context> p_context,
        v8::Local<v8::String> p_specifier,
        v8::Local<v8::FixedArray> /*p_import_attributes*/,
        v8::Local<v8::Module> p_referrer)
    {
        v8::Isolate* isolate = p_context->GetIsolate();
        Environment* env = Environment::wrap(p_context);
        const String specifier = impl::Helper::to_string(isolate, p_specifier);
        const String parent_id = env->find_esm_module_id_by_script_id(p_referrer->ScriptId());

        // 第三个参数为 true：依赖只编译不执行，等最外层那次调用统一求值
        if (JavaScriptModule* child = env->_load_module(parent_id, specifier, true))
        {
            if (!child->esm_module.IsEmpty())
            {
                return child->esm_module.Get(isolate);
            }

            // 目标是 CommonJS / JSON / `godot` 这类非 ESM 模块，生成一个转发模块
            if (v8::Local<v8::Module> bridge = create_bridge_module(env, p_context, child->id, child->exports.Get(isolate));
                !bridge.IsEmpty())
            {
                return bridge;
            }
        }

        // 失败时异常已经挂在 isolate 上，V8 会把它带出去
        return v8::MaybeLocal<v8::Module>();
    }

    v8::Local<v8::Module> NativeESMModuleResolver::create_bridge_module(
        Environment* p_env,
        const v8::Local<v8::Context>& p_context,
        const String& p_module_id,
        const v8::Local<v8::Value>& p_exports)
    {
        v8::Isolate* isolate = p_env->get_isolate();

        // 生成的模块大致长这样：
        //     const __jsb_esm_cjs = require("<模块 id>");
        //     export default __jsb_esm_cjs;
        //     export const foo = __jsb_esm_cjs["foo"];
        // 属性名不能直接当标识符时用字符串导出名，不要把属性丢掉：
        //     const __jsb_esm_cjs_3 = __jsb_esm_cjs["some-name"];
        //     export { __jsb_esm_cjs_3 as "some-name" };
        // require 走的是模块缓存，不会把目标模块再执行一遍。
        String source = jsb_format("const %s = require(%s);\nexport default %s;\n", kBridgeVar, to_string_literal(p_module_id), kBridgeVar);

        // `godot` 模块是个按需加载的 Proxy，列不出名字，这种只给默认导出
        if (p_exports->IsObject() && !p_exports.As<v8::Object>()->IsProxy())
        {
            v8::Local<v8::Array> keys;
            if (p_exports.As<v8::Object>()->GetOwnPropertyNames(p_context).ToLocal(&keys))
            {
                for (uint32_t i = 0; i < keys->Length(); ++i)
                {
                    v8::Local<v8::Value> key;
                    v8::Local<v8::String> key_string;

                    // 整数键（如 "0"）在 C++ 接口里拿到的是数字而不是字符串，统一转成字符串再处理
                    if (!keys->Get(p_context, i).ToLocal(&key) || !key->ToString(p_context).ToLocal(&key_string))
                    {
                        continue;
                    }

                    const String name = impl::Helper::to_string(isolate, key_string);

                    // `default` 已经被上面的默认导出占用了，再导一次整个转接头都编译不过
                    if (name.is_empty() || name == "default")
                    {
                        continue;
                    }

                    if (name.is_valid_identifier() && !is_reserved_word(name))
                    {
                        source += jsb_format("export const %s = %s[%s];\n", name, kBridgeVar, to_string_literal(name));
                    }
                    else
                    {
                        // 别名只要合法又唯一就行，用属性序号
                        const String alias = jsb_format("%s_%d", kBridgeVar, i);
                        source += jsb_format("const %s = %s[%s];\nexport { %s as %s };\n",
                                alias, kBridgeVar, to_string_literal(name), alias, to_string_literal(name));
                    }
                }
            }
        }

        return compile_module_source(p_env, p_module_id, source);
    }

    v8::MaybeLocal<v8::Promise> NativeESMModuleResolver::host_import_module_dynamically(
        v8::Local<v8::Context> p_context,
        v8::Local<v8::Data> /*p_host_defined_options*/,
        v8::Local<v8::Value> p_resource_name,
        v8::Local<v8::String> p_specifier,
        v8::Local<v8::FixedArray> /*p_import_attributes*/)
    {
        v8::Isolate* isolate = p_context->GetIsolate();
        Environment* env = Environment::wrap(p_context);
        const String specifier = impl::Helper::to_string(isolate, p_specifier);

        // 发起方的 ScriptOrigin 里存的就是它的资源路径，相对路径按它算
        const String parent_id = p_resource_name->IsString() ? impl::Helper::to_string(isolate, p_resource_name) : String();

        v8::Local<v8::Promise::Resolver> resolver;
        if (!v8::Promise::Resolver::New(p_context).ToLocal(&resolver))
        {
            return v8::MaybeLocal<v8::Promise>();
        }

        v8::Local<v8::Value> result;
        bool failed = false;
        {
            // 加载失败要变成 reject，而不是把异常直接抛回调用 `import()` 的代码
            const v8::TryCatch try_catch(isolate);
            if (JavaScriptModule* child = env->_load_module(parent_id, specifier))
            {
                result = child->esm_module.IsEmpty()
                    ? child->exports.Get(isolate) // CommonJS：直接给它的 exports
                    : child->esm_module.Get(isolate)->GetModuleNamespace();
            }
            else
            {
                failed = true;
                const v8::Local<v8::Value> error = impl::Helper::new_string(isolate, jsb_format("failed to import '%s'", specifier));
                result = try_catch.HasCaught() ? try_catch.Exception() : error;
            }
        }

        const v8::Maybe<bool> settled = failed ? resolver->Reject(p_context, result) : resolver->Resolve(p_context, result);
        if (settled.IsNothing())
        {
            return v8::MaybeLocal<v8::Promise>();
        }
        return resolver->GetPromise();
    }

    void NativeESMModuleResolver::initialize_import_meta(
        v8::Local<v8::Context> p_context,
        v8::Local<v8::Module> p_module,
        v8::Local<v8::Object> p_meta)
    {
        v8::Isolate* isolate = p_context->GetIsolate();
        Environment* env = Environment::wrap(p_context);
        const StringName module_id = env->find_esm_module_id_by_script_id(p_module->ScriptId());
        if (module_id == StringName())
        {
            return;
        }

        // url 直接用引擎的资源路径（res:// 或 user://），不拼成 file:// URL
        p_meta->CreateDataProperty(p_context,
                impl::Helper::new_string_ascii(isolate, "url"),
                impl::Helper::new_string(isolate, module_id))
            .FromMaybe(false);

        // resolve(specifier)：把相对路径解析成资源路径，模块里想知道某个依赖在哪就不用自己拼路径
        v8::Local<v8::FunctionTemplate> resolve_template = v8::FunctionTemplate::New(isolate,
                &esm_meta_resolve,
                impl::Helper::new_string(isolate, module_id));

        v8::Local<v8::Function> resolve_function;
        if (resolve_template->GetFunction(p_context).ToLocal(&resolve_function))
        {
            p_meta->CreateDataProperty(p_context,
                    impl::Helper::new_string_ascii(isolate, "resolve"),
                    resolve_function)
                .FromMaybe(false);
        }
    }

    void NativeESMModuleResolver::install_host_callbacks(Environment* p_env)
    {
        v8::Isolate* isolate = p_env->get_isolate();
        isolate->SetHostImportModuleDynamicallyCallback(&host_import_module_dynamically);
        isolate->SetHostInitializeImportMetaObjectCallback(&initialize_import_meta);
    }
}

#endif // JSB_NATIVE_ESM && JSB_WITH_V8
