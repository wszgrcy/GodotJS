// Godot 的 C++ 单测靠 `godot --test` 跑，所以用例必须编进可执行文件里。
//
// Godot 会把 modules/*/tests/*.h 汇总进 modules_tests.gen.h，再由 tests/test_main.cpp
// 包含。我们的用例要用 V8 的头文件和 C++20，而 test_main.cpp 是按引擎自身的参数
// （C++17、没有 V8 头文件路径）编译的，塞进去编不过。所以头文件放在 tests/jsb/ 下
// ——Godot 只扫 tests 目录本身，不扫子目录——由本文件用模块自己的编译环境单独编一个
// 编译单元。TEST_CASE 是静态自注册的，编进来就会出现在 --test 的列表里。
#include "jsb/jsb_test_helpers.h"
#include "jsb/test_jsb_any_runtime.h"
#include "jsb/test_jsb_esm_runtime.h"
#include "jsb/test_jsb_sarray.h"
#include "jsb/test_jsb_script_class_name.h"
#include "jsb/test_jsb_v8_runtime.h"

// 本文件除了静态注册对象外没有别的对外符号，链接器从静态库里挑目标文件时不会带上它。
// register_types.cpp 里调一下这个空函数，把它拽进可执行文件。
namespace jsb::tests
{
void force_link() {}
} // namespace jsb::tests
