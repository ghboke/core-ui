import { createContext, useContext } from "react";

export const ThemeModeContext = createContext<"light" | "dark">("light");

export function useThemeMode() {
  return useContext(ThemeModeContext);
}
