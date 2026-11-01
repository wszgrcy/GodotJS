// CommonJS 里 require 一个 .mjs
const hello = require("./hello.mjs");
const target = require("./cjs_target.js");

module.exports = {
    via_require: hello.default,
    via_require_named: hello.HELLO,
    // CommonJS 取值本来就是 obj["名字"]，属性名是不是合法标识符都无所谓
    via_require_odd: target["not-an-identifier"],
};
