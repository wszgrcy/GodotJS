// 一个普通 CommonJS 模块，用来测试 ESM 里 import 一个非 ESM 文件
module.exports = {
    from_cjs: 7,
    label: "cjs",
};

// 属性名不是合法标识符的几种情况：带连字符、纯数字、和默认导出撞名
module.exports["not-an-identifier"] = "odd-name";
module.exports["0"] = "zero-key";
module.exports.default = "cjs-default-prop";
