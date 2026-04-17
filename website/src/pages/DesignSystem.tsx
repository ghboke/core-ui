import { makeStyles, tokens, Text, Card, Divider } from "@fluentui/react-components";
import { useTranslation } from "react-i18next";

const useStyles = makeStyles({
  page: {
    maxWidth: "880px",
  },
  title: {
    fontSize: "32px",
    fontWeight: 700,
    color: tokens.colorNeutralForeground1,
    marginBottom: "8px",
  },
  subtitle: {
    color: tokens.colorNeutralForeground2,
    marginBottom: "32px",
    lineHeight: "24px",
  },
  section: {
    marginBottom: "40px",
  },
  sectionTitle: {
    fontSize: "20px",
    fontWeight: 600,
    color: tokens.colorNeutralForeground1,
    marginBottom: "16px",
  },
  colorGrid: {
    display: "grid",
    gridTemplateColumns: "repeat(auto-fill, minmax(140px, 1fr))",
    gap: "12px",
  },
  colorCard: {
    padding: "0",
    overflow: "hidden",
  },
  colorSwatch: {
    height: "64px",
  },
  colorInfo: {
    padding: "8px 12px",
  },
  colorName: {
    fontSize: "12px",
    fontWeight: 600,
    color: tokens.colorNeutralForeground1,
  },
  colorHex: {
    fontSize: "11px",
    fontFamily: "Consolas, monospace",
    color: tokens.colorNeutralForeground3,
  },
  typeRow: {
    display: "flex",
    alignItems: "baseline",
    justifyContent: "space-between",
    paddingBlock: "12px",
    borderBottom: `1px solid ${tokens.colorNeutralStroke2}`,
  },
  typeSample: {
    color: tokens.colorNeutralForeground1,
  },
  typeMeta: {
    fontSize: "12px",
    fontFamily: "Consolas, monospace",
    color: tokens.colorNeutralForeground3,
    textAlign: "end" as const,
  },
  spacingRow: {
    display: "flex",
    alignItems: "center",
    gap: "12px",
    paddingBlock: "6px",
  },
  spacingBar: {
    height: "12px",
    backgroundColor: tokens.colorBrandBackground,
    borderRadius: tokens.borderRadiusSmall,
    flexShrink: 0,
  },
  spacingLabel: {
    fontSize: "13px",
    fontFamily: "Consolas, monospace",
    color: tokens.colorNeutralForeground2,
    minWidth: "80px",
  },
  spacingValue: {
    fontSize: "12px",
    color: tokens.colorNeutralForeground3,
  },
  radiusGrid: {
    display: "grid",
    gridTemplateColumns: "repeat(auto-fill, minmax(100px, 1fr))",
    gap: "16px",
  },
  radiusItem: {
    display: "flex",
    flexDirection: "column",
    alignItems: "center",
    gap: "8px",
  },
  radiusBox: {
    width: "64px",
    height: "64px",
    border: `2px solid ${tokens.colorBrandBackground}`,
    backgroundColor: tokens.colorBrandBackground2,
  },
  radiusLabel: {
    fontSize: "12px",
    fontWeight: 600,
    color: tokens.colorNeutralForeground1,
  },
  radiusValue: {
    fontSize: "11px",
    fontFamily: "Consolas, monospace",
    color: tokens.colorNeutralForeground3,
  },
  shadowGrid: {
    display: "grid",
    gridTemplateColumns: "repeat(auto-fill, minmax(120px, 1fr))",
    gap: "24px",
  },
  shadowItem: {
    display: "flex",
    flexDirection: "column",
    alignItems: "center",
    gap: "8px",
  },
  shadowBox: {
    width: "80px",
    height: "80px",
    backgroundColor: tokens.colorNeutralBackground1,
    borderRadius: tokens.borderRadiusMedium,
  },
});

const brandColors = [
  { name: "shade50", hex: "#061724" },
  { name: "shade40", hex: "#082338" },
  { name: "shade30", hex: "#0a2e4a" },
  { name: "shade20", hex: "#0c3b5e" },
  { name: "shade10", hex: "#0e4775" },
  { name: "primary", hex: "#0f6cbd" },
  { name: "tint10", hex: "#2886de" },
  { name: "tint20", hex: "#479ef5" },
  { name: "tint30", hex: "#62abf5" },
  { name: "tint40", hex: "#77b7f7" },
  { name: "tint50", hex: "#96c6fa" },
  { name: "tint60", hex: "#ebf3fc" },
];

const statusColors = [
  { name: "danger", hex: "#d13438" },
  { name: "success", hex: "#107c10" },
  { name: "warning", hex: "#fde300" },
  { name: "info", hex: "#0078d4" },
];

const typeRamp = [
  { name: "Display", size: 68, weight: "semibold" },
  { name: "Large Title", size: 40, weight: "semibold" },
  { name: "Title 1", size: 32, weight: "semibold" },
  { name: "Title 2", size: 28, weight: "semibold" },
  { name: "Title 3", size: 24, weight: "semibold" },
  { name: "Subtitle", size: 20, weight: "semibold" },
  { name: "Body 2", size: 16, weight: "regular" },
  { name: "Body 1", size: 14, weight: "regular" },
  { name: "Caption", size: 12, weight: "regular" },
  { name: "Caption 2", size: 10, weight: "regular" },
];

const spacingScale = [
  { name: "xxs", value: 2 },
  { name: "xs", value: 4 },
  { name: "sNudge", value: 6 },
  { name: "s", value: 8 },
  { name: "mNudge", value: 10 },
  { name: "m", value: 12 },
  { name: "l", value: 16 },
  { name: "xl", value: 20 },
  { name: "xxl", value: 24 },
  { name: "xxxl", value: 32 },
];

const radii = [
  { name: "small", value: 2 },
  { name: "medium", value: 4 },
  { name: "large", value: 6 },
  { name: "xLarge", value: 8 },
  { name: "xxLarge", value: 12 },
  { name: "circular", value: 9999 },
];

const shadows = [
  { name: "shadow2", blur: 2, offset: 1 },
  { name: "shadow4", blur: 4, offset: 2 },
  { name: "shadow8", blur: 8, offset: 4 },
  { name: "shadow16", blur: 16, offset: 8 },
  { name: "shadow28", blur: 28, offset: 14 },
  { name: "shadow64", blur: 64, offset: 32 },
];

export function DesignSystem() {
  const styles = useStyles();
  const { t } = useTranslation();

  return (
    <div className={styles.page}>
      <h1 className={styles.title}>{t("ds.title")}</h1>
      <p className={styles.subtitle}>{t("ds.subtitle")}</p>

      <div className={styles.section}>
        <h2 className={styles.sectionTitle}>{t("ds.brandColors")}</h2>
        <div className={styles.colorGrid}>
          {brandColors.map((c) => (
            <Card key={c.name} className={styles.colorCard}>
              <div className={styles.colorSwatch} style={{ backgroundColor: c.hex }} />
              <div className={styles.colorInfo}>
                <div className={styles.colorName}>{c.name}</div>
                <div className={styles.colorHex}>{c.hex}</div>
              </div>
            </Card>
          ))}
        </div>
      </div>

      <div className={styles.section}>
        <h2 className={styles.sectionTitle}>{t("ds.statusColors")}</h2>
        <div className={styles.colorGrid}>
          {statusColors.map((c) => (
            <Card key={c.name} className={styles.colorCard}>
              <div className={styles.colorSwatch} style={{ backgroundColor: c.hex }} />
              <div className={styles.colorInfo}>
                <div className={styles.colorName}>{c.name}</div>
                <div className={styles.colorHex}>{c.hex}</div>
              </div>
            </Card>
          ))}
        </div>
      </div>

      <Divider />

      <div className={styles.section} style={{ marginTop: "40px" }}>
        <h2 className={styles.sectionTitle}>{t("ds.typography")}</h2>
        {typeRamp.map((item) => (
          <div key={item.name} className={styles.typeRow}>
            <span
              className={styles.typeSample}
              style={{
                fontSize: `${Math.min(item.size, 48)}px`,
                fontWeight: item.weight === "semibold" ? 600 : 400,
              }}
            >
              {item.name}
            </span>
            <span className={styles.typeMeta}>
              {item.size}px / {item.weight}
            </span>
          </div>
        ))}
      </div>

      <Divider />

      <div className={styles.section} style={{ marginTop: "40px" }}>
        <h2 className={styles.sectionTitle}>{t("ds.spacing")}</h2>
        {spacingScale.map((s) => (
          <div key={s.name} className={styles.spacingRow}>
            <span className={styles.spacingLabel}>{s.name}</span>
            <div className={styles.spacingBar} style={{ width: `${s.value * 4}px` }} />
            <span className={styles.spacingValue}>{s.value}px</span>
          </div>
        ))}
      </div>

      <Divider />

      <div className={styles.section} style={{ marginTop: "40px" }}>
        <h2 className={styles.sectionTitle}>{t("ds.radius")}</h2>
        <div className={styles.radiusGrid}>
          {radii.map((r) => (
            <div key={r.name} className={styles.radiusItem}>
              <div
                className={styles.radiusBox}
                style={{ borderRadius: r.value === 9999 ? "50%" : `${r.value}px` }}
              />
              <span className={styles.radiusLabel}>{r.name}</span>
              <span className={styles.radiusValue}>{r.value === 9999 ? "circular" : `${r.value}px`}</span>
            </div>
          ))}
        </div>
      </div>

      <Divider />

      <div className={styles.section} style={{ marginTop: "40px" }}>
        <h2 className={styles.sectionTitle}>{t("ds.shadow")}</h2>
        <Text size={200} style={{ color: tokens.colorNeutralForeground3, display: "block", marginBottom: "20px" }}>
          {t("ds.shadowDesc")}
        </Text>
        <div className={styles.shadowGrid}>
          {shadows.map((s) => (
            <div key={s.name} className={styles.shadowItem}>
              <div
                className={styles.shadowBox}
                style={{
                  boxShadow: `0 0 2px rgba(0,0,0,0.12), 0 ${s.offset}px ${s.blur}px rgba(0,0,0,0.14)`,
                }}
              />
              <span className={styles.radiusLabel}>{s.name}</span>
              <span className={styles.radiusValue}>blur {s.blur}px</span>
            </div>
          ))}
        </div>
      </div>
    </div>
  );
}
