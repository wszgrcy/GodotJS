// 脚本类的产物可以只有 `.mjs`（构建器输出原生 ESM 的情形）。
//
// 夹具是 `res://libs/lib-probe.ts` 加手工提交的 `.godot/GodotJS/libs/lib-probe.mjs`；
// 源文件被 tsconfig 排除，所以 tsc 不会给它生成 `.js`。
//
// 引擎把 `.ts` 源路径映射成产物路径时（`PathUtil::convert_typescript_path`）如果只拼 `.js`，
// 就找不到产物：`can_instantiate()` 返回 false，日志里是 `the javascript file is missing`。
//
// 本文件故意不经 tsc：`.ts` 会被编成 CommonJS，`import` 会变成 `require`，盖不到原生 ESM
// 那条路。它挂在子节点而不是场景根上，是因为场景的代码生成会给根脚本生成一句 `import`，
// 而本项目的 `.ts` 按 CommonJS 编译，`import` 一个 `.mjs` 会被 tsc 判错。
import gd from "godot";
import { reportTestFailure, beginAsyncTest, endAsyncTest } from "tests/test-status.js";
import LibProbe from "libs/lib-probe";

/** 脚本类源文件 */
const SOURCE = "res://libs/lib-probe.ts";
/** 手工提交的 ESM 产物 */
const ARTIFACT = "res://.godot/GodotJS/libs/lib-probe.mjs";
/** 上一轮构建可能留下的 CJS 产物，用例要确保没有 */
const CJS_LEFTOVER = "res://.godot/GodotJS/libs/lib-probe.js";

function check(label, ok, detail) {
    if (!ok) {
        throw new Error(`script-class: ${label} —— ${detail}`);
    }
}

/** new 会走 ScriptClassInfo::instantiate：它把产物路径反推成 `.ts` 源，加载那个脚本时才建
 *  真正的引擎对象，反推不到就是个没绑定的 JS 空壳（JS 方法能调，get_class() 报 Bad this）。 */
function checkInstantiable(label, Lib) {
    check(`${label}：默认导出是类`, typeof Lib === "function", "默认导出不是类");
    const instance = new Lib();
    check(`${label}：new 出来的是引擎对象`, instance.get_class() === "Node", `get_class() 返回 ${instance.get_class()}`);
    check(`${label}：实例上的子类方法可用`, instance.thrice(7) === 21, "thrice(7) 不等于 21");
    instance.free();
}

export default class TestScriptClass extends gd.Node {
    async _ready() {
        beginAsyncTest();
        try {
            check("产物目录里没有 .js", !gd.FileAccess.file_exists(CJS_LEFTOVER), `${CJS_LEFTOVER} 还在，前提不成立`);

            const script = gd.ResourceLoader.load(SOURCE);
            check("脚本能实例化", script !== null && script.can_instantiate() === true, `引擎按 ${SOURCE} 找不到产物，产物目录里只有 ${ARTIFACT}`);
            check("脚本类基类解析正确", script.get_instance_base_type() === "Node", `get_instance_base_type() 返回 ${script.get_instance_base_type()}`);

            checkInstantiable("静态 import", LibProbe);

            const mod = await import("libs/lib-probe");
            check("动态 import 与静态 import 是同一个模块", mod.default === LibProbe, "两次解析拿到的不是同一个类");
            checkInstantiable("动态 import", mod.default);
        } catch (error) {
            // 只报失败。完成信号由 start.ts 统一打，场景用例自己打会让套件提前算作完成。
            reportTestFailure("script-class", error);
        } finally {
            endAsyncTest();
        }
    }
}
