---
"@godot-js/editor": minor
---

新增原生 ESM 模块解析器（`jsb_native_esm`，仅 V8，默认关闭）

打开开关后，`.mjs` 文件不再被包成 CommonJS 函数执行，而是交给 V8 自己的模块机制编译，
`import` / `export` 按标准语义工作：

- 默认导出、具名导出、`export * from`、命名空间导入
- 循环依赖（依赖只编译不执行，求值统一放在最外层，不会读到半初始化的模块）
- 顶层 `await`：能在同步里算完的（`await` 一个已就绪的 Promise）正常拿到值；
  等的是真实异步操作时直接报错，并用 V8 的信息指出卡在哪个文件，不会返回半成品导出
- `import()` 动态导入，返回真正的 Promise
- `import.meta.url` 与 `import.meta.resolve()`（值是引擎路径 `res://...`，不是 `file://` URL）
- CommonJS 里 `require("./x.mjs")` 拿到的是该模块的命名空间
- `.mjs` 默认导出一个继承引擎类型的类时，照样登记成 Godot 脚本类

从 `.mjs` 里 import 非 ESM 的东西（`.js` / `.cjs` / `.json` / `godot`）时，会自动生成一个
转发模块：默认导出是它的 `module.exports` / 解析后的 JSON / 引擎命名空间，同时按属性给出
具名导出。属性名不是合法标识符的（`"some-name"`、`"0"`）走 ESM 的字符串导出名，不会丢属性。

```js
import cjs, { from_cjs } from "./legacy.js";   // 默认导出 = module.exports
import data from "./data.json";
import gd from "godot";
const { Node, Vector2 } = gd;                  // 引擎类要这样拿，见下面的限制
```

失败处理对齐 Node：加载失败的模块不会留在"已加载"状态。同一个模块再 `require` 一次，
会把 V8 缓存在模块上的原始异常重新抛出去（Node 的 `ModuleWrap::runSync` 就是这么做的），
不会出现"第二次加载成功、拿到一个空模块"。顶层 await 卡住的模块同理，且 V8 不允许对
求值中的模块重复求值，所以它被标记为不可用。

其余扩展名（`.js` / `.cjs` / `.json`）仍走原来的 CommonJS 路径，行为不变。

已知限制：

- 加载失败过的模块（语法错、缺依赖、求值抛错、顶层 await 卡住）会一直失败，直到模块缓存重建；
  改了文件要等上下文重建才能重新试（Node 的 ESM 缓存也是这个行为）
- 说明符可以省略扩展名（`import "./hello"` 能解析到 `hello.mjs`），比 Node 的 ESM 宽松
- **`godot` 模块只能默认导入**。它是个按需解析的 Proxy，列不出自有属性，所以
  `import { Node } from "godot"` 会在链接阶段报"does not provide an export named 'Node'"。
  写成 `import gd from "godot"` 再 `gd.Node` 或解构即可。Node 对无法静态分析导出的模块
  也是同样行为。打包成 CommonJS 的 TS 代码不受影响（具名 import 会变成属性访问）。
- 转发模块导出目标对象的所有自有属性，包括不可枚举的；Node 只导可枚举的。
  `module.exports.default` 不会单独导出（`default` 已被默认导出占用），但 `cjs.default` 能取到。
- 只有 V8 实现有，QuickJS / JavaScriptCore 没有对应实现
- 所有入口都是同步的，所以真正需要异步等待的顶层 await 用不了（Node 的入口是异步的）
- `import.meta` 只填 `url` 和 `resolve`，没有 `main` / `filename` / `dirname`
- 转发模块是生成一段 ESM 源码再编译（Node 用 V8 的 SyntheticModule，不生成源码），
  每个非 ESM 依赖多一次编译

编译开关：`scons ... jsb_native_esm=yes`，实现见 `bridge/jsb_native_esm_resolver.{h,cpp}`，
用例见 `tests/test_jsb_esm_runtime.h` 与 `tests/project/tests/esm/`。
