import { createDarkTheme, createLightTheme } from "@fluentui/react-components";
import type { BrandVariants } from "@fluentui/react-components";

// Brand color ramp from core-ui theme.h (Fluent Blue)
const brand: BrandVariants = {
  10: "#061724",
  20: "#082338",
  30: "#0a2e4a",
  40: "#0c3b5e",
  50: "#0e4775",
  60: "#115ea3",
  70: "#115ea3",
  80: "#0f6cbd",
  90: "#2886de",
  100: "#479ef5",
  110: "#62abf5",
  120: "#77b7f7",
  130: "#96c6fa",
  140: "#b4d6fa",
  150: "#cfe4fa",
  160: "#ebf3fc",
};

export const lightTheme = createLightTheme(brand);
export const darkTheme = createDarkTheme(brand);
