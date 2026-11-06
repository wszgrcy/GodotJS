import { Node } from "godot";

/**
 * 脚本类源文件，产物只有 `.mjs`（由手工提交的 `.godot/GodotJS/libs/lib-probe.mjs` 提供）。
 * 这个文件被 tsconfig 排除，tsc 不会生成 `.js`。
 *
 * 它必须存在：`new` 一个脚本类时，引擎用 `convert_javascript_path` 把产物路径反推成 `.ts`
 * 源路径，再加载那个 Script 来建真正的引擎对象（bridge/jsb_class_info.cpp 的
 * ScriptClassInfo::instantiate）。源文件不在，new 出来就是个没绑定的 JS 空壳。
 *
 * 这里用 `import { Node }` 而产物 `.mjs` 用 `import gd from "godot"`，是因为两者的模块格式
 * 不同：`.ts` 编出来是 CommonJS，取具名成员；`.mjs` 是真 ESM，而 `godot` 模块只保证默认导出。
 */
export default class LibProbe extends Node {
    thrice(x: number): number {
        return x * 3;
    }
}
