---
"@godot-js/editor": patch
---

V8 运行时升级到 15.7.112.12

引擎内嵌的 V8 从 12.6.228.49 升到 15.7.112.12，依赖包取 release 1.4.0。
`Intl`、`Intl.Segmenter`、`\p{...}` Unicode 属性正则、`Date.toLocaleDateString` 都照常可用，
仍需要 `icudtl.dat`（与可执行文件同目录，或用项目设置 `GodotJS/runtime/intl/icu_data_path` 指定）。

升级带来的三处连带改动，写代码时会碰到：

- V8 15 要求 C++20，模块的编译标准从 `gnu++17` 抬到 `gnu++20`。这只影响编译 GodotJS 模块的
  编译器：CI 的 Linux runner 是 ubuntu-22.04（GCC 11），C++20 的"反向候选"规则它实现得不对，
  `Variant == Ref<T>` 会被判成二义；`u8"..."` 在 C++20 里是 `char8_t` 数组，不能初始化 `char[]`。
  两处都改成与标准无关的写法。
- V8 14.1 起 `Context`/`Object`/`Message` 的 `GetIsolate()` 被删除（15 里已移除），
  官方给的替代是 `v8::Isolate::GetCurrent()`，并保证返回同一个 isolate。原生 ESM 解析器的
  三个回调改用这个写法。
- V8 15 的静态库有 17 处未定义的 `__atomic_compare_exchange`，Linux 与 Android 链接时要补
  `-latomic`，已由模块的构建脚本加上，使用者不用管。

本地验证：Linux x86_64 编辑器构建 0 error，`[jsb]*` 单元测试 49 例 / 33679 断言全过，
测试工程跑通且日志里的 ERROR 与升级前逐行一致；quickjs-ng 后端同样通过。
Windows、macOS、iOS、Android 由 CI 覆盖。
