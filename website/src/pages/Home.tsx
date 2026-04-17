import { makeStyles, tokens, Button, Text, Divider } from "@fluentui/react-components";
import { ArrowRightRegular, OpenRegular } from "@fluentui/react-icons";
import { Link } from "react-router";
import { useTranslation } from "react-i18next";
import { FeatureCard } from "../components/FeatureCard";
import { CodeBlock } from "../components/CodeBlock";
import { features } from "../data/features";
import { controls } from "../data/controls";
import { totalFunctions } from "../data/api-functions";

const useStyles = makeStyles({
  heroWrap: {
    paddingBlock: "72px 56px",
    textAlign: "center",
  },
  heroContent: {
    maxWidth: "660px",
    marginInline: "auto",
  },
  badge: {
    display: "inline-flex",
    alignItems: "center",
    gap: "6px",
    fontSize: "12px",
    fontWeight: 600,
    color: tokens.colorBrandForeground1,
    backgroundColor: tokens.colorBrandBackground2,
    paddingBlock: "5px",
    paddingInline: "14px",
    borderRadius: "20px",
    marginBottom: "20px",
    letterSpacing: "0.2px",
  },
  badgeDot: {
    width: "6px",
    height: "6px",
    borderRadius: "50%",
    backgroundColor: tokens.colorBrandForeground1,
  },
  title: {
    fontSize: "44px",
    fontWeight: 700,
    lineHeight: 1.15,
    letterSpacing: "-1px",
    color: tokens.colorNeutralForeground1,
    marginBottom: "20px",
    whiteSpace: "nowrap",
    "@media (max-width: 768px)": {
      fontSize: "28px",
      whiteSpace: "normal",
    },
  },
  subtitle: {
    fontSize: "17px",
    lineHeight: 1.65,
    color: tokens.colorNeutralForeground2,
    marginBottom: "36px",
    maxWidth: "560px",
    marginInline: "auto",
  },
  actions: {
    display: "flex",
    gap: "12px",
    justifyContent: "center",
    flexWrap: "wrap",
  },
  primaryBtn: {
    borderRadius: "10px",
    fontWeight: 600,
    paddingInline: "24px",
    height: "42px",
  },
  outlineBtn: {
    borderRadius: "10px",
    fontWeight: 600,
    paddingInline: "24px",
    height: "42px",
  },
  stats: {
    display: "flex",
    gap: "1px",
    justifyContent: "center",
    paddingBlock: "40px",
    backgroundColor: tokens.colorNeutralBackground2,
    borderRadius: "16px",
    marginBlock: "40px",
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
    overflow: "hidden",
  },
  stat: {
    flex: 1,
    textAlign: "center",
    paddingBlock: "20px",
  },
  statValue: {
    fontSize: "32px",
    fontWeight: 800,
    letterSpacing: "-0.5px",
    lineHeight: 1.2,
    color: tokens.colorBrandForeground1,
  },
  statLabel: {
    fontSize: "13px",
    color: tokens.colorNeutralForeground3,
    marginTop: "4px",
    fontWeight: 500,
  },
  section: {
    paddingBlock: "40px",
  },
  sectionHeader: {
    marginBottom: "28px",
  },
  sectionTitle: {
    fontSize: "26px",
    fontWeight: 700,
    color: tokens.colorNeutralForeground1,
    letterSpacing: "-0.5px",
    marginBottom: "8px",
  },
  sectionDesc: {
    color: tokens.colorNeutralForeground3,
    fontSize: "15px",
    lineHeight: 1.6,
  },
  featureGrid: {
    display: "grid",
    gridTemplateColumns: "repeat(auto-fill, minmax(280px, 1fr))",
    gap: "14px",
  },
  codeGrid: {
    display: "grid",
    gridTemplateColumns: "1fr 1fr",
    gap: "14px",
    "@media (max-width: 768px)": {
      gridTemplateColumns: "1fr",
    },
  },
});

const cExample = `#include "ui_core.h"

int main() {
    ui_init();

    UiWindowConfig cfg = {
        .title = L"Hello UI Core",
        .width = 800, .height = 600
    };
    UiWindow win = ui_window_create(&cfg);

    UiWidget root = ui_vbox();
    ui_widget_set_padding_uniform(root, 16);
    ui_widget_set_gap(root, 12);

    UiWidget label = ui_label(L"Hello, World!");
    ui_label_set_font_size(label, 24);
    ui_widget_add_child(root, label);

    UiWidget btn = ui_button(L"Click Me");
    ui_widget_on_click(btn, on_click, NULL);
    ui_widget_add_child(root, btn);

    ui_window_set_root(win, root);
    ui_window_show(win);
    return ui_run();
}`;

const markupExample = `<!-- app.ui -->
<VBox padding="16" gap="12">
  <Label text="Hello, World!"
         fontSize="24"
         bold="true" />

  <HBox gap="8">
    <Button text="Default" />
    <Button text="Primary" type="primary" />
  </HBox>

  <CheckBox text="Enable feature"
            checked="true" />

  <Slider min="0" max="100" value="50" />

  <TextInput placeholder="Type here..."
             width="300" />
</VBox>`;

export function Home() {
  const styles = useStyles();
  const { t } = useTranslation();

  const controlCount = `${controls.length}+`;

  return (
    <div>
      <div className={styles.heroWrap}>
        <div className={styles.heroContent}>
          <div className={styles.badge}>
            <span className={styles.badgeDot} />
            {t("home.badge")}
          </div>
          <h1 className={styles.title}>{t("home.title")}</h1>
          <p className={styles.subtitle}>{t("home.subtitle")}</p>
          <div className={styles.actions}>
            <Button
              appearance="primary"
              size="large"
              icon={<ArrowRightRegular />}
              iconPosition="after"
              className={styles.primaryBtn}
              as={Link}
              // @ts-expect-error Link props
              to="/docs/getting-started"
            >
              {t("home.getStarted")}
            </Button>
            <Button
              appearance="outline"
              size="large"
              icon={<OpenRegular />}
              iconPosition="after"
              className={styles.outlineBtn}
              as="a"
              href="https://github.com"
              target="_blank"
            >
              {t("home.viewGithub")}
            </Button>
          </div>
        </div>
      </div>

      <div className={styles.stats}>
        {[
          { value: controlCount, label: t("home.stats.controls") },
          { value: String(totalFunctions), label: t("home.stats.apiFunctions") },
          { value: "D2D", label: t("home.stats.gpuAccelerated") },
          { value: "DPI", label: t("home.stats.perMonitor") },
        ].map((s) => (
          <div key={s.label} className={styles.stat}>
            <div className={styles.statValue}>{s.value}</div>
            <div className={styles.statLabel}>{s.label}</div>
          </div>
        ))}
      </div>

      <Divider />

      <div className={styles.section}>
        <div className={styles.sectionHeader}>
          <Text className={styles.sectionTitle} as="h2" block>{t("home.whyTitle")}</Text>
          <Text className={styles.sectionDesc} block>{t("home.whySubtitle")}</Text>
        </div>
        <div className={styles.featureGrid}>
          {features.map((f) => (
            <FeatureCard key={f.titleKey} feature={f} />
          ))}
        </div>
      </div>

      <Divider />

      <div className={styles.section}>
        <div className={styles.sectionHeader}>
          <Text className={styles.sectionTitle} as="h2" block>{t("home.codeTitle")}</Text>
          <Text className={styles.sectionDesc} block>{t("home.codeSubtitle")}</Text>
        </div>
        <div className={styles.codeGrid}>
          <CodeBlock code={cExample} language="C API" />
          <CodeBlock code={markupExample} language=".ui Markup" />
        </div>
      </div>
    </div>
  );
}
