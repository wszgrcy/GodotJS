// 手写的 ESM 场景脚本（不经 tsc 转换，所以 `import` 是真正的 import，不会被降级成 require）。
//
// 一个场景里查同一个脚本类的两条加载路径，产物都只有 `.mjs`：
//
// 一、按 `.ts` 源路径加载（`PathUtil::convert_typescript_path` 正向映射）。
// 只拼 `.js` 的实现下引擎找不到产物，`can_instantiate()` 返回 false，继续把脚本装到节点上
// 会在 C++ 里 FATAL 崩进程，所以先断言 `can_instantiate()` 再装脚本，让失败停在可断言的那步。
//
// 二、裸模块名 import（模块解析器的搜索路径：`.godot/GodotJS`、`res://`、`res://node_modules`，
// 见 `bridge/jsb_environment.cpp`）。静态和动态 import 都走一遍，然后 `new` 出实例。
// `new` 这步会走 `ScriptClassInfo::instantiate`（`bridge/jsb_class_info.cpp`）：它拿产物路径用
// `convert_javascript_path` 反推出 `.ts` 源，加载那个 Script 时才建真正的引擎对象，
// 所以 `get_class()` 能验出「new 出来的是不是真引擎对象」。
//
// fixture：源 `res://libs/lib-probe.ts`（被 tsconfig 排除，所以没有 `.js` 产物），产物
// `.godot/GodotJS/libs/lib-probe.mjs` 手工提交在那里（静态 import 在场景加载时就要解析，
// 来不及等运行时复制）。
//
// `godot` 模块是 Proxy，不提供具名导出，所以只能整体取。
import gd from "godot";
import { reportTestFailure, beginAsyncTest, endAsyncTest } from "tests/test-status.js";
import LibProbe from "libs/lib-probe";

/** 脚本类源文件 */
const SOURCE = "res://libs/lib-probe.ts";
/** 手工提交的 ESM 产物，位于模块解析器搜索路径的第一条 */
const ARTIFACT = "res://.godot/GodotJS/libs/lib-probe.mjs";
/** 上一轮 tsc 或旧构建可能留下的 CJS 产物，用例要确保没有 */
const CJS_LEFTOVER = "res://.godot/GodotJS/libs/lib-probe.js";

function check(label, ok, detail) {
    if (!ok) {
        throw new Error(`script-class: ${label} —— ${detail}`);
    }
}

/** 一、`.ts` 源路径 → 产物：装到节点上用 */
function checkViaScriptResource() {
    check("产物目录里没有 .js", !gd.FileAccess.file_exists(CJS_LEFTOVER), `${CJS_LEFTOVER} 还在，用例前提不成立`);

    const script = gd.ResourceLoader.load(SOURCE);
    check("脚本资源能加载", script !== null, `ResourceLoader.load(${SOURCE}) 返回空`);
    check(
        "产物只有 .mjs 时脚本能实例化",
        script.can_instantiate() === true,
        `引擎按 ${SOURCE} 找不到产物；产物目录里只有 ${ARTIFACT}`,
    );

    const host = new gd.Node();
    host.set_script(script);
    check("脚本装到节点上后方法可用", host.thrice(7) === 21, "thrice(7) 不等于 21");
    host.free();
}

/** 断言 new 出来的是真引擎对象，不是没绑定的 JS 空壳 */
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
            checkViaScriptResource();
            // 静态 import 在场景脚本求值时就解析，动态 import 在运行中解析，两条路都要盖到。
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
