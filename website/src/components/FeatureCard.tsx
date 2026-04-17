import { makeStyles, tokens, Text } from "@fluentui/react-components";
import {
  TopSpeedRegular,
  PaintBrushRegular,
  CodeRegular,
  PlugConnectedRegular,
  AppsRegular,
  ScaleFillRegular,
} from "@fluentui/react-icons";
import { useTranslation } from "react-i18next";
import type { Feature } from "../data/features";

const iconMap: Record<string, React.ReactNode> = {
  gpu: <TopSpeedRegular />,
  design: <PaintBrushRegular />,
  markup: <CodeRegular />,
  api: <PlugConnectedRegular />,
  controls: <AppsRegular />,
  dpi: <ScaleFillRegular />,
};

const useStyles = makeStyles({
  card: {
    padding: "22px",
    borderRadius: "12px",
    border: `1px solid ${tokens.colorNeutralStroke2}`,
    backgroundColor: tokens.colorNeutralBackground1,
    transitionProperty: "border-color, box-shadow, transform",
    transitionDuration: "200ms",
    transitionTimingFunction: "ease",
    ":hover": {
      borderTopColor: tokens.colorBrandStroke1,
      borderRightColor: tokens.colorBrandStroke1,
      borderBottomColor: tokens.colorBrandStroke1,
      borderLeftColor: tokens.colorBrandStroke1,
      boxShadow: `0 4px 16px rgba(15, 108, 189, 0.08)`,
      transform: "translateY(-2px)",
    },
  },
  iconWrap: {
    width: "36px",
    height: "36px",
    borderRadius: "10px",
    backgroundColor: tokens.colorBrandBackground2,
    display: "flex",
    alignItems: "center",
    justifyContent: "center",
    color: tokens.colorBrandForeground1,
    fontSize: "18px",
    marginBottom: "14px",
  },
  title: {
    fontSize: "14px",
    fontWeight: 600,
    color: tokens.colorNeutralForeground1,
    marginBottom: "6px",
    letterSpacing: "-0.1px",
  },
  description: {
    color: tokens.colorNeutralForeground3,
    lineHeight: 1.55,
    fontSize: "13px",
  },
});

export function FeatureCard({ feature }: { feature: Feature }) {
  const styles = useStyles();
  const { t } = useTranslation();

  return (
    <div className={styles.card}>
      <div className={styles.iconWrap}>{iconMap[feature.icon]}</div>
      <Text className={styles.title} block>{t(feature.titleKey)}</Text>
      <Text size={200} className={styles.description}>
        {t(feature.descKey)}
      </Text>
    </div>
  );
}
