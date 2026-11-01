// 永远等不到的顶层 await：同步入口必须报错，不能返回半成品
await new Promise(() => {});

export const never = "never";
