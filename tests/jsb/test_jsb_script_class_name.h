#ifndef GODOTJS_TESTS_JSB_SCRIPT_CLASS_NAME_H
#define GODOTJS_TESTS_JSB_SCRIPT_CLASS_NAME_H

#include "jsb_test_helpers.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"

/**
 * `get_global_class_name` 是编辑器登记脚本类时调用的那个函数。
 * 它不执行脚本，只用正则从源码文本里抽「类名 + 基类」，所以用例只需要文件内容，
 * 不需要这些源码真能跑起来。
 *
 * 抽不出类名时编辑器会报 `Condition "p_class_doc.name.is_empty()" is true.`，
 * 并且这个脚本类不会进 global_script_class_cache.cfg（创建节点对话框、脚本类图标、
 * Help 文档里都找不到它）。所以这些写法都要盯住：
 *   .ts   / .mjs（原生 ESM）: export default class X extends Y
 *   .js   / .cjs（CommonJS）: exports.default = class X extends Y  或  分行写法
 *   打包器产物          : var X = class extends Y {}  +  export { X as default }
 */
namespace jsb::tests
{
    /** 用完自动删除的临时目录，脚本源码写在里面。 */
    class TempScriptDir
    {
        Ref<DirAccess> dir_;

    public:
        TempScriptDir()
        {
            Error err = OK;
            dir_ = DirAccess::create_temp("jsb-class-name", false, &err);
            REQUIRE_MESSAGE(err == OK, "cannot create temp directory");
            REQUIRE(dir_.is_valid());
        }

        /** 写入一个脚本文件，返回它的绝对路径。 */
        String write(const String& p_file_name, const String& p_source)
        {
            const String path = dir_->get_current_dir().path_join(p_file_name);
            const Ref<FileAccess> file_access = FileAccess::open(path, FileAccess::WRITE);
            REQUIRE_MESSAGE(file_access.is_valid(), vformat("cannot write %s", path));
            file_access->store_string(p_source);
            return path;
        }
    };

    struct ParsedClass
    {
        String name;
        String base_type;
#if GODOT_4_4_OR_NEWER
        bool is_tool = false;
#endif
    };

    /** 按编辑器扫描脚本时的调用方式走一遍。 */
    inline ParsedClass parse_class(const String& p_path)
    {
        GodotJSScriptLanguage* language = GodotJSScriptLanguage::get_singleton();
        REQUIRE(language != nullptr);

        ParsedClass parsed;
#if GODOT_4_4_OR_NEWER
        parsed.name = language->get_global_class_name(p_path, &parsed.base_type, nullptr, nullptr, &parsed.is_tool);
#else
        parsed.name = language->get_global_class_name(p_path, &parsed.base_type);
#endif
        return parsed;
    }

    TEST_CASE("[jsb] global class name: commonjs 单行形式")
    {
        TempScriptDir dir;
        const String path = dir.write("cjs_inline.js",
                "exports.default = class CjsInline extends Node {\n"
                "    _ready() {}\n"
                "};\n");

        const ParsedClass parsed = parse_class(path);
        CHECK(parsed.name == "CjsInline");
        CHECK(parsed.base_type == "Node");
    }

    TEST_CASE("[jsb] global class name: commonjs 分行形式")
    {
        TempScriptDir dir;
        const String path = dir.write("cjs_split.js",
                "class CjsSplit extends Node {\n"
                "    _ready() {}\n"
                "}\n"
                "exports.default = CjsSplit;\n");

        const ParsedClass parsed = parse_class(path);
        CHECK(parsed.name == "CjsSplit");
        CHECK(parsed.base_type == "Node");
    }

    TEST_CASE("[jsb] global class name: 原生 ESM 的 .mjs 脚本类")
    {
        // 这条就是修复点：原生 ESM 产物没有 exports.default，形状与 .ts 相同。
        TempScriptDir dir;
        const String path = dir.write("esm_default.mjs",
                "import gd from \"godot\";\n"
                "export default class EsmDefault extends gd.Node3D {\n"
                "    _ready() { console.log(\"ready\"); }\n"
                "}\n");

        const ParsedClass parsed = parse_class(path);
        CHECK(parsed.name == "EsmDefault");
        // 限定名只取最后一段，否则基类会被认成命名空间 gd
        CHECK(parsed.base_type == "Node3D");
    }

    TEST_CASE("[jsb] global class name: 原生 ESM 用具名导入的基类")
    {
        TempScriptDir dir;
        const String path = dir.write("esm_named.mjs",
                "import { Node } from \"godot\";\n"
                "export default class EsmNamed extends Node {\n"
                "}\n");

        const ParsedClass parsed = parse_class(path);
        CHECK(parsed.name == "EsmNamed");
        CHECK(parsed.base_type == "Node");
    }

    TEST_CASE("[jsb] global class name: 原生 ESM 形状的 .js 也认")
    {
        // package.json 里 type: module 时，ESM 产物的扩展名是 .js。
        TempScriptDir dir;
        const String path = dir.write("esm_as_js.js",
                "import { Node } from \"godot\";\n"
                "export default class EsmAsJs extends Node {\n"
                "}\n");

        const ParsedClass parsed = parse_class(path);
        CHECK(parsed.name == "EsmAsJs");
        CHECK(parsed.base_type == "Node");
    }

    TEST_CASE("[jsb] global class name: .ts 源文件保持原样")
    {
        TempScriptDir dir;
        const String path = dir.write("ts_source.ts",
                "import { Node } from \"godot\";\n"
                "export default class TsSource extends Node {\n"
                "    _ready(): void {}\n"
                "}\n");

        const ParsedClass parsed = parse_class(path);
        CHECK(parsed.name == "TsSource");
        CHECK(parsed.base_type == "Node");
    }

#if GODOT_4_4_OR_NEWER
    TEST_CASE("[jsb] global class name: .ts 的 @Tool() 标记")
    {
        TempScriptDir dir;
        const String path = dir.write("ts_tool.ts",
                "import { Node } from \"godot\";\n"
                "@Tool()\n"
                "export default class TsTool extends Node {\n"
                "}\n");

        const ParsedClass parsed = parse_class(path);
        CHECK(parsed.name == "TsTool");
        CHECK(parsed.is_tool == true);
    }
#endif

    TEST_CASE("[jsb] global class name: 不是脚本类的文件不能误判")
    {
        TempScriptDir dir;
        {
            const String path = dir.write("plain_function.mjs",
                    "import { Node } from \"godot\";\n"
                    "export function helper(node) { return node.get_name(); }\n");
            CHECK(parse_class(path).name.is_empty());
        }
        {
            const String path = dir.write("plain_cjs.js",
                    "const value = 1;\n"
                    "module.exports = { value };\n");
            CHECK(parse_class(path).name.is_empty());
        }
        {
            // 有 default 导出，但不是继承 Godot 类的 class
            const String path = dir.write("default_not_class.mjs",
                    "const config = { speed: 1 };\n"
                    "export default config;\n");
            CHECK(parse_class(path).name.is_empty());
        }
        {
            const String path = dir.write("empty.js", "");
            CHECK(parse_class(path).name.is_empty());
        }
    }

    TEST_CASE("[jsb] global class name: 打包器产出的匿名类默认导出")
    {
        // esbuild 产物的典型形状：类是匿名的，赋给变量后用 `export { 变量名 as default }` 导出。
        TempScriptDir dir;
        const String path = dir.write("bundled.mjs",
                "import \"res://dist/cuj-polyfill.mjs\";\n"
                "import __gd$0 from \"godot\";\n"
                "//#region src/node_3d.ts\n"
                "var CharacterBody3D = __gd$0.CharacterBody3D;\n"
                "var SPEED = 5;\n"
                "var CB = class extends CharacterBody3D {\n"
                "\tcamera;\n"
                "\t_ready() { console.log(\"hello\"); }\n"
                "\t_physics_process(delta) {}\n"
                "};\n"
                "//#endregion\n"
                "export { CB as default };\n");

        const ParsedClass parsed = parse_class(path);
        CHECK(parsed.name == "CB");
        CHECK(parsed.base_type == "CharacterBody3D");
    }

    TEST_CASE("[jsb] global class name: 打包器产出的具名类默认导出")
    {
        // 开了 minify 之外的构建时，类可能带名字；多个导出项时也要能找到 default。
        TempScriptDir dir;
        const String path = dir.write("bundled_named.mjs",
                "import { Node } from \"godot\";\n"
                "var Player = class Player extends Node {\n"
                "};\n"
                "export { Player as default, Player as Player };\n");

        const ParsedClass parsed = parse_class(path);
        CHECK(parsed.name == "Player");
        CHECK(parsed.base_type == "Node");
    }

    TEST_CASE("[jsb] global class name: 默认导出不是 Godot 类时不能登记")
    {
        TempScriptDir dir;
        {
            const String path = dir.write("default_function.mjs",
                    "var helper = function (node) { return node.get_name(); };\n"
                    "export { helper as default };\n");
            CHECK(parse_class(path).name.is_empty());
        }
        {
            const String path = dir.write("default_object.mjs",
                    "var config = { speed: 1 };\n"
                    "export { config as default };\n");
            CHECK(parse_class(path).name.is_empty());
        }
    }

    TEST_CASE("[jsb] global class name: 文件读不到时返回空")
    {
        CHECK(parse_class("res://jsb_does_not_exist.mjs").name.is_empty());
    }

    /**
     * 定位模块目录里已有的文件。
     * 不能直接用相对路径：其它用例会通过 GodotJSScriptLanguageIniter 把工作目录切到测试工程里，
     * 用例之间的顺序也不保证。这里按「二进制所在引擎根目录 -> 当前目录 -> 测试工程」依次找。
     */
    inline String locate_module_file(const String& p_path_under_module)
    {
        const String module_rel = String("modules/") + JSB_MODULE_NAME_STRING;
        const String engine_root = OS::get_singleton()->get_executable_path().get_base_dir().get_base_dir();

        const String candidates[] = {
            engine_root.path_join(module_rel).path_join(p_path_under_module),
            module_rel.path_join(p_path_under_module),
        };
        for (const String& candidate : candidates)
        {
            if (FileAccess::exists(candidate))
            {
                return candidate;
            }
        }
        // 工作目录已被切到测试工程时，res:// 就是 tests/project，路径前缀要去掉
        if (p_path_under_module.begins_with("tests/project/"))
        {
            const String in_project = "res://" + p_path_under_module.trim_prefix("tests/project/");
            if (FileAccess::exists(in_project))
            {
                return in_project;
            }
        }
        return String();
    }

    TEST_CASE("[jsb] global class name: 测试工程里的 ESM 脚本类夹具")
    {
        // 与 tests/project/tests/script-class/test-script-class.mjs 对齐：
        // 那份夹具是「产物只有 .mjs 的脚本类」，编辑器必须能认出它的类名。
        const String fixture = locate_module_file("tests/project/tests/script-class/test-script-class.mjs");
        REQUIRE_MESSAGE(!fixture.is_empty(), "cannot locate tests/project/tests/script-class/test-script-class.mjs");

        const ParsedClass parsed = parse_class(fixture);
        CHECK(parsed.name == "TestScriptClass");
        CHECK(parsed.base_type == "Node");
    }
}

#endif
