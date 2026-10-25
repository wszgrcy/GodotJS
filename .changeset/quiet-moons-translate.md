---
"@godot-js/editor": patch
---

`--generate-types` now follows the engine locale for doc comments

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
