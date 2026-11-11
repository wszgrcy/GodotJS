#ifndef GODOTJS_V8_HEADERS_H
#define GODOTJS_V8_HEADERS_H

// v8-array-buffer.h 在 namespace v8 里写了 `friend class TypedArray;`，但 TypedArray 自己
// 声明在 v8-typed-array.h 里，解析到这一行时还没声明。MSVC 会往外层命名空间找这个名字，
// 找到 Godot 全局的 `template<typename T> class TypedArray`，报 C2990（GCC 和 Clang 不报）。
// 先在本命名空间里把它声明出来，让查找停在 v8 里。
namespace v8 {
class TypedArray;
}

#include <v8.h>
#include <v8-persistent-handle.h>
#include <libplatform/libplatform.h>
#include <v8-inspector.h>
#include <v8-version-string.h>

#if JSB_V8_CPPGC
#   include <v8-cppgc.h>
#   include <cppgc/default-platform.h>
#endif

#include "../../internal/jsb_logger.h"
#include "../../internal/jsb_macros.h"

#include "../shared/jsb_custom_field.h"

#endif
