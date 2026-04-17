import { makeStyles, tokens } from "@fluentui/react-components";
import { useTranslation } from "react-i18next";
import { CodeBlock } from "../components/CodeBlock";

const useStyles = makeStyles({
  page: { maxWidth: "880px" },
  title: { fontSize: "32px", fontWeight: 700, color: tokens.colorNeutralForeground1, marginBottom: "8px" },
  subtitle: { color: tokens.colorNeutralForeground2, marginBottom: "32px", lineHeight: "24px" },
  section: { marginBottom: "32px" },
  sectionTitle: { fontSize: "20px", fontWeight: 600, color: tokens.colorNeutralForeground1, marginBottom: "12px" },
  paragraph: { color: tokens.colorNeutralForeground2, lineHeight: "24px", marginBottom: "16px" },
});

const basicMarkup = `<VBox padding="16" gap="12">
  <Label text="Hello, World!" fontSize="24" bold="true" />
  <Button text="Click Me" onClick="handleClick" />
  <HBox gap="8">
    <CheckBox text="Option A" />
    <CheckBox text="Option B" checked="true" />
  </HBox>
</VBox>`;

const styleExample = `<Style>
  .card {
    bgColor: #2d2d2d;
    padding: 16;
    radius: 8;
  }
  Button:hover {
    bgColor: #3a3a3a;
  }
  Button.primary {
    bgColor: #0f6cbd;
  }
</Style>

<VBox class="card">
  <Label text="Styled Card" />
  <Button text="Primary" class="primary" />
</VBox>`;

const dataBinding = `<VBox padding="16" gap="8">
  <Label text="{title}" fontSize="20" bold="true" />
  <Label text="Count: {count}" />
  <Slider min="0" max="100" value="{sliderValue}" />
  <ProgressBar value="{progress}" />
</VBox>

<!-- In C++ code:
  markup->SetText("title", L"My App");
  markup->SetFloat("sliderValue", 50.0f);
  markup->SetFloat("progress", 0.75f);
-->`;

const includeExample = `<!-- header.ui -->
<HBox gap="8" align="center" height="48" padding="0 16">
  <Label text="{appTitle}" fontSize="16" bold="true" />
  <Spacer expand="true" />
  <IconButton svg="{settingsIcon}" ghost="true" />
</HBox>

<!-- app.ui -->
<VBox>
  <Include src="header.ui" appTitle="My App" />
  <Separator />
  <VBox expand="true" padding="16">
    <Label text="Content goes here" />
  </VBox>
</VBox>`;

const repeaterExample = `<VBox padding="16" gap="8">
  <Label text="Todo List" fontSize="20" bold="true" />
  <Repeater model="{todos}">
    <HBox gap="8" align="center">
      <CheckBox text="{item.text}" checked="{item.done}" />
      <Spacer expand="true" />
      <IconButton svg="{deleteIcon}" ghost="true" />
    </HBox>
  </Repeater>
</VBox>`;

const hotReload = `// Enable hot reload in debug builds
// Changes to .ui files are detected and applied instantly
// No recompilation needed — just save the file

UiWindow win = ui_window_create(&cfg);
markup_load_file(win, L"app.ui");  // watches for changes
ui_window_show(win);`;

export function Markup() {
  const styles = useStyles();
  const { t } = useTranslation();

  return (
    <div className={styles.page}>
      <h1 className={styles.title}>{t("markup.title")}</h1>
      <p className={styles.subtitle}>{t("markup.subtitle")}</p>

      <div className={styles.section}>
        <h2 className={styles.sectionTitle}>{t("markup.basicTitle")}</h2>
        <p className={styles.paragraph}>{t("markup.basicDesc")}</p>
        <CodeBlock code={basicMarkup} language=".ui Markup" />
      </div>

      <div className={styles.section}>
        <h2 className={styles.sectionTitle}>{t("markup.styleTitle")}</h2>
        <p className={styles.paragraph}>{t("markup.styleDesc")}</p>
        <CodeBlock code={styleExample} language=".ui Markup" />
      </div>

      <div className={styles.section}>
        <h2 className={styles.sectionTitle}>{t("markup.bindingTitle")}</h2>
        <p className={styles.paragraph}>{t("markup.bindingDesc")}</p>
        <CodeBlock code={dataBinding} language=".ui Markup" />
      </div>

      <div className={styles.section}>
        <h2 className={styles.sectionTitle}>{t("markup.includeTitle")}</h2>
        <p className={styles.paragraph}>{t("markup.includeDesc")}</p>
        <CodeBlock code={includeExample} language=".ui Markup" />
      </div>

      <div className={styles.section}>
        <h2 className={styles.sectionTitle}>{t("markup.repeaterTitle")}</h2>
        <p className={styles.paragraph}>{t("markup.repeaterDesc")}</p>
        <CodeBlock code={repeaterExample} language=".ui Markup" />
      </div>

      <div className={styles.section}>
        <h2 className={styles.sectionTitle}>{t("markup.hotReloadTitle")}</h2>
        <p className={styles.paragraph}>{t("markup.hotReloadDesc")}</p>
        <CodeBlock code={hotReload} language="C" />
      </div>
    </div>
  );
}
