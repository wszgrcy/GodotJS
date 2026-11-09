# @godot-js/editor

## 1.4.0

### Minor Changes

-   93f837c: 新增原生 ESM 模块解析器（`jsb_native_esm`，仅 V8，默认关闭）

    打开开关后，`.mjs` 文件不再被包成 CommonJS 函数执行，而是交给 V8 自己的模块机制编译，
    `import` / `export` 按标准语义工作：

    -   默认导出、具名导出、`export * from`、命名空间导入
    -   循环依赖（依赖只编译不执行，求值统一放在最外层，不会读到半初始化的模块）
    -   顶层 `await`：能在同步里算完的（`await` 一个已就绪的 Promise）正常拿到值；
        等的是真实异步操作时直接报错，并用 V8 的信息指出卡在哪个文件，不会返回半成品导出
    -   `import()` 动态导入，返回真正的 Promise
    -   `import.meta.url` 与 `import.meta.resolve()`（值是引擎路径 `res://...`，不是 `file://` URL）
    -   CommonJS 里 `require("./x.mjs")` 拿到的是该模块的命名空间
    -   `.mjs` 默认导出一个继承引擎类型的类时，照样登记成 Godot 脚本类

    从 `.mjs` 里 import 非 ESM 的东西（`.js` / `.cjs` / `.json` / `godot`）时，会自动生成一个
    转发模块：默认导出是它的 `module.exports` / 解析后的 JSON / 引擎命名空间，同时按属性给出
    具名导出。属性名不是合法标识符的（`"some-name"`、`"0"`）走 ESM 的字符串导出名，不会丢属性。

    ```js
    import cjs, { from_cjs } from "./legacy.js"; // 默认导出 = module.exports
    import data from "./data.json";
    import gd from "godot";
    const { Node, Vector2 } = gd; // 引擎类要这样拿，见下面的限制
    ```

    失败处理对齐 Node：加载失败的模块不会留在"已加载"状态。同一个模块再 `require` 一次，
    会把 V8 缓存在模块上的原始异常重新抛出去（Node 的 `ModuleWrap::runSync` 就是这么做的），
    不会出现"第二次加载成功、拿到一个空模块"。顶层 await 卡住的模块同理，且 V8 不允许对
    求值中的模块重复求值，所以它被标记为不可用。

    其余扩展名（`.js` / `.cjs` / `.json`）仍走原来的 CommonJS 路径，行为不变。

    已知限制：

    -   加载失败过的模块（语法错、缺依赖、求值抛错、顶层 await 卡住）会一直失败，直到模块缓存重建；
        改了文件要等上下文重建才能重新试（Node 的 ESM 缓存也是这个行为）
    -   说明符可以省略扩展名（`import "./hello"` 能解析到 `hello.mjs`），比 Node 的 ESM 宽松
    -   **`godot` 模块只能默认导入**。它是个按需解析的 Proxy，列不出自有属性，所以
        `import { Node } from "godot"` 会在链接阶段报"does not provide an export named 'Node'"。
        写成 `import gd from "godot"` 再 `gd.Node` 或解构即可。Node 对无法静态分析导出的模块
        也是同样行为。打包成 CommonJS 的 TS 代码不受影响（具名 import 会变成属性访问）。
    -   转发模块导出目标对象的所有自有属性，包括不可枚举的；Node 只导可枚举的。
        `module.exports.default` 不会单独导出（`default` 已被默认导出占用），但 `cjs.default` 能取到。
    -   只有 V8 实现有，QuickJS / JavaScriptCore 没有对应实现
    -   所有入口都是同步的，所以真正需要异步等待的顶层 await 用不了（Node 的入口是异步的）
    -   `import.meta` 只填 `url` 和 `resolve`，没有 `main` / `filename` / `dirname`
    -   转发模块是生成一段 ESM 源码再编译（Node 用 V8 的 SyntheticModule，不生成源码），
        每个非 ESM 依赖多一次编译

    编译开关：`scons ... jsb_native_esm=yes`，实现见 `bridge/jsb_native_esm_resolver.{h,cpp}`，
    用例见 `tests/test_jsb_esm_runtime.h` 与 `tests/project/tests/esm/`。

-   2207357: 发布包打开原生 ESM（构建参数 `jsb_native_esm=yes`）

    之前这个能力只在源码自编译、且手动传了开关时才有，发布包里没编进去，
    `.mjs` 在发布包里仍然只能按 CommonJS 执行。现在 Linux / Windows / macOS /
    Android / iOS 的构建统一带上该开关，发布包里的 `.mjs` 直接走 V8 的模块机制。

    开关本身没动，默认仍是关闭：自己编引擎的人不传参数就保持原有行为。
    web 平台用的是浏览器自己的 JS 引擎，没有 V8，这个开关对它不起作用，所以没加。

### Patch Changes

-   28614e5: 修复 JS 定时器在高帧率下静默停摆，以及启动阶段注册的定时器提前触发

    引擎每帧把「这一帧过了多久」交给 JS 侧的定时器轮，原来这个量是 `(本帧时间戳 - 上帧时间戳) / 1000`
    的整数毫秒，余数直接丢掉。一帧的墙上时间不足 1ms 时（无头轻场景，或加了 `--fixed-fps` 关掉帧率
    节流之后），每帧交出去的增量恒为 0，定时器轮再也推进不了，所有 `setTimeout` / `setInterval` /
    `setImmediate`，以及建立在它们之上的 `await`、RxJS `asyncScheduler` 等，会**不报错地永久停摆**。

    现在帧间隔按微秒累加，攒满 1ms 才交给定时器轮，余数不再丢。

    顺带修掉另一处：`last_ticks_` 初值是 0，第一帧算出的增量等于「引擎从启动到现在的总时长」，
    这一帧会把定时器轮猛推一大截，启动阶段注册的 `setTimeout(fn, 50)` 会在第一帧被当作早已过期而
    当场触发。现在第一帧交 0 增量，从第二帧起交真实帧间隔（宁可晚一帧，不提前）。

    回归用例在 `tests/project/tests/timer/`，由 `pnpm test`（`run-runtime-matrix.mts` 里标了
    `timer clock` 的那一步）在跑每个宿主运行时时单独起一个进程带跑。它守住的是首帧猛推
    那一条；亚毫秒截断要一帧真实耗时低于 1ms 才暴露，本用例不覆盖。

-   4360417: 修复主线程卡顿之后注册的定时器提前触发

    JS 定时器轮不读时钟，只累加每次推进进来的毫秒数。主线程被占住期间不推进，而定时器到期时间
    按 `delay + 当前时钟` 计算，于是卡顿之后注册的 `setTimeout(fn, 5000)`，欠下的那段会在下一次
    推进时计入已等待时长，定时器提前触发，最坏情况当场触发（看门狗只过 2 秒就报超时 5 秒）。

    定时器时钟改为自行读取 `OS::get_ticks_usec()`，调用方只决定推进时机，不再传入帧间隔；注册
    定时器时把已流逝而未入钟的时长补进首次延时。`Environment::update()` 不再接收增量参数，
    Worker 侧同步调整。附带修正主环境喂入上一帧时长导致的定时器天然晚一帧触发。

    回归用例在 `tests/project/tests/timer/`，覆盖卡顿后注册 `setTimeout`、长延时定时器、
    `setInterval` 首次延时与回调内卡顿后再注册。未打补丁判负（卡 1.2 秒后 `setTimeout(500)`
    实际 0ms 触发），打过判过。

-   05520c9: 脚本类产物允许是 `.mjs`：`.ts` 源路径不再硬拼成 `.js`

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

## 1.3.2

### Patch Changes

-   让实例上的 `name` 属性真正读写引擎节点名。

    此前 `name` 被整体排除在属性绑定之外，`node.name = "X"` 只会改到 JS 包装对象上，
    引擎名不变且不报错，靠名字解析的东西（`get_node("X")`、动画轨道路径）会静默失效。

    需要三处配合修改：

    -   `internal/jsb_string_names.cpp`：去掉对 `name` 的整体排除。
    -   `bridge/jsb_object_bindings.cpp`：给 `name` 豁免 getter/setter 方法省略，
        否则绑上属性就会丢掉 `get_name()` / `set_name()`。
    -   `bridge/jsb_editor_utility_funcs.cpp`：typings 导出做同样豁免，
        否则运行时方法还在但 typings 说没有，`tsc` 编不过。

    改完 `.name` 与 `get_name()` / `set_name()` 两边都可用，与 GDScript 一致；
    JS 类与函数自带的 `name` 不受影响（Godot 属性装在原型上，与构造器不同对象）。

    新增 `tests/project/tests/naming/` 覆盖上述行为。

-   web 运行时补上引擎类构造器的 `name`。

    V8 / QuickJS / JSC 都把类名交给了引擎 API，`Node3D.name === "Node3D"` 成立；
    web 那套的构造器是 JS 侧写的匿名函数，类名只用于日志，于是 `name` 落在变量名上，
    构建压缩后所有引擎类都叫同一个短名（实测 `f`）。现在 `NewClass` 把类名写回 `name`，
    属性形状与 JS 函数自带的一致（不可写、不可枚举、可配置），四个运行时表现一致。

## 1.3.0

### Minor Changes

-   154398f: feat: JSWorker for all runtimes (not just V8) + Node transfers
-   764d32a: Upgrade to Godot 4.7.2

    -   adapt to `ClassDB::ClassInfo` moving a class' constants, enums and signals into `GDType`.
        The new accessors include inherited members unless `p_no_inheritance` is passed, so they are
        called with `true` to keep reporting only what a class declares itself, which is what 4.6
        reported. Leaving the default made the generated typings repeat every inherited enum and
        integer constant in each subclass, and those copies lost their documentation comments.
    -   `Script::instance_has()` is no longer virtual in 4.7 (deprecated in favour of
        `Object::get_script()`), so the `GodotJSScript` override is only compiled for 4.6.
    -   `memnew()` on a `RefCounted` now returns `Ref<T>` instead of `T*`.
    -   `AnimationMixer::get_animation_library_list()` and `AnimationLibrary::get_animation_list()`
        now fill a `LocalVector<StringName>` instead of a `List<StringName>`.
    -   `HashSet` copy construction is now explicit.
    -   include `core/object/callable_mp.h` explicitly, it is no longer pulled in transitively.
    -   keep 4.6 support through `compat/jsb_engine_compat.h`.

-   14e8cba: GodotJS 1.1
-   ffbade0: Node rpc and rpc_id/rpcId methods are now type safe thanks to additional codegen for RPCs.
-   83f086e: feat: Resolve nested node_modules (PNPM without hoisting)
-   964d80b: Godot 4.6 support and initial 4.7 dev support
-   5e5214e: feat: Debug on mobile, wait for debugger and LWS/V8 auto-download

    -   There's now Project Settings -> GodotJS -> Runtime -> Wait for Debugger (currently V8 only). This will pause execution before any JavaScript user code runs. This allows you to much more easily debug app start-up.

    -   There is now support for debugging on mobile devices. Unlike Editor builds on your Desktop, mobile builds don't have source distributed with them. Instead debugging works in tandem with an Editor build, which provides the source files to the debugger. Configure the host that provides source code with the new Project Settings -> GodotJS -> Runtime -> Source Map Base URL option. Your debugger (NOT the mobile device) must be able to reach this this URL. By default, that means you'll want to use: http://127.0.0.1:9300 Additionally, you need to ensure your TypeScript build config has sourceMaps enabled. The debugger can then fetch source files over HTTP. It is however, worth noting fetching source files can be pretty slow, particularly if you have a lot of them. Local debugging of desktop editor builds is generally much simpler.

    -   GodotJS Development: libwebsockets (LWS) was previously included as a binary in the repo. It (and V8) are now automatically downloaded from GodotJS' GodotJS-Dependencies release on Github. Whilst not 100% foolproof, this does help validate the integrity of binaries included in builds.

### Patch Changes

-   e28d324: **Types:** Statically resolved return type for `get_node()`/`getNode()`.
-   e28d324: **Types:** `ResourceLoader.load()` overload type.
-   2073ac9: **Fix:** `@ExportObject(Node)` was only working for sub-classes, not `Node` itself.
-   5171c3a: **Fix:** Duplicate "Generating" label appeared in the UI during codegen.
-   0514427: **Types:** `GDictionary` absent values are now correctly typed as returning as `null`, not `undefined`.
-   e28d324: **Feature:** Improved type conversion error messages.
-   e28d324: **Fix:** `@export_` support for enums as keys of `GDictionary`.
-   8979b46: **Types/Fix:** Invalid codegen for certain types (function literals in particular)
-   e28d324: **Fix:** Background thread script instantiation (for 4.5 editor).
-   e28d324: **Fix:** Handle `hint_string` if `@export_var` is a `GArray` with an element type provided via `details.class_`.
-   16450df: **Types:** Added `Resource` `duplicate()` return type.
-   e28d324: **Feature:** Improved user project type configurability.

    -   Codegen for scenes and resources now default to being stored in
        `gen/types` rather than `typings/`. However, a setting has been
        introduced to configure this. `typings/` is no longer used
        because the directory is configured as a type root, which means
        directories contained within are expected to be module
        definitions.
    -   `"types": ["node"]` in the default tsconfig was hiding type errors.
        This is no longer set. Essentially, this setting was disabling
        all types except node types from our type roots.
    -   `@types/node` removed from the default `package.json`. It was
        misleading and made it easy to accidentally use non-existent
        functionality. We no longer need these types because...
    -   Our JS essentials (console, timeout and intervals APIs) are now
        included in our `godot.minimal.d.ts`.
    -   No longer including `<reference no-default-lib=true/>` in our TS
        files. This seemed to be interfering with user tsconfig options,
        and it was not required. Not that it's been removed you're able to
        more freely make changes to your tsconfig. For example, you may set
        `libs` (or `target`) to make use of newer JS APIs.

-   e28d324: **Feature:** Ergonomics overhaul. Camel-case, TS types, codegen + more.

    -   There's now a project setting which can be toggled to swap to a
        more idiomatic JS naming scheme for Godot bindings. We use
        camel and pascal case to more closely align with typical
        JavaScript/TypeScript conventions. For `@Decorators` we've gone
        with pascal case, which is used in libraries like Angular. Camel
        case is perhaps more popular, but pascal case allows us to avoid
        reserved names, and thus we can cleanly write `@Export`, instead of
        needing to include the trailing underscore on `@export_`.

    -   TypeScript types have been improved. Particularly `Signal<>` and
        `Callable<>`. `Signal1`, `Signal2`, etc. are now deprecated, as are
        `AnySignal` and `AnyCallable`, since the new `Signal<T>` and `Callable<T>`
        types handle an arbitrary number of parameters. Importantly,
        `Callable.bind(...)` is now accurately typed, so you'll receive
        type errors when connecting to signals.

    -   `GArray` and `GDictionary` now have a static `.create<T>()` method
        which allows you to create nested data structures from literals
        and benefit from full type checking. When a `GArray` or `GDictionary`
        is expected as a property, a `.proxy()` can be provided in its
        place.

    -   Partially worked around https://github.com/microsoft/TypeScript/issues/43826
        whereby our proxied `GArray` and `GDictionary` always return proxied
        nested values, but will accept non-proxied values when mutating
        a property. Basically, there's now `GArrayReadProxy` and
        `GDictionaryReadProxy`. These aren't runtime types, they're just
        TS types that make it easier to work with proxies. Under normal
        circumstances, you likely won't need to know these types exist.

    -   Codegen leveled up. Any TS module can now export a function
        named `codegen` with the type `CodeGenHandler`. This function
        will be called during codegen to allow you to optionally
        augment type generation involving user-defined types. Consider,
        for example, the `SceneNodes` codegen which previously only knew
        how to handle Godot/native types in the scene hierarchy. When
        a user type was encountered, it'd write the native type, which
        is still useful, but it'd be nice to be able to include user
        types. The reason we don't by default is user types are not
        required to follow our generic parameter convention where
        each node is passed a `Map` argument.

        Let's see an example:

        ```ts
          export default class CardCollection<Card extends CardNode = CardNode> extends GameNode<SceneNodes['scenes/card_collection_3d.tscn']>
        ```

        the type above does not take a `Map`. Perhaps more interesting, it
        takes a different generic parameter, a `CardNode`. If we encounter
        a `CardCollection` script attached to a node in the scene somewhere,
        GodotJS' internal codegen can't possibly know what that generic
        parameter ought to be. So we can help it out. In the same file
        where `CardCollection` is defined, we could provide a `codegen`
        handler like so:

        ```ts
        export const codegen: CodeGenHandler = (rawRequest) => {
            const request = rawRequest.proxy();

            switch (request.type) {
                case CodeGenType.ScriptNodeTypeDescriptor: {
                    const cardNodeScript = request.node.get("cardNodeScript");
                    return GDictionary.create<UserTypeDescriptor>({
                        type: DescriptorType.User,
                        name: "CardCollection",
                        resource: "res://src/card-collection.ts",
                        arguments: GArray.create<TypeDescriptor>([
                            GDictionary.create<UserTypeDescriptor>({
                                type: DescriptorType.User,
                                name: cardNodeScript?.getGlobalName() ?? "CardNode",
                                resource: cardNodeScript?.resourcePath ?? "res://src/card-node.ts",
                            }),
                        ]),
                    });
                }
            }

            return undefined;
        };
        ```

        Above we handle the codegen request to determine the node type
        of the provided `request.node`. What's _really_ neat here is we
        don't need to hard-code that generic. We've instead exported a
        configurable `Script` reference for use in the editor:

        ```ts
          @ExportObject(Script)
          cardNodeScript: Script = ResourceLoader.load('res://src/card-node.ts') as Script;
        ```

        So the codegen logic simply grabs the type exported from the
        chosen script, and provides it as a generic argument to
        `CardCollection<>`.

        One thing worth noting, your class does NOT need to be a `@Tool`.
        In the above example, `CardCollection<T>` is not a `@Tool`, and
        hence the node script is not instantiated during codegen, which
        is why we've used `request.node.get('cardNodeScript')` rather
        than trying to access the property directly. That said, if you
        want, codegen can be combined with `@Tool`.

    -   There's also a bunch of logging/error reporting improvements.

-   5c5943e: **Feature:** Bumped default tsconfig target from es2016 to es2022

    All supported runtimes ought to support the 2022 standard. You can
    still manually change the target if desired. This change is to
    provide a better user experience by default.

-   64f5ee8: **Feature:** Godot String methods now exposed as static methods on `String`.

    This doesn't change anything about how strings are used within
    JavaScript i.e. we're still using JS native string type, not
    Godot's String. However, Godot's String class has many utility
    functions, some of them static and some of them as instance
    methods. These are now all available for consumption in JavaScript.
    Godot String instance methods are mapped to static methods that
    take a `target: string` as their first parameter.

    In general, if there's an equivalent native JS string method, you
    should always use it instead since it will be much more performant.

-   d87eea1: **Feature:** Constructor params support and GDScript compatible `new`

    Support for `new` is very important for improving interop with
    other scripting languages, allowing them to instantiate objects
    from a Script reference. Crucially, this allows native
    GDExtension to instantiate Nodes/Objects/Resources implemented
    in GodotJS.

    Object construction has been refactored. Previously we had three
    cases to handle:

    1. new SomeJSWrapperAroundAGodotObject() – JS construction
    2. CDO (Class default object) construction - used to determine
       default parameters on a class (for use in the editor).
    3. Cross binding. Which is when a Godot Object is constructed and
       our script latter needs to attach to it.

    In the past, case 1 was the only situation in which instantiating
    a JS class ought to also instantiate the Godot native object.
    However, in my previous commit I changed CDOs so also instantiate
    the underlying native object. So case 2 was eliminated.

    Case 3 (cross binding) is a common situation, it was previously
    implemented in a somewhat intrusive fashion. All JS objects were
    being constructed and passed a parameter as their first argument.
    This indicated whether the object was cross binding (or a CDO).
    This prevented users from (easily) implementing constructors, the
    user had to know about the internal parameter and pass this
    up through to super(). For the most part I imagine users (myself
    included) simply avoided using constructors. However, this
    complicated some code that would otherwise be trivial in GDScript
    or C# because the latter has constructors and the former \_init.

    Consequently, I've implemented a new strategy to determine whether
    a constructor is being called from C++ (cross binding) or from JS.
    The implementation is quite straight forward, but arriving at this
    solution was not necessarily obvious. V8 doesn't expose APIs to
    intercept construction, and we can't naively use shared flags
    to mark a native constructor in progress because the solution
    needs to support reentrancy since during construction an object
    may instantiate other objects. Additionally, we can't just use
    different constructors (V8 templates) because there's issues with
    both sub-classing and `instanceof` detection.

    The solution was to take advantage of JS' `Reflect.construct` API.
    This allows us to call a constructor but have `new.target` set to
    an arbitrary constructable. `new.target` survives whilst
    traversing up through super() constructors (similar to how we
    previously passed arguments up). This let's us mark a particular
    instantiation as coming from C++. `Reflect.construct` instantiates
    `this` to match the prototype chain from the provided `newTarget`
    so we simply set it to the prototype of the original constructor
    and we're on our way.

    Using `Reflect.construct` is also standard JS, so it's available
    on our support JS runtimes without needing to resort to any
    runtime-specific code.

-   72e0bf6: **Types:** Improved GObject types
-   d5c1dfa: **Fix:** Bind script instance during default prop evaluation.

    If properties are implemented in JavaScript as properties (getters)
    they'll often want to call Godot methods on self. Without the
    script instance being bound this led to a crash.

-   29d9a48: fix: QuickJS number -> int32 conversion
-   e28d324: **Feature:** Introduced a setting to control whether `.d.ts` for scenes are auto-generated on when scenes are saved in the Editor.
-   f554365: **Feature:** Expose Godot Variant utility method typeof() as `godot_typeof()`/`godotTypeof()`.
-   460737c: **Fix:** Threading / Godot address reuse crash.
-   e28d324: **Types:** Codegen + type checking for animations.
-   98daa2a: fix: Codegen for global constants
-   e28d324: **Feature:** `CameraFeed` types.
-   abce9e2: **Types/Fix:** Codegen now types more `GArray`/`GDictionary` generic params based on exported variable hint strings.

    Additionally, fixed some codegen for enums when camel-case bindings are enabled.

-   e28d324: **Feature:** Support async module loader in quickjs.impl.
-   ea214c4: fix: null handling in GArray/GDictionary create() helpers
-   cffe0d8: **Feature:** Use TypeScript 5.9.2 (latest) by default
-   b1d8b3f: **Fix:** Enter the V8 isolate and context scopes before editor codegen compile to stop a SIGSEGV on batch reimport.
-   a883172: **Feature:** Godot 4.5 support
-   5bc325e: **Types:** "Generate Godot d.ts" in the UI is now "Generate types" and in addition to
    generating all Godot and project types (which it already did), the
    autogen directory will now be wiped of all files/directories before
    commencing generation. This ensures old generated files no longer
    pollute the project.
-   6e96120: **Fix:** Duplicate PackedByteArray to_array_buffer() registration
-   4bb388b: **Fix:** Added missing `PROPERTY_USAGE_SCRIPT_VARIABLE` flag on exported variables.
-   94ac86d: **Types:** `NodePathMap` now permits `undefined`/optional children.
-   e28d324: **Feature/Types:** Types + codegen for project input actions.
-   555acc6: **Types:** `EditorUndoRedoManager` API types
-   c82dfac: **Types**: Ensure the GAny union type includes null
-   1341389: **Fix:** Critical bug fixes (for crashes) that may occur due to the
    current isolate not being set.
-   5721fe7: Default tsconfig to use node16 for improved CJS/ESM interop
-   e28d324: **Feature:** `GDictionary`/`GArray` recursive handling and `toJSON()`/`toString()`

    `GArrayProxy` (now a named type) will now JSON encode like a regular
    JS array i.e. `[1,2,3]` instead of `{"0": 1, "1": 2, "2": 3}`.

    We previously ensured that values accessed via a proxy were
    themselves proxied. Thus allowing access to nested properties
    via chained access e.g. dict_proxy.a.b. However, when setting or
    inserting a proxy-wrapped value, we previously inserted the proxy
    itself, rather than the wrapped target. This has now been rectified,
    it's not safe to do something like:

    ```ts
    const a = new GDictionary().proxy();
    a.b = new Dictionary().proxy();
    ```

    The above will result in a `GDictionary` containing a `b`
    property that is an empty `GDictionary`.

-   9ebe4f6: **Fix:** TStringNameCache v8::String reference loss

    https://github.com/godotjs/GodotJS/issues/110

-   15d1e25: fix: issue with `node_module` resolving
-   dcb33db: fix: Non-V8 Godot object construction
-   ebc0414: Fixed ObjectDB object removal race/deadlock/crash
-   b207444: **Feature:** Native object now guaranteed bound during JS initializaation.

    Previously, when a JS class was being instantiated for an existing
    native Godot object (e.g. ResourceLoader.load('...')) the JS
    instance wasn't actually being bound to the native Godot object
    until _after_ the JS instance was constructed/initialized. This
    created a problem in which field/property initializers and the
    constructor were unable to call any native functions, but could
    call JS functions.

    Now, the Godot object is always bound to our instantiated object
    as part of our Godot native class constructor. Thus, we can now
    safely call methods during initialization. Importantly, this also
    fixes TC39 Stage 3 decorator `@bind.signal()` support for fields.

-   64dbebc: fix: Background/async resource loading where scripts are present.

    Shadow script instance replacement was failing due to calling the
    wrong GodotJSScript::instance_create() overload. The overloads
    have been replaced with separate functions to mitigate future
    occurrences.

    Additionally, shadow script instance replacement was not properly
    handling user types, only built-ins. This broke in a subtle hard
    to debug fashion, whereby you received back a valid JS class
    instance for the expected Godot object, however its prototype
    chain was incomplete. The JS object was an instance of the base
    Godot native object rather than the script on the object.

-   1d85819: **Fix:** `JSWorker` transfer crash.
-   e28d324: **Feature:** Async module loader.
-   e0f44db: **Types:** Improved generated typings formatting.
-   8249045: fix: [web] Fixed issue with `null` incorrectly being treated as an object by GodotJS.
-   70e28af: **Fix:** Ensure usercode triggered from `get('prop')`/`set('prop')` does not crash the process, JS runtime errors are caught and logged.
-   e28d324: **Fix:** `godot.lib.api` `instanceof` checks against proxied classes.
-   e94bbf0: **Fix:** Don't crash when a GodotJS class implementation's parent class does not match the attached node.

    Instead, we log an error and prevent the script from being instantiated.

-   5f96ffc: **Fix:** Enum codegen (and godot.lib.api iteration).
-   e28d324: **Performance:** Added LRU support for `StringNameCache`.
-   e28d324: **Types**: Refined `SceneTree` `GArray` return types.
-   937c6f6: **Fix:** Don't leak temporary default properties object
-   21dffd1: fix: Array counts props incorrectly typed as any
-   e28d324: **Feature:** Introduced settings to ensure dependencies are included in exported builds.

    -   "Referenced Node Modules" can be enabled to package an _entire_ node module when any file belonging to that module is referenced.
    -   "Include directories" is another setting that allows you to explicitly add additional directories you want included in your exported builds.

-   e28d324: **Feature:** Godot 4.4 support.
-   102806b: fix: Ensure setTimeout correctly handles negative delays
-   88c3060: fix: Improved node_modules handling during game exports.
-   e28d324: **Fix:** Improved TypeScript class matching regex.

    It was failing in the presence of generics which contain an `extends`
    clause e.g.

    ```ts
    export default class GameNode<Map extends Record<string, Node> = {}> extends Node3D<Map>
    ```

    The regex will now look for the last `extends` on the line in order
    to detect the base class. This is only an improvement, it's not
    fool-proof and will fail if the base class has a generic that
    contains a conditional type expression. Since we only have access
    to PCRE2, this is probably the best we can do with just regex.

-   d70a467: **Fix:** Another missing `Isolate::Scope` that could lead to a runtime crash.
-   17f7731: fix: .ts/.js files not included in game export

    Exporting GodotJS games was fundamentally broken by
    a Godot engine change which altered the way .skip()
    behaved on exported files. Basically, we were skipping
    .ts files because we don't want them in the export,
    only .js files which we added as extra files. However,
    the change to skip's behavior meant extra files are
    also skipped. Instead .ts files are now remapped to
    the .js file, not skipped. The result is as
    previously intended i.e. only .js files ship.

-   e28d324: **Fix/Types:** Ensure that codegen quotes property keys when necessary.
-   e28d324: **Feature:** Support for importing `.json` files.
-   a4bafef: **Feature:** New decorator syntax for exporting properties.

    TC39 (JavaScript standard body) have progressed a new decorator
    syntax to Stage 3, and TypeScript 5.0 implemented support. The new
    syntax is not quite as flexible but is type-safe and will be
    more performant when implemented directly in JS engines.
    Consequently, all existing decorators have deprecated (but
    remain for backwards compatibility). In their place a new API,
    `createClassBinder`, has been introduced.

    Our default `tsconfig.json` has been updated to turn off legacy
    decorators, unlocking access to the newer syntax. You cannot use
    both at once. Both sets of decorators contain warnings if used
    with in incompatible tsconfig.

    The new decorator syntax looks something like:

    ```
    const bind = createClassBinder();

    @bind()
    @bind.tool()
    export default class Player extends CharacterBody2D {
      @bind.export.cache()
      @bind.export.object(SceneSynchronizer)
      accessor synchronizer!: SceneSynchronizer;

      @bind.export.cache()
      @bind.export(Variant.Type.TypeInt)
      accessor walkSpeed: number = 350;

      @bind.export.cache()
      @bind.export(Variant.Type.TypeInt)
      accessor dashSpeed: number = 1000;

      @bind.export(Variant.Type.TypeInt)
      accessor dashCooldownMs: number = 500;

      @bind.export.enum(Direction)
      accessor facing = Direction.Down;

      @bind.export(Variant.Type.TypeInt)
      accessor useCooldownMs: number = 500;
    }
    ```

    A few key points:

    1. You must create a ClassBinder using createClassBinder().
       This is a function and contains properties/APIs on it.
       You can use any variable name, but the convention I'll be
       using going forward is to use a variable named `bind`.
    2. The decorator APIs are all functions that return a decorator
       i.e. It's `@bind()` and `@bind.export.cache()` not `@bind`
       or `@bind.export.cache`.
    3. We're using JavaScript's new/upcoming auto-accessor syntax.
       This is not a requirement for all decorators, but not all
       decorators (e.g., the new cache() decorator) are supported
       on fields.
    4. The new cache decorator enables caching of variants on the
       Godot side of the JS <-> Godot bridge.
       The decorator generates a `set` accessor that updates the cache
       automatically whenever a value is assigned to the JS property.
       The purpose of the cache is that is provides a fairly sizeable
       performance improvement when using Godot's general purpose
       .get("property_name") method. This is particularly useful if
       you want to expose data to performance sensitive GDExtensions.
    5. The order of decorators matters!
       Decorators are evaluated "inside->out" and class decorators are
       evaluated after all property decorators. `bind()` MUST be
       executed AFTER all other decorators. The `@bind.export.cache()`
       decorator MUST be evaluated AFTER the property export.

-   e28d324: **Fix:** Always use thread-safe variant pool allocator.
-   01895d4: **Types:** Added types for `Node` direct child APIs:

    -   add_child
    -   get_child
    -   get_children
    -   move_child
    -   remove_child

-   e28d324: **Feature:** Module resolution now supports modules that utilize `exports` in their `package.json`.
-   e28d324: **Fix:** Do not instantiate `GodotJSMonitor` if the JS runtime does not support `Performance`.
-   6399703: **Fix/Types:** Don't allow codegen failures to output malformed types.
-   e28d324: **Feature:** Added settings to optionally generate `.d.ts` files for Godot scene files in the Editor.
-   8998cef: `--generate-types` now follows the engine locale for doc comments

        `DocData` always holds the raw English text from `doc/classes/*.xml`; the editor only
        translates on display, through `DTR()`. The type generator read the raw text, so generated
        `.d.ts` files came out English whatever locale was passed.

        Wrapping the doc text fields in `DTR()` makes `godot --headless --editor --generate-types

    -l zh_Hans` emit the official Simplified Chinese class reference, i.e. the same strings the
editor Help panel shows. This works for the locales the engine ships doc translations for
(`zh_Hans`, `zh_Hant`, `fr`, `es`, `ru`, `ko`, ...). The locale has to match the doc catalog
exactly: `zh_CN` is not one of them and silently falls back to English.

        With the default locale the text stays English, since `DTR()` returns the source string when
        no doc translation is loaded.

-   e28d324: fix: exported properties no longer leak into the base class.
    feat: inherited properties now class categorized in the Editor.

    Previously sub-classes were reusing the same [[ClassProperties]]
    and [[ClassSignals]] as super classes. Thus sub-classes were
    exporting properties against super-classes. This also meant that
    classes with a shared parent were receiving each others'
    properties. Each class no longer looks up the prototype chain
    for these objects.

    Consequently, to ensure properties are exported and appear in the
    Editor, we now recurse in a similar fashion to GDScript.
    Fortunately, we don't need to worry about the cycle detection
    logic that GDScript implements, since TypeScript handles this for
    us and cycles won't compile. Added benefit is now that properties
    appear in the editor categorized appropriately by class.

-   4d24683: **Types:** GDictionary keys() return type
-   d24b974: **Types:** ResolveNodePathMap utility type.

    This type makes it easier to dynamically define your own `NodePathMap` types derived from generated SceneNodes.

    It's also useful for creating Node scripts that don't live at the top level of a scene e.g.

    ```ts
    export default class Table extends Node3D<ResolveNodePathMap<SceneNodes["scenes/example/table.tscn"], "Table">> {
        // ...
    }
    ```

    The above assumes a `Table` node exists as a child of the root in scenes/example/table.tscn. Now
    `this.get_node`/`this.getNode` will auto-complete (and provided types for) children of `Table`.

-   8e3bc27: **Fix:** Variant constructor failure on first bound Variant class.
-   3fa1679: fix: resolving `.mjs` file extension
-   e28d324: **Fix:** `@export_dictionary` decorator.
-   1675f15: **Fix:** Don't crash when script binding fails.
-   e28d324: **Fix:** NIL is now permitted as convertible to any other variant types.
-   be2c73c: **Types:** `UserTypeDescriptor` `resource` property is now type-safe and will auto-complete, accepting `Script` resources.
-   b96f13a: **Fix:** Ensure resource types are (re)generated when a scene saves
-   59abb7f: **Types:** Dynamic dispatch methods (e.g.,`call_deferred`) are now typesafe.

    Typing these methods was not nearly as straight-forward as I would
    have liked. There's also a small usage gotcha due to method
    parameters being bivariant in TypeScript. Even with strict function
    types enabled, contravariant parameter enforcement only applies
    when comparing the variance of non-method functions. This might be
    a bit theoretical, so I'll demonstrate with an example.

    ```ts
    class Sound {
        play() {
            console.log("Ba-ding!");
        }
    }

    class Moo extends Sound {
        moo() {
            console.log("Moo!");
        }
    }

    class Animal {
        vocalize(sound: Sound) {
            sound.play();
        }
    }

    class Cow extends Animal {
        override vocalize(moo: Moo) {
            moo.moo();
        }
    }

    function vocalize(animal: Animal, sound: Sound) {
        animal.vocalize(sound);
    }

    vocalize(new Cow(), new Sound());
    ```

    The above is perfectly valid in TypeScript, no type errors, but
    will crash at runtime. The issue is TypeScript allows us to
    override vocalize() and take a covariant (subtype) parameter. So,
    we proceed to call moo() on what the implementation believes is
    a Moo, but we end up receiving a Sound instead. This obviously
    isn't ideal, and the TypeScript language developers are well aware
    of the situation, but at present this behavior is required to
    support structural type checking on generics, and to handle some
    DOM type weirdness.

    Now if we add:

    ```ts
    function callLater<T, S extends keyof T>(
        delayMs: number,
        target: T,
        methodName: S,
        ...args: T[S] extends (...args: any[]) => any ? Parameters<T[S]> : never
    ) {
        setTimeout(() => (target[methodName] as (...args: any[]) => any)(...args), delayMs);
    }

    class Animal {
        vocalize(sound: Sound) {
            sound.play();
        }

        vocalizeLater(sound: Sound) {
            callLater(1000, this, "vocalize", sound); // Error on this line
        }
    }
    ```

    This gives the error:

    > Argument of type '[Sound]' is not assignable to parameter of type 'this["vocalize"] extends (...args: any[]) => any ? Parameters<this["vocalize"]> : never'.(2345)

    Basically, `this` is NOT the same as `Animal`, it's a polymorphic
    type. Due to the use of `this` the type checker is unable to
    validate that `[Sound]` is the correct parameter types tuple. This
    occurs BECAUSE the parameter types are bivariant, the parameter
    types can be (and in this example are) more restrictive than those
    declared in the `Animal` type.

    Now, this poses an interesting problem for Godot's call_deferred
    (and similar) APIs. Because it's quite common to want to do:

    ```ts
    this.callDeferred("remove_child", someChild);
    ```

    Which leads to a similar error with `removeChild` parameters not
    being known for the `this` type. The solution is to introduce a
    cast to the same type (or a parent type):

    ```ts
    (this as Node).callDeferred("remove_child", someChild);
    ```

    This works around the issue. Of course, this _technically_ isn't
    type-safe if a sub-class was to override `removeChild` similarly
    to our `vocalize` example above. The cast is basically telling
    the typechecker, "Go away. I know what I'm doing. Probably."

    Now that I've explained the usage gotcha, I'll touch on some
    technical details of the implementation.

    Using our example above, if you were to try pull `callLater` into
    the `Animal` class, drop the `T` generic parameter and replace its
    usages with the type `this`. When you try use the method you'll
    run into the dreaded:

    > Type instantiation is excessively deep and possibly infinite.

    This occurs because `callLater` attempts to handle parameters for
    all functions on the `Animal` class. But one of those functions
    is `callLater`. So the parameters for a call to callLater are
    potentially the parameters for another callLater... you see where
    this is going.

    The solution is basically to explicitly prevent recursion through
    `callLater`. Easy enough for this one example. But Godot has
    several dynamic dispatch methods, they all need to be excluded,
    not just the function itself, because you could chain calls back
    and forth between them.

    So, this is where the new 'godot' module interface comes in:

    ```ts
    /**
     * Godot has many APIs that are a form of dynamic dispatch, i.e., they take the name of a function or property and
     * then operate on the value matching the name. TypeScript is powerful enough to allow us to type these APIs.
     * However, since these APIs can be used to call each other, the type checker can get hung up trying to infinitely
     * recurse on these types. What follows is an interface with the built-in dynamic dispatch names. GodotJS' types
     * will not recurse through methods matching these names. If you want to build your own dynamic dispatch APIs, you
     * can use interface merging to insert additional method names.
     */
    interface GodotDynamicDispatchNames {
        call: "call";
        callv: "callv";
        call_deferred: "call_deferred";
        add_do_method: "add_do_method";
        add_undo_method: "add_undo_method";
    }
    ```

    An interface isn't actually the most obvious way to define the
    exclusions, a union would be simpler. However, if you were to add
    your own dynamic dispatch type method in a sub-class, GodotJS
    types will need to avoid recursing through it too. So you can use
    interface merging to add to the set.

    There's actually a bit more complexity than just calls to those
    methods. Because TS type checking is structural, without the above
    you hit up against infinite recursion simply by virtue of the
    methods existing on the type, even if you're not calling them.

-   e28d324: **Types:** Godot/GDScript best effort typing for nullability of Object types.

    Although they're technically nullable everywhere (Ref<> in C++),
    many APIs won't ever return to `null`. Unfortunately, properties do
    not have flags to tell us this, so we are making best effort guesses
    as follows:

    -   Getters/setters are nullable.
    -   Function return types are nullable, unless the function name
        starts with `"create"`.
    -   Function arguments are non-nullable.

    This seems to be a reasonable starting point, and we will continue to manually tweak
    types from here.

-   e28d324: **Fix:** Support for script instantiation from a ResourceLoader executing on another thread.
-   26590b4: fix: exported property hint_string regression w/ camel-case bindings

    This was causing enums, ranges etc. to display incorrectly in the Godot Editor.

-   a89e27c: fix: non-V8 increased value stack (hard limit) size
-   f341cda: **Fix/Types:** Ensure GDScript singleton class accessors/modifiers are `static`.

    Fixes https://github.com/godotjs/GodotJS/issues/99

-   e28d324: **Fix:** Adhere to typical JS semantics and use default arg when undefined is passed (not just when the parameter is omitted).
-   e28d324: **Feature:** Added `@export_object` shorthand decorator for exporting objects.
-   e28d324: **Feature:** Ensure string representation of Godot objects include the script class name.
-   eedd327: fix: setTimeout/setInterval invocation crash
-   3d66a57: **Types:** Improved support for statically typed optional nodes.
-   d7353ab: **Feature:** Worker postMessage support for structured clone and transfer.

    This enables bi-directional transfer of all Godot types between
    workers and the host environment. The structured clone algorithm
    uses referential equality of Variant rather than structural
    equality. This both gives us a performance boost and also ensures
    that if the same Variant is referred to in nested structures,
    upon deserialization, these relationships will remain intact.

    `JSWorkerParent.transfer()` is now deprecated since `postMessage` can
    achieve the same and more.

    As before, this is still a v8 only feature. I would say this makes
    workers somewhat LESS experimental, but not yet stable.

-   eabb12a: **Feature:** `GArray` tuple support.

    For example, with the type `GArray<[number, string]>` `.get(0)` will return a `number` and `.get(1)` will return a `string`.

-   ed2e47f: fix: non-V8 JS object -> Godot type conversion
-   e28d324: **Types:** Codegen — treat Signal args as output types, not input types.
-   47904ff: **Types:** `get_tree()`/`getTree()` no longer types as returning `null`.

    Although it _will_ return `null` when not inside a tree. This is a runtime error, with an error message
    being logged i.e., you should not ever call this function expecting a `null` result.

-   e28d324: **Fix:** TypeLoader `post_bind` now supports reentrancy.
-   e28d324: **Feature/Types:** Codegen/types for resources in your project i.e., `ResourceLoader.load("res://whatever")` is now strongly typed depending on the file path.
-   e28d324: **Feature:** API to access `GDictionary`/`GArray` like JS objects/arrays (`.proxy()`) and
    improved type definitions for `GDictionary` and `GArray`.
-   63dc143: **Fix:** Make Variant constants (e.g., `Vector3.ZERO`) Readonly<> so they can't (easily) be accidentally mutated.

## 1.0.0

### Major Changes

-   Implement a [_Monitor_](https://github.com/godotjs/GodotJS/wiki/Statistics) of the runtime statistics of a running project
-   Implement _console.time/console.timeEnd_
-   Refactor [_Worker_](https://github.com/godotjs/GodotJS/wiki/Worker) to support _Godot Object_ transfer

### Minor Changes

-   Add _worker.onready_ callback
-   Add threading support for _InstanceBindingCallback_
-   Improve multi-threading support for _IConsoleOutput_
-   Refactor _GodotJSScript_ to support loading in _Worker_
-   Add a setting entry for _Exporter_ to manually include source files
-   More strict type check for native object bindings
-   _TypeConvert_ returns _true_ and _nullptr_ for dead object pointers
-   Ignore _Thread_ class in d.ts codegen

### Patch Changes

-   Fix a godot profiler crash on _GodotJSScriptLanguage::profiling*get*_
-   Fix a possible crash in _Worker::finish()_
-   Fix an access violation issue in _Worker_

## 0.9.9

### Minor Changes

-   Add prebuilt libs of lws/v8 for Windows/Linux on ARM64
-   Add support for hooking godot typeloader
-   Add support for JavaScriptCore

### Patch Changes

-   Minor bugfix

## 0.9.8

### Patch Changes

-   Fix a crash issue in `jsb::Buffer` destructor when using _quickjs-ng_ as runtime
-   Support for pure JavaScript projects (still with typings)
-   Refactor annotations and helper functions in `jsb.core`

### Breaking Changes

-   All annotations are moved into `godot.annotations` module (from `jsb.core`).
-   `GLOBAL_GET` and `EDITOR_GET` are moved into `godot` module (from `jsb.core`).
-   `callable()` is removed from `jsb.core`, use `Callable.create` in `godot` module instead.
-   `to_array_buffer()` is removed from `jsb.core`, use `PackedByteArray.to_array_buffer()` instead.
-   `$wait` is removed from `jsb.core`, use `SignalN<...>.as_promise()` instead.

> [!NOTE]
> Regenerating `.d.ts` files in your project will help you a lot to fix most errors caused by the breaking changes below.
> _VSCode_ (or `tsc`) will report all relevant changes as errors.
> Old annotations still work temporarily in this version but will be removed in a future version. Please update your codes.
