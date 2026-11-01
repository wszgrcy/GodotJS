// 默认导出一个继承引擎类型的类，用来验证 `.mjs` 也能当 Godot 脚本类用
import gd from "godot";

export default class EsmWidget extends gd.Node {
    fn() {
        return 7;
    }
}
