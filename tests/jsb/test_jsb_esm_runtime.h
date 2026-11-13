#ifndef GODOTJS_TESTS_JSB_ESM_RUNTIME_H
#define GODOTJS_TESTS_JSB_ESM_RUNTIME_H

#include "jsb_test_helpers.h"

#if JSB_NATIVE_ESM && JSB_WITH_V8

namespace jsb::tests
{
    /**
     * 每个用例都重新初始化一次脚本语言，模块缓存互不影响。
     * 用例里的模块名不带 res:// 前缀，走解析器的搜索路径（见 `Environment::init`）。
     */
    struct EsmTestCase
    {
        GodotJSScriptLanguageIniter initer;
        const std::shared_ptr<jsb::Environment> env;
        const jsb::tests::V8ContextScope context_scope;

        EsmTestCase()
            : env(GodotJSScriptLanguage::get_singleton()->get_environment()),
              context_scope(env.get()) {}

        JavaScriptModule* load(const String& p_module_id)
        {
            JavaScriptModule* mod = nullptr;
            CHECK(env->load(p_module_id, &mod) == OK);
            return mod;
        }

        // 读模块命名空间（或 CommonJS 的 exports）上的一个属性
        v8::Local<v8::Value> get_export(const JavaScriptModule* p_module, const char* p_name)
        {
            // 加载失败时别去空指针上取东西，直接判失败退出这个用例
            REQUIRE(p_module != nullptr);
            REQUIRE(!p_module->exports.IsEmpty());
            return get_property(p_module->exports.Get(env->get_isolate()).As<v8::Object>(), p_name);
        }

        v8::Local<v8::Value> get_property(const v8::Local<v8::Value>& p_object, const char* p_name)
        {
            v8::Local<v8::Value> value;
            const v8::Local<v8::String> key = impl::Helper::new_string_ascii(env->get_isolate(), p_name);
            REQUIRE(p_object.As<v8::Object>()->Get(env->get_context(), key).ToLocal(&value));
            return value;
        }

        // 调用导出里的函数，返回结果
        v8::Local<v8::Value> call_export(const JavaScriptModule* p_module, const char* p_name, int p_argument)
        {
            v8::Local<v8::Value> callee = get_export(p_module, p_name);
            REQUIRE(callee->IsFunction());

            v8::Local<v8::Value> argv[] = { v8::Integer::New(env->get_isolate(), p_argument) };
            v8::Local<v8::Value> result;
            REQUIRE(callee.As<v8::Function>()->Call(env->get_context(), v8::Undefined(env->get_isolate()), 1, argv).ToLocal(&result));
            return result;
        }

        String to_string(const v8::Local<v8::Value>& p_value)
        {
            if (p_value.IsEmpty())
            {
                return "<missing>";
            }
            return impl::Helper::to_string(env->get_isolate(), p_value);
        }

        double to_number(const v8::Local<v8::Value>& p_value)
        {
            REQUIRE(!p_value.IsEmpty());
            REQUIRE(p_value->IsNumber());
            return p_value.As<v8::Number>()->Value();
        }

        bool to_bool(const v8::Local<v8::Value>& p_value)
        {
            REQUIRE(!p_value.IsEmpty());
            REQUIRE(p_value->IsBoolean());
            return p_value.As<v8::Boolean>()->Value();
        }
    };

    TEST_CASE("[jsb]esm resolver only claims es modules")
    {
        EsmTestCase test;

        ModuleSourceInfo info;
        CHECK(test.env->find_module_resolver("tests/esm/forms", info));
        CHECK(info.source_filepath.ends_with(".mjs"));

        CHECK(test.env->find_module_resolver("tests/esm/cjs_target", info));
        CHECK(info.source_filepath.ends_with(".js"));

        CHECK(test.env->find_module_resolver("tests/esm/data", info));
        CHECK(info.source_filepath.ends_with(".json"));

        // 没有 package.json 声明的 `.js` 仍按 CommonJS 加载
        JavaScriptModule* cjs = test.load("tests/esm/cjs_target");
        REQUIRE(cjs != nullptr);
        CHECK(!cjs->is_native_esm());
    }

    TEST_CASE("[jsb]esm named, default and namespace imports")
    {
        EsmTestCase test;

        JavaScriptModule* mod = test.load("tests/esm/forms");
        REQUIRE(mod != nullptr);
        CHECK(mod->is_native_esm());
        CHECK(!mod->esm_module.IsEmpty());

        // 默认导出、具名导出、重命名导出
        CHECK(test.to_string(test.get_export(mod, "default")) == "forms");
        CHECK(test.to_string(test.get_export(mod, "renamed")) == "world");

        // `export * from` 转发过来的名字
        CHECK(test.to_string(test.get_export(mod, "FROM_EXTRA")) == "extra");
        CHECK(test.to_number(test.get_export(mod, "num")) == 7);

        // 命名空间导入参与计算的结果
        CHECK(test.to_string(test.get_export(mod, "joined")) == "42-world-world");

        // 函数和类导出
        CHECK(test.to_number(test.call_export(mod, "double", 21)) == 42);
        CHECK(test.get_export(mod, "Widget")->IsFunction());
    }

    TEST_CASE("[jsb]esm importing commonjs, json and the godot module")
    {
        EsmTestCase test;

        JavaScriptModule* mod = test.load("tests/esm/interop");
        REQUIRE(mod != nullptr);
        CHECK(mod->is_native_esm());

        // import 一个 CommonJS：默认导出是它的 module.exports，具名导出按属性转发
        CHECK(test.to_bool(test.get_export(mod, "cjs_default_is_object")));
        CHECK(test.to_number(test.get_export(mod, "cjs_from")) == 7);
        CHECK(test.to_string(test.get_export(mod, "cjs_label")) == "cjs");

        // 属性名不是合法标识符也能转出来；CJS 自己的 .default 属性不会顶掉默认导出
        CHECK(test.to_string(test.get_export(mod, "oddName")) == "odd-name");
        CHECK(test.to_string(test.get_export(mod, "zeroKey")) == "zero-key");
        CHECK(test.to_string(test.get_export(mod, "cjs_default_prop")) == "cjs-default-prop");

        // import 一个 JSON：默认导出是解析出来的值
        CHECK(test.to_number(test.get_export(mod, "json_answer")) == 42);
        CHECK(test.to_number(test.get_export(mod, "json_item_count")) == 3);

        // import "godot" 拿到引擎命名空间
        CHECK(test.to_bool(test.get_export(mod, "godot_node_is_function")));

        CHECK(test.to_string(test.get_export(mod, "default")) == "interop");
    }

    TEST_CASE("[jsb]esm through a commonjs middle module")
    {
        EsmTestCase test;

        JavaScriptModule* mod = test.load("tests/esm/chain");
        REQUIRE(mod != nullptr);
        CHECK(mod->is_native_esm());

        // 中间的 cjs_bridge.js 是 CommonJS，被 ESM 拉进来时走转发模块
        JavaScriptModule* bridge = test.load("tests/esm/cjs_bridge");
        REQUIRE(bridge != nullptr);
        CHECK(!bridge->is_native_esm());

        CHECK(test.to_string(test.get_export(mod, "leaf")) == "leaf-via-widget");
    }

    TEST_CASE("[jsb]esm cyclic imports")
    {
        EsmTestCase test;

        JavaScriptModule* cycle_a = test.load("tests/esm/cycle_a");
        REQUIRE(cycle_a != nullptr);
        CHECK(cycle_a->is_native_esm());

        // cycle_b 是被 cycle_a 的 import 拉进来的，再 load 一次只会拿缓存
        JavaScriptModule* cycle_b = test.load("tests/esm/cycle_b");
        REQUIRE(cycle_b != nullptr);
        CHECK(cycle_b->is_native_esm());

        // cycle_b 先求值，cycle_a 读到的已经是算好的值
        CHECK(test.to_string(test.get_export(cycle_a, "seenFromA")) == "b");

        // cycle_a 在求值之前就被 cycle_b 读过一次，靠函数声明才没踩到 TDZ
        CHECK(test.get_export(cycle_a, "getB")->IsFunction());
        CHECK(test.to_string(test.get_export(cycle_b, "seenFromB")) == "a");
    }

    TEST_CASE("[jsb]esm top-level await")
    {
        EsmTestCase test;

        JavaScriptModule* mod = test.load("tests/esm/tla");
        REQUIRE(mod != nullptr);
        CHECK(mod->is_native_esm());

        CHECK(test.to_number(test.get_export(mod, "ANSWER")) == 42);
        CHECK(test.to_string(test.get_export(mod, "default")) == "tla");
    }

    TEST_CASE("[jsb]esm import.meta")
    {
        EsmTestCase test;

        JavaScriptModule* mod = test.load("tests/esm/meta");
        REQUIRE(mod != nullptr);

        CHECK(test.to_string(test.get_export(mod, "url")).contains("tests/esm/meta.mjs"));
        CHECK(test.to_string(test.get_export(mod, "meta_keys")).contains("resolve"));

        // import.meta.resolve 把相对路径解析成资源路径
        CHECK(test.to_string(test.get_export(mod, "resolved")) == "res://tests/esm/extra.mjs");
    }

    TEST_CASE("[jsb]esm dynamic import")
    {
        EsmTestCase test;

        JavaScriptModule* mod = test.load("tests/esm/dyn");
        REQUIRE(mod != nullptr);

        // 动态 import 一个 `.mjs`
        CHECK(test.to_number(test.get_export(mod, "dyn_default")) == 42);
        CHECK(test.to_string(test.get_export(mod, "dyn_named")) == "world");

        // 动态 import 非 ESM 的文件也能拿到内容
        CHECK(test.to_string(test.get_export(mod, "dyn_cjs_label")) == "cjs");
        CHECK(test.to_number(test.get_export(mod, "dyn_json_answer")) == 42);
    }

    TEST_CASE("[jsb]esm required from a commonjs parent")
    {
        EsmTestCase test;

        JavaScriptModule* mod = test.load("tests/esm/cjs_requires_esm");
        REQUIRE(mod != nullptr);
        CHECK(!mod->is_native_esm());

        // require 一个 `.mjs` 拿到的是它的命名空间
        CHECK(test.to_number(test.get_export(mod, "via_require")) == 42);
        CHECK(test.to_string(test.get_export(mod, "via_require_named")) == "world");

        // CommonJS 自己读非标识符属性一直没问题，跟这次改动无关
        CHECK(test.to_string(test.get_export(mod, "via_require_odd")) == "odd-name");
    }

    TEST_CASE("[jsb]esm default-exported class becomes a script class")
    {
        EsmTestCase test;

        // 脚本类的登记在依赖图求值之后，`.mjs` 才能和 `.js` 一样当 Godot 脚本用
        JavaScriptModule* mod = test.load("tests/esm/widget_class");
        REQUIRE(mod != nullptr);
        CHECK((bool) mod->script_class_id);
        CHECK((bool) test.env->find_script_class(mod->script_class_id));
    }

    TEST_CASE("[jsb]esm module that throws reports the error every time")
    {
        EsmTestCase test;

        // 用全局 require 触发加载，异常要以 Error 的形式抛到调用处。
        // 同一个模块 require 两次：两次都得抛出原来的错误，不能第二次“成功”返回一个空模块
        const v8::Local<v8::String> code = impl::Helper::new_string(test.env->get_isolate(),
                "(() => { const out = []; for (let i = 0; i < 2; i++) {"
                "  try { require('tests/esm/boom'); out.push('no-throw'); }"
                "  catch (e) { out.push(String(e && e.message ? e.message : e)); } }"
                " return out.join('|'); })()");

        v8::Local<v8::Script> script;
        v8::Local<v8::Value> result;
        REQUIRE(v8::Script::Compile(test.env->get_context(), code).ToLocal(&script));
        REQUIRE(script->Run(test.env->get_context()).ToLocal(&result));

        CHECK(test.to_string(result) == "esm-boom|esm-boom");
    }

    TEST_CASE("[jsb]esm failed module is not handed out as loaded")
    {
        EsmTestCase test;

        JavaScriptModule* first = nullptr;
        CHECK(test.env->load("tests/esm/boom", &first) != OK);
        CHECK(first == nullptr);

        // 模块还在缓存里，但 V8 那边是 kErrored，再 load 一次仍然要失败
        JavaScriptModule* second = nullptr;
        CHECK(test.env->load("tests/esm/boom", &second) != OK);
        CHECK(second == nullptr);
    }

    TEST_CASE("[jsb]esm cannot import engine classes by name from the godot module")
    {
        EsmTestCase test;

        // `godot` 模块是个按需解析的 Proxy，列不出自有属性，具名导入在链接阶段就过不去，
        // 只能 `import gd from "godot"` 再 gd.Node。Node 对无法静态分析导出的模块也是这个行为
        JavaScriptModule* mod = nullptr;
        CHECK(test.env->load("tests/esm/godot_named", &mod) != OK);
        CHECK(mod == nullptr);
    }

    TEST_CASE("[jsb]esm top-level await that never settles fails")
    {
        EsmTestCase test;

        JavaScriptModule* first = nullptr;
        CHECK(test.env->load("tests/esm/stall", &first) != OK);
        CHECK(first == nullptr);

        // 求值中的模块不能重复求值，第二次 load 也必须失败而不是返回半成品
        JavaScriptModule* second = nullptr;
        CHECK(test.env->load("tests/esm/stall", &second) != OK);
        CHECK(second == nullptr);
    }

    TEST_CASE("[jsb]esm module is evaluated only once")
    {
        EsmTestCase test;

        // identity_main 从三条路径拿到同一个模块，`id` 必须完全相等（执行两次就会不相等）
        JavaScriptModule* mod = test.load("tests/esm/identity_main");
        REQUIRE(mod != nullptr);
        CHECK(test.to_bool(test.get_export(mod, "same")));
    }

    TEST_CASE("[jsb]esm module with a syntax error fails every load")
    {
        EsmTestCase test;

        // 编译期失败和运行期抛错走的是不同分支，两边都不能把模块当成加载成功
        JavaScriptModule* first = nullptr;
        CHECK(test.env->load("tests/esm/broken", &first) != OK);
        CHECK(first == nullptr);

        JavaScriptModule* second = nullptr;
        CHECK(test.env->load("tests/esm/broken", &second) != OK);
        CHECK(second == nullptr);
    }

    TEST_CASE("[jsb]esm importing a missing module fails")
    {
        EsmTestCase test;

        JavaScriptModule* mod = nullptr;
        CHECK(test.env->load("tests/esm/missing_dep", &mod) != OK);
        CHECK(mod == nullptr);

        JavaScriptModule* second = nullptr;
        CHECK(test.env->load("tests/esm/missing_dep", &second) != OK);
        CHECK(second == nullptr);
    }

    TEST_CASE("[jsb]esm dynamic import rejects when the target is missing")
    {
        EsmTestCase test;

        // 动态 import 失败要变成 promise reject，不能把整个模块加载搞崩
        JavaScriptModule* mod = test.load("tests/esm/dyn_fail");
        REQUIRE(mod != nullptr);
        CHECK(test.to_string(test.get_export(mod, "outcome")).begins_with("rejected:"));
    }

    TEST_CASE("[jsb]esm import.meta.resolve throws for an unknown specifier")
    {
        EsmTestCase test;

        JavaScriptModule* mod = test.load("tests/esm/meta_bad");
        REQUIRE(mod != nullptr);
        CHECK(test.to_string(test.get_export(mod, "outcome")).begins_with("threw:"));
    }

    TEST_CASE("[jsb]esm import propagates a failure from a commonjs target")
    {
        EsmTestCase test;

        // 转发模块里的 require 抛错，要一路传到调用方
        JavaScriptModule* mod = nullptr;
        CHECK(test.env->load("tests/esm/imports_bad_cjs", &mod) != OK);
        CHECK(mod == nullptr);

        JavaScriptModule* second = nullptr;
        CHECK(test.env->load("tests/esm/imports_bad_cjs", &second) != OK);
        CHECK(second == nullptr);
    }

    TEST_CASE("[jsb]esm specifier without an extension")
    {
        EsmTestCase test;

        // 不写扩展名也能解析到（Node 的 ESM 要求写全，我们的解析器会补扩展名）
        JavaScriptModule* mod = test.load("tests/esm/no_ext");
        REQUIRE(mod != nullptr);
        CHECK(test.to_string(test.get_export(mod, "v")) == "world");
    }

    TEST_CASE("[jsb]esm load entry module")
    {
        EsmTestCase test;

        // entry.mjs 里带了自检，导出对不上就会抛异常，load 不会返回 OK
        JavaScriptModule* mod = nullptr;
        CHECK(test.env->load("tests/esm/entry", &mod) == OK);
        CHECK(mod != nullptr);
    }

    TEST_CASE("[jsb]esm for a .js declared as module by its package.json")
    {
        EsmTestCase test;

        // 入口文件是 `.js`，但包自带的 package.json 写了 `"type": "module"`，
        // 按 CommonJS 读会在 `export` 关键字上抛语法错误
        ModuleSourceInfo info;
        CHECK(test.env->find_module_resolver("tests/esm/pkg_type_module", info));
        CHECK(info.source_filepath.ends_with("index.js"));

        JavaScriptModule* mod = test.load("tests/esm/pkg_type_module");
        // 下面的断言要读它的导出，拿着空指针继续跑会把整个测试进程带崩，这里先判负退出
        if (mod == nullptr)
        {
            return;
        }
        CHECK(mod->is_native_esm());
        CHECK(test.to_string(test.get_export(mod, "default")) == "pkg-default");
        CHECK(test.to_string(test.get_export(mod, "tag")) == "type-module-js");

        // 包内的相对 import 也按 ES 模块走
        CHECK(test.to_string(test.get_export(mod, "joined")) == "type-module-js/sub");
    }

    TEST_CASE("[jsb]esm relative import from a module at the res:// root")
    {
        EsmTestCase test;

        // 工程根目录下的模块，上一级目录是 `res://`。算成 `res:/` 时拼出来的路径找不到文件
        JavaScriptModule* mod = test.load("esm_root_a");
        // 同上：加载失败时不要拿着空指针继续读导出
        if (mod == nullptr)
        {
            return;
        }
        CHECK(test.to_string(test.get_export(mod, "value")) == "root-sibling");
    }
}

#endif // JSB_NATIVE_ESM && JSB_WITH_V8

#endif
