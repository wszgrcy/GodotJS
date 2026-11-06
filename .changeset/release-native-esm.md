---
"@godot-js/editor": minor
---

发布包打开原生 ESM（构建参数 `jsb_native_esm=yes`）

之前这个能力只在源码自编译、且手动传了开关时才有，发布包里没编进去，
`.mjs` 在发布包里仍然只能按 CommonJS 执行。现在 Linux / Windows / macOS /
Android / iOS 的构建统一带上该开关，发布包里的 `.mjs` 直接走 V8 的模块机制。

开关本身没动，默认仍是关闭：自己编引擎的人不传参数就保持原有行为。
web 平台用的是浏览器自己的 JS 引擎，没有 V8，这个开关对它不起作用，所以没加。
