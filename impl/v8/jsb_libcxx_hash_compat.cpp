// Android 上补一个 libc++ 缺的哈希函数。
//
// 为什么会缺：V8 的 Android 预编译库由 V8 自己钉的 NDK（15.7 钉的是 r30）编译，
// 而 Godot 4.7 要求引擎用 NDK r29 编译。r30 的 libc++ 把 std::hash 对内存块（指针、
// 字符串等）的哈希实现挪进了库里，变成一个出库函数 __hash_memory；r29 的 libc++ 里
// 没有这个函数（头文件不声明，libc++_static.a 也不定义）。于是 V8 的目标文件留下一条
// 外部引用，链接时报：
//
//     ld.lld: error: undefined symbol: std::__ndk1::__hash_memory(void const*, unsigned long)
//
// 对着 r29 的 libc++ 逐个核过，V8 缺的 libc++ 符号只有这一个。
//
// 为什么自己实现是安全的：这个函数只被用来算哈希值，要求只有两条 —— 同一个键在同一进程
// 里每次算出同一个值，以及分布别太糟（哈希表性能）。V8 的所有目标文件都是用同一套 r30
// 头文件编的，凡是要用 __hash_memory 的地方都会走到这一份实现，不会出现"一半用新的
// 一半用旧的"。引擎自己的代码用 r29 编译，走的是 libc++ 内联的另一套哈希，两边各自独立，
// 不共享哈希表。
//
// 标成 weak 是为了往前兼容：将来 Godot 升到 r30 或更新的 NDK，libc++ 会提供强定义，
// 链接器自动用那份，这里不会造成重复符号。
#ifdef ANDROID_ENABLED

#include <cstddef>

namespace std {
inline namespace __ndk1 {

__attribute__((weak)) size_t __hash_memory(const void* p_data, size_t p_size) noexcept {
	const unsigned char* data = static_cast<const unsigned char*>(p_data);

	// FNV-1a 逐字节混合，末尾再做一次 64 位散列收尾（splitmix 的最后两轮）。
	// 指针键只有 8 字节，收尾那两轮保证低位也散得开，哈希表不会退化。
	size_t hash = 0xcbf29ce484222325ULL ^ p_size;
	for (size_t i = 0; i < p_size; i++) {
		hash ^= data[i];
		hash *= 0x100000001b3ULL;
	}
	hash ^= hash >> 33;
	hash *= 0xff51afd7ed558ccdULL;
	hash ^= hash >> 29;
	return hash;
}

} // namespace __ndk1
} // namespace std

#endif // ANDROID_ENABLED
