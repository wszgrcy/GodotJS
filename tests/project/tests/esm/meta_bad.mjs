// import.meta.resolve 解析不到要抛错
export const outcome = (() => {
    try {
        import.meta.resolve("./nope.mjs");
        return "no-throw";
    } catch (e) {
        return "threw:" + String(e && e.message ? e.message : e);
    }
})();
