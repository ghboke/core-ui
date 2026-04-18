import { useState, useEffect, useCallback } from "react";
import {
  makeStyles,
  tokens,
  Button,
  Tooltip,
} from "@fluentui/react-components";
import {
  WeatherMoonRegular,
  WeatherSunnyRegular,
  CodeRegular,
  LocalLanguageRegular,
  SearchRegular,
  NavigationRegular,
} from "@fluentui/react-icons";
import { Link } from "react-router";
import { useTranslation } from "react-i18next";
import { SearchDialog } from "./SearchDialog";
import versionData from "../../../version.json";

const useStyles = makeStyles({
  header: {
    display: "flex",
    alignItems: "center",
    height: "52px",
    paddingInline: "20px",
    borderBottomWidth: "1px",
    borderBottomStyle: "solid",
    borderBottomColor: tokens.colorNeutralStroke2,
    backgroundColor: tokens.colorNeutralBackground1,
    position: "sticky",
    top: 0,
    zIndex: 100,
  },
  brand: {
    display: "flex",
    alignItems: "center",
    gap: "10px",
    textDecoration: "none",
    color: tokens.colorNeutralForeground1,
    fontWeight: 700,
    fontSize: "15px",
    letterSpacing: "-0.2px",
  },
  brandIcon: {
    width: "28px",
    height: "28px",
    borderRadius: "8px",
    backgroundColor: tokens.colorBrandBackground,
    display: "flex",
    alignItems: "center",
    justifyContent: "center",
    color: tokens.colorNeutralForegroundOnBrand,
    fontSize: "11px",
    fontWeight: 800,
    letterSpacing: "0.5px",
  },
  version: {
    fontSize: "11px",
    fontWeight: 500,
    color: tokens.colorNeutralForeground4,
    backgroundColor: tokens.colorNeutralBackground3,
    paddingBlock: "2px",
    paddingInline: "6px",
    borderRadius: "4px",
    marginInlineStart: "6px",
    lineHeight: "16px",
  },
  spacer: {
    flex: 1,
  },
  actions: {
    display: "flex",
    alignItems: "center",
    gap: "2px",
  },
  langLabel: {
    fontSize: "12px",
    fontWeight: 600,
    marginInlineStart: "2px",
  },
  searchBtn: {
    minWidth: "180px",
    justifyContent: "flex-start",
    fontWeight: 400,
    color: tokens.colorNeutralForeground3,
    backgroundColor: tokens.colorNeutralBackground3,
    borderRadius: "8px",
    marginInlineEnd: "8px",
    height: "32px",
    "@media (max-width: 768px)": {
      display: "none",
    },
    ":hover": {
      backgroundColor: tokens.colorNeutralBackground4,
    },
  },
  kbd: {
    fontSize: "10px",
    fontFamily: "'Cascadia Code', Consolas, monospace",
    color: tokens.colorNeutralForeground4,
    backgroundColor: tokens.colorNeutralBackground1,
    paddingBlock: "1px",
    paddingInline: "6px",
    borderRadius: "4px",
    borderTopWidth: "1px",
    borderRightWidth: "1px",
    borderBottomWidth: "1px",
    borderLeftWidth: "1px",
    borderTopStyle: "solid",
    borderRightStyle: "solid",
    borderBottomStyle: "solid",
    borderLeftStyle: "solid",
    borderTopColor: tokens.colorNeutralStroke2,
    borderRightColor: tokens.colorNeutralStroke2,
    borderBottomColor: tokens.colorNeutralStroke2,
    borderLeftColor: tokens.colorNeutralStroke2,
    marginInlineStart: "auto",
    lineHeight: "18px",
  },
  menuBtn: {
    display: "none",
    "@media (max-width: 768px)": {
      display: "inline-flex",
    },
  },
  hideOnMobile: {
    "@media (max-width: 768px)": {
      display: "none",
    },
  },
});

interface HeaderProps {
  themeMode: "light" | "dark";
  onToggleTheme: () => void;
  onToggleMobileMenu?: () => void;
}

export function Header({ themeMode, onToggleTheme, onToggleMobileMenu }: HeaderProps) {
  const styles = useStyles();
  const { t, i18n } = useTranslation();
  const [searchOpen, setSearchOpen] = useState(false);

  const toggleLang = () => {
    const next = i18n.language === "zh-CN" ? "en" : "zh-CN";
    i18n.changeLanguage(next);
  };

  const langLabel = i18n.language === "zh-CN" ? "EN" : "中";

  const handleKeyDown = useCallback((e: KeyboardEvent) => {
    if ((e.ctrlKey || e.metaKey) && e.key === "k") {
      e.preventDefault();
      setSearchOpen(true);
    }
  }, []);

  useEffect(() => {
    window.addEventListener("keydown", handleKeyDown);
    return () => window.removeEventListener("keydown", handleKeyDown);
  }, [handleKeyDown]);

  return (
    <>
      <header className={styles.header}>
        {onToggleMobileMenu && (
          <Button
            appearance="subtle"
            icon={<NavigationRegular />}
            onClick={onToggleMobileMenu}
            size="small"
            className={styles.menuBtn}
            aria-label="Menu"
          />
        )}
        <Link to="/" className={styles.brand}>
          <div className={styles.brandIcon}>UI</div>
          <span>CORE UI</span>
          <span className={styles.version}>v{versionData.version}</span>
        </Link>
        <div className={styles.spacer} />
        <div className={styles.actions}>
          <Button
            appearance="subtle"
            icon={<SearchRegular />}
            className={styles.searchBtn}
            onClick={() => setSearchOpen(true)}
            size="small"
          >
            {t("search.label")}
            <span className={styles.kbd}>Ctrl K</span>
          </Button>
          <Tooltip content={t("header.switchLang")} relationship="label">
            <Button appearance="subtle" icon={<LocalLanguageRegular />} onClick={toggleLang} size="small" className={styles.hideOnMobile}>
              <span className={styles.langLabel}>{langLabel}</span>
            </Button>
          </Tooltip>
          <Tooltip content={themeMode === "dark" ? t("header.lightMode") : t("header.darkMode")} relationship="label">
            <Button
              appearance="subtle"
              icon={themeMode === "dark" ? <WeatherSunnyRegular /> : <WeatherMoonRegular />}
              onClick={onToggleTheme}
              size="small"
              aria-label={themeMode === "dark" ? t("header.lightMode") : t("header.darkMode")}
            />
          </Tooltip>
          <Tooltip content={t("header.github")} relationship="label">
            <Button
              appearance="subtle"
              icon={<CodeRegular />}
              as="a"
              href="https://github.com"
              target="_blank"
              size="small"
              aria-label="GitHub"
              className={styles.hideOnMobile}
            />
          </Tooltip>
        </div>
      </header>
      <SearchDialog open={searchOpen} onClose={() => setSearchOpen(false)} />
    </>
  );
}
