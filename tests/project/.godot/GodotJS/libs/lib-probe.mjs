// 手写的 ESM 产物，手工放在这里（不在 tsconfig 的 include 里，tsc 不会生成也不会删）。
// `.godot/GodotJS` 是模块解析器搜索路径的第一条，所以场景脚本里可以直接
// `import LibProbe from "libs/lib-probe"`。
//
// 配套的源文件 `res://libs/lib-probe.ts` 必须存在：`new` 脚本类时，引擎用
// `convert_javascript_path` 把产物路径反推成 `.ts` 源路径，加载那个 Script 才建真正的
// 引擎对象（bridge/jsb_class_info.cpp 的 ScriptClassInfo::instantiate）。
import gd from "godot";

export default class LibProbe extends gd.Node {
    thrice(x) {
        return x * 3;
    }
}
