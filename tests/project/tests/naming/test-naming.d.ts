import { Node } from "godot";

/**
 * `test-naming.js` 的类型声明。
 *
 * 场景 codegen 会为 `Naming.tscn` 生成 `PackedScene<TestNaming>`，把默认导出当类型用，
 * 所以这里必须给出类声明，不能只写 `any`。
 */
declare class TestNaming extends Node {
}

export default TestNaming;
