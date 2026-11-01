// 从 ESM import 一个会抛错的 CommonJS，错误要传出来
import x from "./bad_cjs.js";

export const v = x;
