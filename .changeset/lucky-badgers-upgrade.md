---
"@godot-js/editor": minor
---

Upgrade to Godot 4.7.2

- adapt to `ClassDB::ClassInfo` moving a class' constants, enums and signals into `GDType`.
  The new accessors include inherited members unless `p_no_inheritance` is passed, so they are
  called with `true` to keep reporting only what a class declares itself, which is what 4.6
  reported. Leaving the default made the generated typings repeat every inherited enum and
  integer constant in each subclass, and those copies lost their documentation comments.
- `Script::instance_has()` is no longer virtual in 4.7 (deprecated in favour of
  `Object::get_script()`), so the `GodotJSScript` override is only compiled for 4.6.
- `memnew()` on a `RefCounted` now returns `Ref<T>` instead of `T*`.
- `AnimationMixer::get_animation_library_list()` and `AnimationLibrary::get_animation_list()`
  now fill a `LocalVector<StringName>` instead of a `List<StringName>`.
- `HashSet` copy construction is now explicit.
- include `core/object/callable_mp.h` explicitly, it is no longer pulled in transitively.
- keep 4.6 support through `compat/jsb_engine_compat.h`.
