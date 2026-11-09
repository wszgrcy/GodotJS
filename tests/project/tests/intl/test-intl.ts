import { Node, ProjectSettings } from "godot";

// 判负靠这行 sentinel：runner 要求输出里必须出现 COMPLETED，且一旦出现 FAILED 就判失败。
const FAILURE_SENTINEL = "GODOTJS_TEST_PROJECT_FAILED";

let failed = false;

function check(label: string, ok: boolean, detail: string) {
    if (!ok) {
        failed = true;
        console.error(`${FAILURE_SENTINEL}: intl: ${label}`);
        console.error(detail);
    }
}

// 每项单独包一层：ICU 数据缺失时 Intl 会抛异常，抛在哪一项不该让后面的项跟着消失。
function attempt(label: string, fn: () => string) {
    try {
        check(label, true, "");
        return fn();
    } catch (error) {
        failed = true;
        console.error(`${FAILURE_SENTINEL}: intl: ${label} 抛异常`);
        console.error(error);
        return "<threw>";
    }
}

/**
 * 覆盖 ICU 数据到位之后的 Intl 与 Unicode 正则。
 *
 * V8 的 ICU 语言资源是一个单独的 icudtl.dat，不在预编译的 V8 库里，V8 也不会自己去找它。
 * 引擎启动时没把它加载上，后果不是可捕获的报错：Intl.* 全部抛 "Internal error. Icu error."，
 * 而 Date.prototype.toLocaleDateString 会被 V8 当成内存耗尽，直接终止整个进程。
 * 所以这组用例既验"功能能用"，也顺带验数据文件真的跟着引擎一起装好了。
 *
 * 运行时没有 Intl（例如 quickjs）时整组跳过：这些断言只对编进 ICU 的 V8 有意义。
 */
export default class TestIntl extends Node {
    _ready() {
        try {
            if (typeof Intl === "undefined") {
                console.log("TestIntl ready (skipped: 这个运行时没有 Intl)");
                return;
            }

            // 键名被改的话，用户按文档填的路径会静默失效（填了不报错、Intl 依旧坏），所以钉一下。
            const configured = ProjectSettings.get_setting("GodotJS/runtime/intl/icu_data_path");
            check("项目设置里有 ICU 数据路径这一项", typeof configured === "string",
                `实际类型 ${typeof configured}`);

            // Unicode 属性转义用的是 ICU 编死的字符属性表，不需要 icudtl.dat，
            // 但它和下面几项同属"ICU 有没有编进 V8"这一件事，放在一起看更完整。
            check("\\p{Script=Han} 匹配汉字", /\p{Script=Han}/u.test("中"), "汉字没匹配上");
            check("\\p{Script=Han} 不匹配拉丁字母", !/\p{Script=Han}/u.test("A"), "拉丁字母竟然匹配了");

            // 按语言排序与按码点排序结果不同：码点是 "B"(0x42) < "a"(0x61)，
            // 任何语言的排序规则都是 a < B。拿到码点序就说明 ICU 数据没生效。
            const collate = attempt("Intl.Collator", () => new Intl.Collator("en").compare("B", "a").toFixed());
            check("Collator 按语言排序（a 在 B 前）", Number(collate) > 0, `compare("B","a")=${collate}`);

            const grouped = attempt("Intl.NumberFormat", () => new Intl.NumberFormat("en-US").format(1234.5));
            check("NumberFormat 有千分位", grouped === "1,234.5", `实际 ${grouped}`);

            const formatted = attempt("Intl.DateTimeFormat",
                () => new Intl.DateTimeFormat("en-US", { year: "numeric" }).format(new Date(Date.UTC(2026, 0, 2))));
            check("DateTimeFormat 能格式化年份", formatted.includes("2026"), `实际 ${formatted}`);

            const relative = attempt("Intl.RelativeTimeFormat", () => new Intl.RelativeTimeFormat("en").format(-1, "day"));
            check("RelativeTimeFormat 输出文案", relative === "1 day ago", `实际 ${relative}`);

            // 这一项是当初最先出事的：ICU 代码在、数据不在时，V8 在这里直接终止进程。
            const dateText = attempt("Date.prototype.toLocaleDateString",
                () => new Date(Date.UTC(2026, 0, 2)).toLocaleDateString("en-US"));
            check("toLocaleDateString 能格式化年份", dateText.includes("2026"), `实际 ${dateText}`);

            const numberText = attempt("Number.prototype.toLocaleString", () => (1234.5).toLocaleString("en-US"));
            check("Number.toLocaleString 有千分位", numberText === "1,234.5", `实际 ${numberText}`);
        } catch (error) {
            failed = true;
            console.error(`${FAILURE_SENTINEL}: intl`);
            console.error(error);
        }
        // 这行用来确认用例真的被加载执行了：脚本挂不上时引擎只报 ERROR，sentinel 照样打全
        console.log(`TestIntl ready (${failed ? "FAILED" : "ok"})`);
    }
}
