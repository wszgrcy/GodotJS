---
"@godot-js/editor": patch
---

让 package.json 声明为 `"type": "module"` 的包里的 `.js` 按 ES 模块加载

npm 上有一批包把自己声明成 ES 模块（package.json 写 `"type": "module"`），入口仍然是
`.js`。GodotJS 之前只把 `.mjs` 当 ES 模块，`.js` 一律按 CommonJS 读——把源码包进
`(function(exports, require, module, __filename, __dirname){ ... })` 再执行。这类包的
`export` 关键字落进函数体里，直接抛 `SyntaxError: Unexpected token 'export'`，
整个包 import 不进来。

ES 模块解析器现在多认一条规则：从文件所在目录往上找最近的 package.json，它写了
`"type": "module"` 就把这个 `.js` 交给 V8 的模块机制加载。工程根目录那份 package.json
不参与判断——它描述的是构建工具的模块格式（很多工程因为它写了 `"type": "module"`），
不代表引擎里 `.js` 的加载格式；只认包边界上的声明。每个目录只读一次 package.json。

回归用例在 `tests/jsb/test_jsb_esm_runtime.h`，夹具是 `tests/project/tests/esm/pkg_type_module/`
（package.json 声明 module，入口 index.js 里再相对 import 同目录的 sub.js）。
未打补丁判负（`Cannot use import statement outside a module`），打过判过。
