#ifndef GODOTJS_MODULE_RESOLVER_H
#define GODOTJS_MODULE_RESOLVER_H

#include "jsb_bridge_pch.h"
#include "jsb_module.h"

namespace jsb
{
    class IModuleResolver
    {
    public:
        virtual ~IModuleResolver() = default;

        // check if the given module_id is valid to resolve
        // `r_asset_path` will be set if there is an available source file in `FileAccess` which matches the given `module_id`
        virtual bool get_source_info(const String& p_module_id, ModuleSourceInfo& r_source_info) = 0;

        /**
         * load source into the module
         * `exports' will be set into `p_module.exports` if loaded successfully
         *
         * `p_defer_esm_evaluation`: 为 true 表示这是 V8 在实例化依赖图的过程中回调进来的，
         * 调用方此刻只想要编译结果。支持原生 ESM 的实现此时只编译不执行，
         * 求值交给最外层那一次调用统一完成，循环依赖才不会断。
         * 不支持原生 ESM 的解析器直接忽略这个参数。
         */
        virtual bool load(Environment* p_env, const String& p_asset_path, JavaScriptModule& p_module, bool p_defer_esm_evaluation = false) = 0;

        // `p_filename_abs` the absolute file path accessible for debugger
        static bool load_from_evaluator(Environment* p_env, JavaScriptModule& p_module, const String& p_asset_path, const v8::Local<v8::Function>& p_elevator);
        static bool load_as_json(Environment* p_env, JavaScriptModule& p_module, const String& p_asset_path, const Vector<uint8_t>& p_bytes, size_t p_len);
    };

    /**
     * the default module resolver finds source files directly with `FileAccess` in all `search_paths`.
     * The module is defined in commonjs style.
     */
    class DefaultModuleResolver : public IModuleResolver
    {
    public:
        virtual ~DefaultModuleResolver() override = default;

        virtual bool get_source_info(const String& p_module_id, ModuleSourceInfo& r_source_info) override;

        /** load module from asset path (use FileAccessSourceReader) */
        // CommonJS 没有稍后再求值这一说，p_defer_esm_evaluation 在这里用不上
        virtual bool load(Environment* p_env, const String& p_asset_path, JavaScriptModule& p_module, bool p_defer_esm_evaluation = false) override;

        DefaultModuleResolver& add_search_path(const String& p_path);

        /** Compile source from reader (in commonjs style) and init as module */
        static bool load(Environment* p_env, const String& p_asset_path, const internal::ISourceReader& p_reader, JavaScriptModule& p_module);

    protected:
        bool check_absolute_file_path(const String& p_module_id, ModuleSourceInfo& o_source_info);
        bool check_package_file_path(const String& p_package_path, const String& p_module_id, ModuleSourceInfo& o_source_info);
        bool check_search_path(const String& p_search_path, const String& p_module_id, ModuleSourceInfo& o_source_info);

        static String resolve_package_export(const Dictionary& p_exports, const String& p_condition, const String& p_module_id);

        // read the source buffer (transformed into commonjs)
        static size_t read_all_bytes_with_shebang(const internal::ISourceReader& p_reader, Vector<uint8_t>& o_bytes);

        static bool check_implicit_source_path(const String& p_module_id, String& o_path);

    private:

        Dictionary package_exports_cache;

        // Helper function for recursive resolution of exports target values
        // p_value: The value retrieved from package.json exports map
        // p_condition: The conditional key to prioritize. In our case, most likely "require".
        // p_subpath: The portion of path (suffix) which did not match the lookup prefix
        // p_wildcard: True if the matched exports key contained a wildcard ('*')
        static String resolve_package_export_value(const Variant& p_value, const String& p_condition, const String& p_subpath, bool p_wildcard);

        Vector<String> search_paths_;
    };
}

#endif
