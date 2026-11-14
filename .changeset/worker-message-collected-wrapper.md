---
"@godot-js/editor": patch
---

worker 的 JS 对象已被回收时，它发来的消息改为丢弃，不再把引擎进程打死

高频创建又丢弃 `JSWorker` 的工程（例如每建一个场景就起几个 worker、退出时 `terminate()`
然后丢掉引用）会随机让整个引擎进程崩掉：日志末尾是
`ERROR: FATAL: Condition "!(!obj.IsEmpty())"` 加 `handle_crash: Program crashed with signal 4`，
跑测试时表现为进程没了但一条用例都没红。

原因：主线程那边代表 worker 的 JS 对象在登记表里存的是弱引用。V8 回收它的时候，第一段回收
回调会立刻把引用清空，而删除登记表项要等第二段回调——第二段什么时候跑由 V8 决定（头文件原话
是 "some time after all the initial callbacks are processed"）。这中间存在一个必然的空档：
登记表项还在、JS 对象已经没了。此期间 worker 发来的消息（包括 worker 启动完成那条 ready）
会撞上这个空档，而代码在这里用的是 `jsb_check`，失败就是直接终止进程，没有任何挽回余地。

`Environment::_on_worker_message` 与 `Environment::_on_worker_transfer` 两处都改成：查到了登记
表项但 JS 对象已回收 → 放锁、丢掉这条消息、记一条 Debug 日志后返回。定成 Debug 而不是 Error，
是因为这条状态属于正常结果而不是用法错误：丢掉 `JSWorker` 引用后 V8 随时可能回收它，而引擎本来
也会在对象被回收时替它 `terminate()` 掉 worker（只是时机不确定）。同一条路径上顺手补了一处：
`_on_worker_transfer` 原来在 `has_object()` 判过之后又重新取一次句柄，中间隔着会分配内存的
transfer 绑定，句柄可能已经取不到，取不到时也会当成"没有收件人"处理。
