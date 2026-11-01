// 动态 import：和顶层 await 一起用，这样同步就能拿到结果
const hello = await import("./hello.mjs");
const cjs = await import("./cjs_target.js");
const data = await import("./data.json");

export const dyn_default = hello.default;
export const dyn_named = hello.HELLO;
export const dyn_cjs_label = cjs.label;
export const dyn_json_answer = data.answer;

export async function load_extra() {
    const mod = await import("./extra.mjs");
    return mod.FROM_EXTRA;
}
