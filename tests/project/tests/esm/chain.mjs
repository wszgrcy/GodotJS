// ESM -> CommonJS -> ESM：中间那层是 CommonJS
import bridge from "./cjs_bridge.js";

export const leaf = bridge.leaf;
