// 动态 import 失败要变成 promise reject，不能把加载整个搞崩
export const outcome = await import("./nope.mjs")
        .then(() => "resolved", (e) => "rejected:" + String(e && e.message ? e.message : e));
