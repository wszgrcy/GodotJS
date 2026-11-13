#ifndef GODOTJS_NATIVE_ESM_RESOLVER_H
#define GODOTJS_NATIVE_ESM_RESOLVER_H

#include "jsb_bridge_pch.h"

#if JSB_NATIVE_ESM && JSB_WITH_V8

#include "jsb_module_resolver.h"

namespace jsb
{
    /**
     * 用 V8 自带的模块机制加载 ES 模块：`import` / `export` 直接交给 V8，
     * 不再像 CommonJS 那样把源码包进一个函数里执行。
     *
     * 走这里的文件：`.mjs`，以及那些被自己那份 package.json 声明为 `"type": "module"` 的 `.js`。
     * 其余 `.js` / `.cjs` / `.json` 仍由 `DefaultModuleResolver` 按 CommonJS 加载。
     * ES 模块里 import 一个非 ESM 文件时，会临时生成一个转发模块，见 `create_bridge_module`。
     */
    class NativeESMModuleResolver : public DefaultModuleResolver
    {
    public:
        virtual bool get_source_info(const String& p_module_id, ModuleSourceInfo& r_source_info) override;

        /** 见 `IModuleResolver::load`：`p_defer_esm_evaluation` 为 true 时只编译，求值交给最外层那次调用 */
        virtual bool load(Environment* p_env, const String& p_asset_path, JavaScriptModule& p_module, bool p_defer_esm_evaluation) override;

        /** 给 isolate 挂上 `import()` 和 `import.meta` 的宿主回调，`Environment::init` 里调一次 */
        static void install_host_callbacks(Environment* p_env);

    private:
        /** V8 实例化模块图时逐个回调，把 import 的说明符换成一个已编译的模块 */
        static v8::MaybeLocal<v8::Module> resolve_module_callback(
            v8::Local<v8::Context> p_context,
            v8::Local<v8::String> p_specifier,
            v8::Local<v8::FixedArray> p_import_attributes,
            v8::Local<v8::Module> p_referrer);

        /** 宿主侧的 `import()`：同步加载并求值，结果包成 promise 交回去 */
        static v8::MaybeLocal<v8::Promise> host_import_module_dynamically(
            v8::Local<v8::Context> p_context,
            v8::Local<v8::Data> p_host_defined_options,
            v8::Local<v8::Value> p_resource_name,
            v8::Local<v8::String> p_specifier,
            v8::Local<v8::FixedArray> p_import_attributes);

        /** 第一次读 `import.meta` 时由 V8 回调，这里填 `url` */
        static void initialize_import_meta(
            v8::Local<v8::Context> p_context,
            v8::Local<v8::Module> p_module,
            v8::Local<v8::Object> p_meta);

        /** 编译源码并记下 script_id。命名空间要等实例化之后才拿得到，这里先不碰 exports */
        static bool compile_module(Environment* p_env, const String& p_asset_path, const String& p_source, JavaScriptModule& p_module);

        /** 从文件所在目录往上找最近的 package.json（工程根目录那份不算），判断它有没有把 `.js` 声明为 ES 模块 */
        bool package_boundary_declares_esm(const String& p_source_path);

        /** 实例化并求值整个依赖图，再把图里每个模块的导出接到自己的命名空间上 */
        bool instantiate_and_evaluate(Environment* p_env, JavaScriptModule& p_root);

        /** 给一个非 ESM 模块（CommonJS / JSON / `godot`）生成转发模块 */
        static v8::Local<v8::Module> create_bridge_module(
            Environment* p_env,
            const v8::Local<v8::Context>& p_context,
            const String& p_module_id,
            const v8::Local<v8::Value>& p_exports);

        // 当前这次求值里编译出来的模块，求值结束后统一接导出、登记脚本类
        Vector<JavaScriptModule*> graph_modules_;

        // 目录 -> 该目录下最近的 package.json 是否把 `.js` 声明为 ES 模块，一个目录只读一次
        HashMap<String, bool> package_esm_cache_;
    };
}

#endif // JSB_NATIVE_ESM && JSB_WITH_V8

#endif
