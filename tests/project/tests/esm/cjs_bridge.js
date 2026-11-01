// CommonJS in the middle of an ESM -> CJS -> ESM chain.
const widget = require("./widget.mjs");
exports.leaf = "leaf-via-" + widget.kind;
