import { makeStyles, tokens, Text, Badge } from "@fluentui/react-components";
import { Link } from "react-router";
import { useTranslation } from "react-i18next";
import { ChevronRightRegular } from "@fluentui/react-icons";
import type { ControlInfo } from "../data/controls";

const categoryColors: Record<string, "brand" | "success" | "warning" | "informative"> = {
  container: "informative",
  input: "brand",
  display: "success",
  navigation: "warning",
};

const useStyles = makeStyles({
  card: {
    display: "flex",
    flexDirection: "column",
    paddingBlock: "18px",
    paddingInline: "18px",
    borderRadius: "12px",
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
    backgroundColor: tokens.colorNeutralBackground1,
    textDecoration: "none",
    color: "inherit",
    transitionProperty: "border-color, box-shadow, transform",
    transitionDuration: "180ms",
    transitionTimingFunction: "ease",
    ":hover": {
      borderTopColor: tokens.colorBrandStroke1,
      borderRightColor: tokens.colorBrandStroke1,
      borderBottomColor: tokens.colorBrandStroke1,
      borderLeftColor: tokens.colorBrandStroke1,
      boxShadow: `0 2px 12px rgba(15, 108, 189, 0.06)`,
      transform: "translateY(-1px)",
    },
    ":focus-visible": {
      outlineWidth: "2px",
      outlineStyle: "solid",
      outlineColor: tokens.colorBrandStroke1,
      outlineOffset: "2px",
    },
  },
  header: {
    display: "flex",
    alignItems: "center",
    justifyContent: "space-between",
    marginBottom: "6px",
  },
  name: {
    fontFamily: "'Cascadia Code', Consolas, monospace",
    fontWeight: 600,
    fontSize: "14px",
    color: tokens.colorNeutralForeground1,
  },
  description: {
    color: tokens.colorNeutralForeground3,
    marginBottom: "14px",
    lineHeight: 1.5,
    fontSize: "13px",
    flex: 1,
  },
  footer: {
    display: "flex",
    alignItems: "center",
    justifyContent: "space-between",
  },
  api: {
    fontFamily: "'Cascadia Code', Consolas, monospace",
    fontSize: "11px",
    color: tokens.colorBrandForeground1,
    backgroundColor: tokens.colorBrandBackground2,
    paddingBlock: "3px",
    paddingInline: "8px",
    borderRadius: "6px",
  },
  arrow: {
    color: tokens.colorNeutralForeground4,
    fontSize: "14px",
  },
});

export function ControlCard({ control }: { control: ControlInfo }) {
  const styles = useStyles();
  const { t } = useTranslation();

  return (
    <Link to={`/docs/controls/${control.name.toLowerCase()}`} className={styles.card}>
      <div className={styles.header}>
        <Text className={styles.name}>{t(control.nameKey)}</Text>
        <Badge appearance="tint" color={categoryColors[control.category]} size="small">
          {t(`controls.category.${control.category}`)}
        </Badge>
      </div>
      <Text className={styles.description}>{t(control.descKey)}</Text>
      <div className={styles.footer}>
        <span className={styles.api}>{control.cApiCreate}</span>
        <ChevronRightRegular className={styles.arrow} />
      </div>
    </Link>
  );
}
