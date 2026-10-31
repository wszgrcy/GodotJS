"use strict";
Object.defineProperty(exports, "__esModule", { value: true });

const { Node, Node3D } = require("godot");

// 判负靠这行 sentinel：runner 要求输出里必须出现 COMPLETED，且一旦出现 FAILED 就判失败。
// 这里不 require ../test-status —— 那是 .ts 源，被 .js require 时会被当 JS 编译而报错。
const FAILURE_SENTINEL = "GODOTJS_TEST_PROJECT_FAILED";

let failed = false;

function check(label, ok, detail) {
	if (!ok) {
		failed = true;
		console.error(`${FAILURE_SENTINEL}: naming: ${label}`);
		console.error(detail);
	}
}

/**
 * 覆盖 `Node.name` 属性能真正读写引擎节点名。
 *
 * 上游曾经把 `name` 整个排除在属性绑定之外（怕和 JS 函数/类自带的 `name` 冲突），
 * 于是 `node.name = "X"` 只会改到 JS 包装对象，引擎名不变且不报错。
 * 这里同时守住反面：JS 类与函数自带的 `name` 不能被 Godot 属性顶掉。
 */
class TestNaming extends Node {
	_ready() {
		try {
			// 属性读写要和引擎名一致，这是 GDScript 里本来就成立的写法
			const node = new Node3D();
			this.add_child(node);
			node.set_name("Alpha");
			check("set_name 之后 .name 读到引擎名", node.name === "Alpha",
				`get_name()=${node.get_name()} 而 .name=${String(node.name)}`);

			node.name = "Beta";
			check(".name 赋值要真的改到引擎名", node.get_name() === "Beta",
				`赋值后 get_name()=${node.get_name()}`);

			// 按名字解析依赖引擎名：改名后旧路径失效、新路径可用
			const child = new Node3D();
			node.add_child(child);
			child.set_name("Kid");
			check("改名后按新路径能找到", this.get_node_or_null("Beta/Kid") !== null,
				'get_node_or_null("Beta/Kid") 返回 null');
			check("改名后旧路径应当失效", this.get_node_or_null("Alpha/Kid") === null,
				"旧路径 Alpha/Kid 竟然还能解析");

			// 反面：Godot 属性装在原型上，不许顶掉 JS 类/函数自带的 name
			check("构造器 Node3D.name 未被顶掉", Node3D.name === "Node3D",
				`Node3D.name=${String(Node3D.name)}`);
			class ScriptedNode extends Node3D {}
			check("子类 name 未被顶掉", ScriptedNode.name === "ScriptedNode",
				`ScriptedNode.name=${String(ScriptedNode.name)}`);
			function helper() {}
			check("普通函数 name 未被顶掉", helper.name === "helper",
				`helper.name=${String(helper.name)}`);

			// 与原生 Godot 语义一致：同名兄弟自动加后缀
			const first = new Node3D();
			this.add_child(first);
			first.name = "Dup";
			const second = new Node3D();
			this.add_child(second);
			second.name = "Dup";
			check("同名兄弟自动加后缀", first.get_name() === "Dup" && second.get_name() === "Dup2",
				`实际是 ${first.get_name()} / ${second.get_name()}`);
		} catch (error) {
			failed = true;
			console.error(`${FAILURE_SENTINEL}: naming`);
			console.error(error);
		}
		// 这行用来确认用例真的被加载执行了：脚本挂不上时引擎只报 ERROR，sentinel 照样打全
		console.log(`TestNaming ready (${failed ? "FAILED" : "ok"})`);
	}
}

exports.default = TestNaming;
