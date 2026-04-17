import { useState, useCallback } from "react";
import { makeStyles, tokens } from "@fluentui/react-components";
import { Outlet } from "react-router";
import { Header } from "./Header";
import { Sidebar } from "./Sidebar";

const useStyles = makeStyles({
  root: {
    display: "flex",
    flexDirection: "column",
    height: "100%",
    backgroundColor: tokens.colorNeutralBackground1,
  },
  body: {
    display: "flex",
    flex: 1,
    overflow: "hidden",
  },
  content: {
    flex: 1,
    overflowY: "auto",
    paddingTop: "36px",
    paddingRight: "48px",
    paddingBottom: "64px",
    paddingLeft: "48px",
    "@media (max-width: 768px)": {
      paddingTop: "24px",
      paddingRight: "16px",
      paddingBottom: "48px",
      paddingLeft: "16px",
    },
  },
  overlay: {
    display: "none",
    "@media (max-width: 768px)": {
      display: "block",
      position: "fixed",
      inset: 0,
      backgroundColor: "rgba(0,0,0,0.4)",
      zIndex: 50,
    },
  },
  mobileSidebar: {
    "@media (max-width: 768px)": {
      position: "fixed",
      top: "52px",
      left: 0,
      bottom: 0,
      zIndex: 51,
      width: "240px",
      backgroundColor: tokens.colorNeutralBackground1,
      borderRightWidth: "1px",
      borderRightStyle: "solid",
      borderRightColor: tokens.colorNeutralStroke2,
      overflowY: "auto",
    },
  },
});

interface LayoutProps {
  themeMode: "light" | "dark";
  onToggleTheme: () => void;
}

export function Layout({ themeMode, onToggleTheme }: LayoutProps) {
  const styles = useStyles();
  const [mobileMenuOpen, setMobileMenuOpen] = useState(false);

  const toggleMobileMenu = useCallback(() => {
    setMobileMenuOpen((prev) => !prev);
  }, []);

  const closeMobileMenu = useCallback(() => {
    setMobileMenuOpen(false);
  }, []);

  return (
    <div className={styles.root}>
      <Header themeMode={themeMode} onToggleTheme={onToggleTheme} onToggleMobileMenu={toggleMobileMenu} />
      <div className={styles.body}>
        <Sidebar onNavigate={closeMobileMenu} />
        {mobileMenuOpen && (
          <>
            <div className={styles.overlay} onClick={closeMobileMenu} />
            <div className={styles.mobileSidebar}>
              <Sidebar mobile onNavigate={closeMobileMenu} />
            </div>
          </>
        )}
        <main className={styles.content}>
          <Outlet />
        </main>
      </div>
    </div>
  );
}
