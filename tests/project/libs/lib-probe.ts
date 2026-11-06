import gd from "godot";

/**
 * 脚本类源文件，产物只有 `.mjs`（由 `tests/script-class/lib-probe.mjs` 在运行时复制到
 * `.godot/GodotJS/libs/`）。这个文件被 tsconfig 排除，tsc 不会生成 `.js`。
 *
 * 它必须存在：`new` 一个脚本类时，引擎用 `convert_javascript_path` 把产物路径反推成 `.ts`
 * 源路径，再加载那个 Script 来建真正的引擎对象（bridge/jsb_class_info.cpp 的
 * ScriptClassInfo::instantiate）。源文件不在，new 出来就是个没绑定的 JS 空壳。
 */
export default class LibProbe extends gd.Node {
    thrice(x: number): number {
        return x * 3;
    }
}
