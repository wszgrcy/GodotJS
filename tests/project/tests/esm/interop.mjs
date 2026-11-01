// 从 ESM 里 import 非 ESM 的东西：CommonJS、JSON、以及 `godot` 模块
import cjsDefault, { from_cjs, label } from "./cjs_target.js";
import data from "./data.json";
import gd from "godot";

// 属性名不是合法标识符也能转出来
export { "not-an-identifier" as oddName } from "./cjs_target.js";
import { "0" as zeroKeyImport } from "./cjs_target.js";
export const zeroKey = zeroKeyImport;

export const cjs_default_is_object = typeof cjsDefault === "object";
export const cjs_from = from_cjs;
export const cjs_label = label;
export const cjs_default_prop = cjsDefault.default;
export const json_answer = data.answer;
export const json_item_count = data.items.length;
export const godot_node_is_function = typeof gd.Node === "function";

export default "interop";
