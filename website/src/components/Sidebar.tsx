import { useState } from "react";
import { makeStyles, tokens } from "@fluentui/react-components";
import {
  HomeRegular,
  HomeFilled,
  RocketRegular,
  RocketFilled,
  AppsRegular,
  AppsFilled,
  ColorRegular,
  ColorFilled,
  CodeRegular,
  CodeFilled,
  LayoutColumnTwoRegular,
  LayoutColumnTwoFilled,
  PlugConnectedRegular,
  PlugConnectedFilled,
  BotRegular,
  BotFilled,
  ChevronDownRegular,
  ChevronRightRegular,
  bundleIcon,
} from "@fluentui/react-icons";
import { NavLink, useLocation } from "react-router";
import { useTranslation } from "react-i18next";
import { controls } from "../data/controls";

const HomeIcon = bundleIcon(HomeFilled, HomeRegular);
const RocketIcon = bundleIcon(RocketFilled, RocketRegular);
const MarkupIcon = bundleIcon(CodeFilled, CodeRegular);
const LayoutIcon = bundleIcon(LayoutColumnTwoFilled, LayoutColumnTwoRegular);
const AppsIcon = bundleIcon(AppsFilled, AppsRegular);
const ApiIcon = bundleIcon(PlugConnectedFilled, PlugConnectedRegular);
const ColorIcon = bundleIcon(ColorFilled, ColorRegular);
const AiIcon = bundleIcon(BotFilled, BotRegular);

const useStyles = makeStyles({
  sidebar: {
    width: "220px",
    minWidth: "220px",
    borderRightWidth: "1px",
    borderRightStyle: "solid",
    borderRightColor: tokens.colorNeutralStroke2,
    backgroundColor: tokens.colorNeutralBackground1,
    paddingBlock: "16px",
    paddingInline: "10px",
    display: "flex",
    flexDirection: "column",
    gap: "1px",
    overflowY: "auto",
    "@media (max-width: 768px)": {
      display: "none",
    },
  },
  mobileSidebar: {
    width: "100%",
    paddingBlock: "16px",
    paddingInline: "10px",
    display: "flex",
    flexDirection: "column",
    gap: "1px",
    overflowY: "auto",
  },
  sectionTitle: {
    fontSize: "11px",
    fontWeight: 600,
    color: tokens.colorNeutralForeground4,
    paddingInline: "12px",
    paddingBlock: "12px 6px",
    textTransform: "uppercase",
    letterSpacing: "0.8px",
  },
  navLink: {
    display: "flex",
    alignItems: "center",
    gap: "9px",
    paddingInline: "12px",
    paddingBlock: "7px",
    borderRadius: "8px",
    textDecoration: "none",
    color: tokens.colorNeutralForeground2,
    fontSize: "13px",
    fontWeight: 400,
    transitionProperty: "background-color, color",
    transitionDuration: "120ms",
    ":hover": {
      backgroundColor: tokens.colorSubtleBackgroundHover,
      color: tokens.colorNeutralForeground1,
    },
  },
  navLinkActive: {
    display: "flex",
    alignItems: "center",
    gap: "9px",
    paddingInline: "12px",
    paddingBlock: "7px",
    borderRadius: "8px",
    textDecoration: "none",
    fontSize: "13px",
    fontWeight: 600,
    backgroundColor: tokens.colorBrandBackground2,
    color: tokens.colorBrandForeground1,
  },
  icon: {
    fontSize: "18px",
    flexShrink: 0,
  },
  // Controls submenu
  expandBtn: {
    display: "flex",
    alignItems: "center",
    gap: "9px",
    paddingInline: "12px",
    paddingBlock: "7px",
    borderRadius: "8px",
    borderTopWidth: "0",
    borderRightWidth: "0",
    borderBottomWidth: "0",
    borderLeftWidth: "0",
    backgroundColor: "transparent",
    color: tokens.colorNeutralForeground2,
    fontSize: "13px",
    fontWeight: 400,
    cursor: "pointer",
    width: "100%",
    textAlign: "start",
    transitionProperty: "background-color, color",
    transitionDuration: "120ms",
    ":hover": {
      backgroundColor: tokens.colorSubtleBackgroundHover,
      color: tokens.colorNeutralForeground1,
    },
  },
  expandBtnActive: {
    display: "flex",
    alignItems: "center",
    gap: "9px",
    paddingInline: "12px",
    paddingBlock: "7px",
    borderRadius: "8px",
    borderTopWidth: "0",
    borderRightWidth: "0",
    borderBottomWidth: "0",
    borderLeftWidth: "0",
    backgroundColor: tokens.colorBrandBackground2,
    color: tokens.colorBrandForeground1,
    fontSize: "13px",
    fontWeight: 600,
    cursor: "pointer",
    width: "100%",
    textAlign: "start",
  },
  expandArrow: {
    marginInlineStart: "auto",
    fontSize: "12px",
    color: tokens.colorNeutralForeground4,
  },
  subMenu: {
    display: "flex",
    flexDirection: "column",
    gap: "0px",
    paddingInlineStart: "20px",
    marginBlock: "2px",
  },
  subCategoryTitle: {
    fontSize: "10px",
    fontWeight: 600,
    color: tokens.colorNeutralForeground4,
    paddingInline: "12px",
    paddingBlock: "6px 2px",
    textTransform: "uppercase",
    letterSpacing: "0.5px",
  },
  subLink: {
    display: "flex",
    alignItems: "center",
    paddingInline: "12px",
    paddingBlock: "4px",
    borderRadius: "6px",
    textDecoration: "none",
    color: tokens.colorNeutralForeground3,
    fontSize: "12px",
    fontWeight: 400,
    transitionProperty: "background-color, color",
    transitionDuration: "120ms",
    ":hover": {
      backgroundColor: tokens.colorSubtleBackgroundHover,
      color: tokens.colorNeutralForeground1,
    },
  },
  subLinkActive: {
    display: "flex",
    alignItems: "center",
    paddingInline: "12px",
    paddingBlock: "4px",
    borderRadius: "6px",
    textDecoration: "none",
    fontSize: "12px",
    fontWeight: 600,
    backgroundColor: tokens.colorBrandBackground2,
    color: tokens.colorBrandForeground1,
  },
});

const categoryOrder = ["container", "input", "display", "navigation"] as const;

interface SidebarProps {
  mobile?: boolean;
  onNavigate?: () => void;
}

export function Sidebar({ mobile, onNavigate }: SidebarProps) {
  const styles = useStyles();
  const { t } = useTranslation();
  const location = useLocation();

  const isControlsPath = location.pathname.startsWith("/docs/controls");
  const [controlsOpen, setControlsOpen] = useState(isControlsPath);

  const renderLink = (to: string, label: string, icon: React.ReactNode, end?: boolean) => (
    <NavLink
      key={to}
      to={to}
      end={end}
      className={({ isActive }) => (isActive ? styles.navLinkActive : styles.navLink)}
      onClick={onNavigate}
    >
      <span className={styles.icon}>{icon}</span>
      {label}
    </NavLink>
  );

  const grouped = categoryOrder.map((cat) => ({
    key: cat,
    controls: controls.filter((c) => c.category === cat),
  }));

  return (
    <nav className={mobile ? styles.mobileSidebar : styles.sidebar}>
      {renderLink("/", t("nav.home"), <HomeIcon />, true)}
      <div className={styles.sectionTitle}>{t("nav.docs")}</div>
      {renderLink("/docs/getting-started", t("nav.gettingStarted"), <RocketIcon />)}
      {renderLink("/docs/markup", t("nav.markup"), <MarkupIcon />)}
      {renderLink("/docs/layout", t("nav.layout"), <LayoutIcon />)}

      {/* Controls with submenu */}
      <button
        className={isControlsPath ? styles.expandBtnActive : styles.expandBtn}
        onClick={() => setControlsOpen((prev) => !prev)}
      >
        <span className={styles.icon}><AppsIcon /></span>
        {t("nav.controls")}
        <span className={styles.expandArrow}>
          {controlsOpen ? <ChevronDownRegular /> : <ChevronRightRegular />}
        </span>
      </button>

      {controlsOpen && (
        <div className={styles.subMenu}>
          {/* Overview link */}
          <NavLink
            to="/docs/controls"
            end
            className={({ isActive }) => (isActive ? styles.subLinkActive : styles.subLink)}
            onClick={onNavigate}
          >
            {t("nav.controlsOverview")}
          </NavLink>

          {grouped.map((group) => (
            <div key={group.key}>
              <div className={styles.subCategoryTitle}>
                {t(`controls.category.${group.key}`)}
              </div>
              {group.controls.map((ctrl) => (
                <NavLink
                  key={ctrl.name}
                  to={`/docs/controls/${ctrl.name.toLowerCase()}`}
                  className={({ isActive }) => (isActive ? styles.subLinkActive : styles.subLink)}
                  onClick={onNavigate}
                >
                  {ctrl.name}
                </NavLink>
              ))}
            </div>
          ))}
        </div>
      )}

      {renderLink("/docs/c-api", t("nav.cApi"), <ApiIcon />)}
      {renderLink("/docs/design-system", t("nav.designSystem"), <ColorIcon />)}
      {renderLink("/docs/ai", t("nav.aiGuide"), <AiIcon />)}
    </nav>
  );
}
