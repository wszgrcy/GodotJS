// 三条路径指向同一个模块，拿到的必须是同一个实例
import { a } from "./identity_a.mjs";
import { b } from "./identity_b.mjs";
import { id } from "./identity.mjs";

export const same = a === b && b === id;
