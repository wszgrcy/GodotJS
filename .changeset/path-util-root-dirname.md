---
"@godot-js/editor": patch
---

修复工程根目录下的模块算上级目录时少一个斜杠，相对 import 找不到文件

`PathUtil::dirname("res://a.mjs")` 返回 `res:/`：它只剔掉最后一个斜杠，把协议后面的双斜杠
也削掉了一个。相对 import 靠它拼路径，拼出 `res:/b.mjs` 后过不了路径归一化，解析器报
`unknown module`。后果是放在工程根目录的模块没法相对 import 同目录的兄弟模块，
`module.path` 也跟着是错的 `res:/`。

`dirname` 现在把截出来、以 `:/` 结尾的结果补回一个斜杠（`res:/` → `res://`）。
`res://a/b.js`、绝对路径、Windows 盘符路径的行为不变。

回归用例在 `tests/jsb/test_jsb_esm_runtime.h`：工程根目录下的 `esm_root_a.mjs` 相对 import
同目录的 `esm_root_b.mjs`。未打补丁判负（解析到 `res:/esm_root_b.mjs`），打过判过。
