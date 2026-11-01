// 具名 import 引擎类：`godot` 模块是 Proxy，列不出自有属性，ESM 链接阶段就会失败
import { Node } from "godot";

export const ok = typeof Node;
