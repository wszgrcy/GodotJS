#ifndef GODOTJS_SHARED_V8_POINTER_TAGS_H
#define GODOTJS_SHARED_V8_POINTER_TAGS_H

#include <cstdint>

// V8 15 asks for a "tag" next to every embedder pointer (internal fields,
// embedder data, v8::External) and refuses to compile if it is missing. The tag
// only exists so V8 can check that a pointer is read back with the same meaning
// it was written with. QuickJS, JavaScriptCore and the web runtime do not have
// that check, so the code shared with them passes the default tag and the
// emulation layer below ignores it.

namespace v8
{
	using EmbedderDataTypeTag = uint16_t;
	using ExternalPointerTypeTag = uint16_t;

	constexpr EmbedderDataTypeTag kEmbedderDataTypeTagDefault = 0;
	constexpr ExternalPointerTypeTag kExternalPointerTypeTagDefault = 0;
}

#endif // GODOTJS_SHARED_V8_POINTER_TAGS_H
