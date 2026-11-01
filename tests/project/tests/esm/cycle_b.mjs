// The other half of the cycle: reads back through the cycle via a hoisted function.
import { getA } from "./cycle_a.mjs";

export function getB() { return "b"; }

export const seenFromB = getA();
