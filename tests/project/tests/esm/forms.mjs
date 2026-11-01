// 覆盖常见的导出写法：默认导出、具名导出、命名空间导入、重命名、星号转发
import def, { HELLO } from "./hello.mjs";
import * as hello_ns from "./hello.mjs";

export { HELLO as renamed } from "./hello.mjs";
export * from "./extra.mjs";

export const joined = `${def}-${HELLO}-${hello_ns.HELLO}`;

export function double(value) {
    return value * 2;
}

export class Widget {
    get size() {
        return 8;
    }
}

export default "forms";
