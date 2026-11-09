#ifndef GODOTJS_SETTINGS_H
#define GODOTJS_SETTINGS_H

#include <cstdint>
#include "../compat/jsb_compat.h"
#include "jsb_macros.h"

namespace jsb::internal
{
    class Settings
    {
    public:
        // 项目设置里用来指定 ICU 数据文件（icudtl.dat）位置的键，留空表示让引擎自己找。
        static constexpr char kIcuDataPathSetting[] = JSB_MODULE_NAME_STRING "/runtime/intl/icu_data_path";

        static bool is_bridge_logging_enabled();
        static uint16_t get_debugger_port();
        static String get_debugger_source_map_base_url();
        static bool get_wait_for_debugger();
        static bool get_sourcemap_enabled();

        /**
         * get the project relative path for `outDir` (it refers to `.godot/GodotJS` by default)
         */
        static String get_jsb_out_dir_name();

        /**
         * get path for .tsbuildinfo (.godot/.tsbuildinfo)
         */
        static String get_tsbuildinfo_path();

        /**
         * get the res path for `outDir`, it's equivalent to `res://` + get_jsb_out_dir_name()
         */
        static String get_jsb_out_res_path();

        static String get_indentation();

        static String get_project_data_dir_name();

        static PackedStringArray get_additional_search_paths();

        static String get_entry_script_path();

        /**
         * ICU 数据文件（icudtl.dat）的位置，来自项目设置；空串表示没指定，由引擎自己找。
         */
        static String get_icu_data_path();

        static bool get_camel_case_bindings_enabled();

        static bool is_packaging_with_source_map();

        static PackedStringArray get_packaging_include_files();

        static PackedStringArray get_packaging_include_directories();

        static bool is_packaging_referenced_node_modules();

#ifdef TOOLS_ENABLED
        // [EDITOR ONLY]
        static bool editor_settings_available();
        static PackedStringArray get_ignored_classes();
        static String get_autogen_path();
        static bool get_autogen_scene_dts_on_save();
        static bool get_gen_scene_dts();
        static bool get_autogen_resource_dts_on_save();
        static bool get_gen_resource_dts();
        static bool get_codegen_use_project_settings();
#endif
    };
}

#endif
