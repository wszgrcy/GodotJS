#ifndef GODOTJS_V8_GLOBAL_INIT_H
#define GODOTJS_V8_GLOBAL_INIT_H
#include "jsb_v8_pch.h"

#include "../../internal/jsb_settings.h"
#include "core/config/project_settings.h"
#include "core/io/file_access.h"

namespace jsb::impl
{
    namespace
    {
        // 把配置里写的路径换成 V8 能用的真实文件路径。V8 只认文件系统路径（没有“给它一块内存”的入口），
        // 而 res:// 在导出后位于 pck 内部，没有真实路径可给，只能先解一份到 user://。
        // 找不到文件时返回空串。
        String icu_data_real_path(const String& p_path)
        {
            if (p_path.is_empty())
            {
                return String();
            }

            String path = p_path;

            if (path.is_relative_path())
            {
                // 相对路径按可执行文件所在目录解释，跟“数据放 exe 旁边”的默认布局一致
                if (const OS* os = OS::get_singleton())
                {
                    path = os->get_executable_path().get_base_dir().path_join(path);
                }
            }

            ProjectSettings* project_settings = ProjectSettings::get_singleton();
            const String globalized = project_settings ? project_settings->globalize_path(path) : String();

            if (!globalized.is_empty() && FileAccess::exists(globalized))
            {
                return globalized;
            }

            if (!FileAccess::exists(path))
            {
                return String();
            }

            const int64_t size = FileAccess::get_size(path);

            if (size <= 0)
            {
                return String();
            }

            // 走到这里说明引擎读得到但 V8 读不到（文件在 pck 里）：解一份到 user://，大小对得上就直接复用。
            const String cache_path = String("user://").path_join(path.get_file());

            if (FileAccess::exists(cache_path) && FileAccess::get_size(cache_path) == size)
            {
                return project_settings ? project_settings->globalize_path(cache_path) : String();
            }

            const Ref<FileAccess> src = FileAccess::open(path, FileAccess::READ);

            if (src.is_null())
            {
                return String();
            }

            const Vector<uint8_t> data = src->get_buffer(size);
            Ref<FileAccess> dst = FileAccess::open(cache_path, FileAccess::WRITE);

            if (dst.is_null())
            {
                JSB_LOG(Warning, "ICU data: cannot unpack %s to %s (error %d)", path, cache_path, FileAccess::get_open_error());
                return String();
            }

            dst->store_buffer(data);
            dst->flush();
            dst.unref(); // 得先关掉再交给 V8，否则 V8 可能读到写了一半的文件

            return project_settings ? project_settings->globalize_path(cache_path) : String();
        }

        // Intl.* 和 Date.prototype.toLocaleDateString 这类 API 要靠 ICU，而 ICU 的语言资源不在
        // 预编译的 V8 库里，是一个单独的 icudtl.dat。V8 不会自己去找它，所以在这里显式加载。
        // 找不到时 V8 不会报错，只会让 Intl 静默坏掉（toLocaleDateString 甚至会直接终止进程），
        // 因此加载失败必须提示出来。
        void initialize_icu_data()
        {
            const OS* os = OS::get_singleton();

            if (!os)
            {
                return;
            }

            struct Candidate
            {
                const char* from;
                String path;
            };

            // 谁说了算：项目设置（特殊指定）> 环境变量（CI 或临时覆盖，不用改项目文件）
            // > 可执行文件旁边（桌面零配置）> res://icudtl.dat（数据打进 pck 的场景，
            // 比如 Android：引擎会把它解到 user:// 再读，同样不用配）。
            const String exe_relative = os->get_executable_path().get_base_dir().path_join("icudtl.dat");

            const Candidate candidates[] = {
                { "project setting", jsb::internal::Settings::get_icu_data_path() },
                { "GODOTJS_ICU_DATA", os->get_environment("GODOTJS_ICU_DATA") },
                { "next to the executable", exe_relative },
                { "res://icudtl.dat", "res://icudtl.dat" },
            };

            String data_path;
            const char* data_from = "next to the executable";

            for (const Candidate& candidate : candidates)
            {
                if (candidate.path.is_empty())
                {
                    continue;
                }

                data_path = icu_data_real_path(candidate.path);

                if (!data_path.is_empty())
                {
                    data_from = candidate.from;
                    break;
                }

                JSB_LOG(Verbose, "ICU data not found at %s (from %s)", candidate.path, candidate.from);
            }

            if (data_path.is_empty())
            {
                // 一个都没找到。这里仍然要调一次 InitializeICU：V8 没编 ICU 时它直接返回成功，
                // 编了 ICU 但数据不在才返回失败，只有这个返回值能区分“根本没 ICU”和“有 ICU 但缺数据”。
                data_path = exe_relative;
            }

            const CharString utf8_path = data_path.utf8();

            if (v8::V8::InitializeICU(utf8_path.get_data()))
            {
                JSB_LOG(VeryVerbose, "ICU data initialized (requested %s, from %s)", data_path, data_from);
            }
            else
            {
                JSB_LOG(Warning,
                    "ICU data not loaded from %s (from %s): Intl.* and Date.prototype.toLocale* will not work. "
                    "Point the project setting \"%s\" or the GODOTJS_ICU_DATA environment variable at icudtl.dat, "
                    "or put icudtl.dat next to the executable.",
                    data_path, data_from, jsb::internal::Settings::kIcuDataPathSetting);
            }
        }
    }

    struct GlobalInitialize
    {
#if JSB_V8_CPPGC
        std::unique_ptr<cppgc::DefaultPlatform> platform = std::make_unique<cppgc::DefaultPlatform>();
#else
        std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();
#endif

        GlobalInitialize()
        {
            initialize_icu_data();

#if JSB_EXPOSE_GC_FOR_TESTING
            constexpr char exposeGcArgs[] = "--expose-gc";
            v8::V8::SetFlagsFromString(exposeGcArgs, std::size(exposeGcArgs) - 1);
#endif

#if JSB_V8_JITLESS
        	constexpr char jitlessArgs[] = "--jitless";
        	v8::V8::SetFlagsFromString(jitlessArgs, std::size(jitlessArgs) - 1);
#endif

#if JSB_V8_CPPGC
            v8::V8::InitializePlatform(platform->GetV8Platform());
            cppgc::InitializeProcess(platform->GetPageAllocator());
#else
            v8::V8::InitializePlatform(platform.get());
#endif

            v8::V8::Initialize();
        }

        //NOTE never called in the current implementation
        ~GlobalInitialize()
        {
#if JSB_V8_CPPGC
            cppgc::ShutdownProcess();
#endif
        }

        static v8::Platform* get_platform()
        {
            static GlobalInitialize global_initialize;
#if JSB_V8_CPPGC
            return global_initialize.platform->GetV8Platform();
#else
            return global_initialize.platform.get();
#endif
        }

        static void init()
        {
            jsb_ensure(get_platform());
        }
    };

}
#endif
