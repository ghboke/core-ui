import { useState, useCallback, useEffect, lazy, Suspense } from "react";
import { FluentProvider, Spinner } from "@fluentui/react-components";
import { Routes, Route } from "react-router";
import { lightTheme, darkTheme } from "./theme";
import { ThemeModeContext } from "./ThemeContext";
import { Layout } from "./components/Layout";
import { Home } from "./pages/Home";
import { GettingStarted } from "./pages/GettingStarted";
import { Markup } from "./pages/Markup";
import { LayoutDoc } from "./pages/LayoutDoc";
import { Controls } from "./pages/Controls";
import { ControlDetail } from "./pages/ControlDetail";
import { CApi } from "./pages/CApi";
import { DesignSystem } from "./pages/DesignSystem";

const AiGuide = lazy(() => import("./pages/AiGuide").then(m => ({ default: m.AiGuide })));

type ThemeMode = "light" | "dark";

const THEME_KEY = "ui-core-theme";

function App() {
  const [themeMode, setThemeMode] = useState<ThemeMode>(() => {
    const saved = localStorage.getItem(THEME_KEY);
    if (saved === "light" || saved === "dark") return saved;
    if (typeof window !== "undefined" && window.matchMedia("(prefers-color-scheme: dark)").matches) {
      return "dark";
    }
    return "light";
  });

  useEffect(() => {
    localStorage.setItem(THEME_KEY, themeMode);
  }, [themeMode]);

  const toggleTheme = useCallback(() => {
    setThemeMode((prev) => (prev === "light" ? "dark" : "light"));
  }, []);

  const theme = themeMode === "dark" ? darkTheme : lightTheme;

  return (
    <FluentProvider theme={theme} style={{ height: "100%" }}>
      <ThemeModeContext.Provider value={themeMode}>
        <Routes>
          <Route element={<Layout themeMode={themeMode} onToggleTheme={toggleTheme} />}>
            <Route index element={<Home />} />
            <Route path="docs/getting-started" element={<GettingStarted />} />
            <Route path="docs/markup" element={<Markup />} />
            <Route path="docs/layout" element={<LayoutDoc />} />
            <Route path="docs/controls" element={<Controls />} />
            <Route path="docs/controls/:name" element={<ControlDetail />} />
            <Route path="docs/c-api" element={<CApi />} />
            <Route path="docs/design-system" element={<DesignSystem />} />
            <Route path="docs/ai" element={
              <Suspense fallback={<div style={{ display: "flex", justifyContent: "center", paddingTop: "80px" }}><Spinner size="medium" /></div>}>
                <AiGuide />
              </Suspense>
            } />
          </Route>
        </Routes>
      </ThemeModeContext.Provider>
    </FluentProvider>
  );
}

export default App;
