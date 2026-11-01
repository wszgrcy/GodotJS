#ifndef GODOTJS_MODULE_H
#define GODOTJS_MODULE_H

#include "jsb_bridge_pch.h"

namespace jsb
{
    class Environment;

    namespace ModuleReloadResult
    {
        enum Type : uint8_t
        {
            NoSuchModule,
            NoChanges,
            Requested,
            Disabled,
        };
    }

    struct ModuleSourceInfo
    {
        // source file path (also used as the moduleid in module_cache)
        String source_filepath;

        // [optional] filepath of package.json which the source file is indirectly resolved from
        String package_filepath;
    };

    struct JavaScriptModule
    {
        StringName id;

        // asset path
        ModuleSourceInfo source_info;

        v8::Global<v8::Object> module;
        v8::Global<v8::Value> exports;

        // the default class exported in this JS module
        ScriptClassID script_class_id;

#if JSB_NATIVE_ESM && JSB_WITH_V8
        // 由 NativeESMModuleResolver 用 `v8::ScriptCompiler::CompileModule` 编译出来的模块。
        // 走 CommonJS 路径加载的模块这里是空的。
        v8::Global<v8::Module> esm_module;

        // 顶层 await 还没 settle 就被同步入口取过一次。V8 不允许对求值中的模块再求值，
        // 所以这种模块后面也不能再用，只能报错。
        bool esm_evaluation_stalled = false;

        // 读源码、编译、链接、求值任一步失败过。编译和链接失败时 V8 不会把异常存在模块上，
        // 只能靠这个标记让后续加载继续报错，而不是“第二次加载成功但导出是空的”。
        bool esm_load_failed = false;
#endif

        /**
         * 这个模块之前求值失败、或者还卡在顶层 await 时，把它当成错误报出去，
         * 返回 true 表示已挂上异常、调用方不能把这个模块当“加载成功”交出去。
         * V8 会把求值失败时的异常缓存在模块上，这里就是把它重新抛出去。
         */
        bool rethrow_esm_failure(v8::Isolate* p_isolate) const;

        // 是否由 V8 原生模块机制加载。这类模块的导出要等整个依赖图求值完才读得到，
        // 脚本类的登记也要等求值完才能做。
        jsb_force_inline bool is_native_esm() const
        {
#if JSB_NATIVE_ESM && JSB_WITH_V8
            return !esm_module.IsEmpty();
#else
            return false;
#endif
        }

#if JSB_SUPPORT_RELOAD && defined(TOOLS_ENABLED)
        bool reload_requested = false;
        uint64_t time_modified = 0;
        String hash;

        jsb_force_inline bool is_reloading() const { return reload_requested; }

        // can't reload modules if it's time_modified is unknown or non-file modules
        bool is_reloadable() const { return time_modified != 0 && !source_info.source_filepath.is_empty(); }
#else
        jsb_force_inline constexpr bool is_reloading() const { return false; }
        jsb_force_inline constexpr bool is_reloadable() const { return false; }
#endif

        void on_load(v8::Isolate* isolate, const v8::Local<v8::Context>& context);
        bool mark_as_reloading();
        void mark_as_reloaded();

    };

    struct JavaScriptModuleCache
    {
    private:
        friend class Environment;

        StringName main_;
        HashMap<StringName, JavaScriptModule*> modules_;
        v8::Global<v8::Object> cache_object_;

    public:
        void init(v8::Isolate* isolate, const v8::Local<v8::Object>& cache_obj)
        {
            cache_object_.Reset(isolate, cache_obj);
        }

        void deinit()
        {
            cache_object_.Reset();
            for (const KeyValue<StringName, JavaScriptModule*>& it : modules_)
            {
                memdelete(it.value);
            }
            modules_.clear();
        }

        jsb_force_inline ~JavaScriptModuleCache()
        {
            jsb_check(cache_object_.IsEmpty());
            jsb_check(modules_.is_empty());
        }

        jsb_force_inline JavaScriptModule* find(const StringName& p_name) const
        {
            JavaScriptModule* const * it = modules_.getptr(p_name);
            return it ? *it : nullptr;
        }

        jsb_force_inline JavaScriptModule* get_main() const
        {
            return find(main_);
        }

        jsb_force_inline bool is_main(const StringName& p_name) const
        {
            return p_name == main_;
        }

        jsb_force_inline v8::Local<v8::Object> get_cache(v8::Isolate* isolate) const
        {
            return cache_object_.Get(isolate);
        }

        JavaScriptModule& insert(v8::Isolate* isolate, const v8::Local<v8::Context>& context, const StringName& p_name, bool p_main_candidate, bool p_init_loaded);
    };

}

#endif
