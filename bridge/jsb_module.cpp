#include "jsb_module.h"
#include "jsb_bridge_helper.h"
#include "jsb_environment.h"

namespace jsb
{
    void JavaScriptModule::on_load(v8::Isolate* isolate, const v8::Local<v8::Context>& context)
    {
        Environment* environment = Environment::wrap(isolate);
        module.Get(isolate)->Set(context, jsb_name(environment, loaded), v8::Boolean::New(isolate, true)).Check();

        if (!source_info.source_filepath.is_empty())
        {
            environment->get_source_map_cache().invalidate(source_info.source_filepath);
        }
    }

    bool JavaScriptModule::mark_as_reloading()
    {
#if JSB_SUPPORT_RELOAD && defined(TOOLS_ENABLED)
        if (!is_reloadable()) return false;

        //TODO reload all related modules (search the module graph) ?
        //TODO inconsistent implementation, since the original time modified is read in module resolvers (SourceReader)
        const uint64_t latest_time = FileAccess::get_modified_time(source_info.source_filepath);
        if (latest_time && latest_time != time_modified)
        {
            time_modified = latest_time;

            const String latest_hash = FileAccess::get_md5(source_info.source_filepath);
            if (!latest_hash.is_empty() && latest_hash != hash)
            {
                hash = latest_hash;
                reload_requested = true;
                return true;
            }
        }
#endif
        return false;
    }

    void JavaScriptModule::mark_as_reloaded()
    {
#if JSB_SUPPORT_RELOAD && defined(TOOLS_ENABLED)
        reload_requested = false;
#endif
    }

    JavaScriptModule& JavaScriptModuleCache::insert(v8::Isolate* isolate, const v8::Local<v8::Context>& context, const StringName& p_name, bool p_main_candidate, bool p_init_loaded)
    {
        jsb_checkf(!((String) p_name).is_empty(), "empty module name is not allowed");
        jsb_checkf(!find(p_name), "duplicated module name %s", p_name);
        if (p_main_candidate && !internal::VariantUtil::is_valid_name(main_))
        {
            main_ = p_name;
            JSB_LOG(Verbose, "load main module %s", p_name);
        }
        else
        {
            JSB_LOG(VeryVerbose, "loading module %s", p_name);
        }

        Environment* environment = Environment::wrap(isolate);
        JavaScriptModule* module = memnew(JavaScriptModule);
        v8::Local<v8::Object> module_obj = v8::Object::New(isolate);
        v8::Local<v8::String> module_id = impl::Helper::new_string(isolate, p_name);
        modules_.insert(p_name, module);

        // register the new module obj into module_cache obj
        cache_object_.Get(isolate)->Set(context, module_id, module_obj).Check();

        // init the new module obj
        module_obj->Set(context, jsb_name(environment, id), module_id).Check();
        module_obj->Set(context, jsb_name(environment, loaded), v8::Boolean::New(isolate, p_init_loaded)).Check();

        module->id = p_name;
        module->module.Reset(isolate, module_obj);
        return *module;
    }

    bool JavaScriptModule::rethrow_esm_failure(v8::Isolate* p_isolate) const
    {
#if JSB_NATIVE_ESM && JSB_WITH_V8
        if (esm_evaluation_stalled)
        {
            impl::Helper::throw_error(p_isolate,
                    jsb_format("the esm module '%s' is still waiting for a top-level await, its exports are not available", id));
            return true;
        }

        if (!esm_module.IsEmpty() && esm_module.Get(p_isolate)->GetStatus() == v8::Module::kErrored)
        {
            // V8 把求值失败时的异常缓存在模块上，重新抛出去，行为才和“每次加载都失败”一致
            p_isolate->ThrowException(esm_module.Get(p_isolate)->GetException());
            return true;
        }

        if (esm_load_failed)
        {
            // 编译、链接失败时 V8 不会把异常存在模块上，只能给一条新错误，
            // 但同样不能让调用方拿到一个“加载成功但导出是空的”模块
            impl::Helper::throw_error(p_isolate, jsb_format("the esm module '%s' failed to load earlier", id));
            return true;
        }
#endif
        return false;
    }

}
