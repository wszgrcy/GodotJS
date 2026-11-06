---
"@godot-js/editor": patch
---

脚本类产物允许是 `.mjs`：`.ts` 源路径不再硬拼成 `.js`

`PathUtil::convert_typescript_path` 把 `.ts` 源路径映射成编译产物的路径，原来只在结尾拼
`.js`。于是产物只有 `.mjs` 时（构建器输出原生 ESM 的情形）引擎找不到产物：脚本资源的
`can_instantiate()` 返回 false，日志里是 `the javascript file is missing`；要是继续把脚本装到
节点上，会在 C++ 里 `FATAL` 崩进程，JS 侧捕获不到。

这是原生 ESM 落地时漏改的一处。反向映射 `convert_javascript_path`（产物 → `.ts`）和
`DefaultModuleResolver::check_implicit_source_path`（模块名 → 产物）早就认 `.mjs` 了，只有
正向这一条还写死 `.js`。

现在正向也按 `.js` → `.cjs` → `.mjs` 依次探测文件是否存在，顺序与
`check_implicit_source_path` 保持一致，只编好 CJS 产物的工程仍然一次命中。三者都不存在时
回落到 `.js`，让「产物缺失」的报错仍指向最常见的名字，日志文案不变。

回归用例在 `tests/project/tests/script-class/`，挂在 `pnpm test` 的场景清单里。夹具是
`res://libs/lib-probe.ts`（被 tsconfig 排除，所以没有 `.js` 产物）加手工提交的
`.godot/GodotJS/libs/lib-probe.mjs`。根脚本 `test-script-class.ts` 断言 `ResourceLoader.load`
加载 `.ts` 源之后 `can_instantiate()` 为真、脚本类基类解析成 `Node`；只拼 `.js` 的实现下
`can_instantiate()` 返回 false，继续把脚本装到节点上会 FATAL 崩进程，所以断言停在那里。
子节点 `import-probe.mjs` 是手写的原生 ESM（`.ts` 会被编成 CommonJS，`import` 会变成
`require`，盖不到这条路），用裸模块名静态和动态 `import` 同一个类，各自 `new` 出实例并断言
`get_class()` 返回 `"Node"`。`new` 会走 `ScriptClassInfo::instantiate`，它把产物路径反推成
`.ts` 源、加载那个脚本时才建真正的引擎对象，反推不到就只是个没绑定的 JS 空壳（JS 方法能调，
`get_class()` 报 Bad this）。
