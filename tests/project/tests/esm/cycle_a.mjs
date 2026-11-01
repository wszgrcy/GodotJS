// Cyclic import: function declarations are initialized during instantiation,
// so referencing an exported function across the cycle never hits a TDZ error.
import { getB } from "./cycle_b.mjs";

export function getA() { return "a"; }

export const seenFromA = getB();
export { getB };
