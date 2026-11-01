// import.meta 内容
export const url = import.meta.url ?? "";
export const meta_keys = Object.keys(import.meta).sort().join(",");
export const resolved = import.meta.resolve("./extra.mjs");
