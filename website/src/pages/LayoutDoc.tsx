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

const vboxExample = `// C API
UiWidget col = ui_vbox();
ui_widget_set_padding_uniform(col, 16);
ui_widget_set_gap(col, 12);
ui_widget_add_child(col, ui_label(L"Top"));
ui_widget_add_child(col, ui_label(L"Middle"));
ui_widget_add_child(col, ui_label(L"Bottom"));

// .ui Markup
// <VBox padding="16" gap="12">
//   <Label text="Top" />
//   <Label text="Middle" />
//   <Label text="Bottom" />
// </VBox>`;

const flexExample = `// expand makes a widget fill remaining space
UiWidget content = ui_vbox();
ui_widget_set_expand(content, 1);  // flex: 1

// In markup:
// <VBox>
//   <Label text="Header" height="48" />
//   <VBox expand="true">  <!-- fills remaining space -->
//     <Label text="Content" />
//   </VBox>
//   <Label text="Footer" height="32" />
// </VBox>`;

const gridExample = `// Grid layout (not yet in C API, markup only)
// <Grid cols="3" colGap="8" rowGap="8" padding="16">
//   <Label text="1" />
//   <Label text="2" />
//   <Label text="3" />
//   <Label text="4" colspan="2" />  <!-- spans 2 columns -->
//   <Label text="5" />
// </Grid>`;

const splitViewExample = `// SplitView with sidebar navigation
// <SplitView mode="compactInline" paneWidth="200">
//   <SplitView.Pane>
//     <NavItem icon="{homeIcon}" text="Home" />
//     <NavItem icon="{settingsIcon}" text="Settings" />
//   </SplitView.Pane>
//   <SplitView.Content>
//     <Stack active="0">
//       <VBox> <!-- Home page --> </VBox>
//       <VBox> <!-- Settings page --> </VBox>
//     </Stack>
//   </SplitView.Content>
// </SplitView>`;

const splitterExample = `// Splitter: draggable divider between two panels
// <Splitter ratio="0.3" direction="horizontal">
//   <VBox>  <!-- Left panel, 30% --> </VBox>
//   <VBox>  <!-- Right panel, 70% --> </VBox>
// </Splitter>`;

const scrollExample = `// ScrollView with auto-scrollbar
UiWidget scroll = ui_scroll_view();
UiWidget content = ui_vbox();
ui_widget_set_gap(content, 8);
for (int i = 0; i < 100; i++) {
    ui_widget_add_child(content, ui_label(L"Item"));
}
ui_scroll_set_content(scroll, content);
ui_widget_set_expand(scroll, 1);`;

export function LayoutDoc() {
  const styles = useStyles();
  const { t } = useTranslation();

  return (
    <div className={styles.page}>
      <h1 className={styles.title}>{t("layout.title")}</h1>
      <p className={styles.subtitle}>{t("layout.subtitle")}</p>

      <div className={styles.section}>
        <h2 className={styles.sectionTitle}>{t("layout.vboxHboxTitle")}</h2>
        <p className={styles.paragraph}>{t("layout.flexDesc")}</p>
        <CodeBlock code={vboxExample} language="C" />
      </div>

      <div className={styles.section}>
        <h2 className={styles.sectionTitle}>{t("layout.flexTitle")}</h2>
        <p className={styles.paragraph}>{t("layout.flexExpandDesc")}</p>
        <CodeBlock code={flexExample} language="C" />
      </div>

      <div className={styles.section}>
        <h2 className={styles.sectionTitle}>{t("layout.gridTitle")}</h2>
        <p className={styles.paragraph}>{t("layout.gridDesc")}</p>
        <CodeBlock code={gridExample} language=".ui Markup" />
      </div>

      <div className={styles.section}>
        <h2 className={styles.sectionTitle}>{t("layout.splitViewTitle")}</h2>
        <p className={styles.paragraph}>{t("layout.splitViewDesc")}</p>
        <CodeBlock code={splitViewExample} language=".ui Markup" />
      </div>

      <div className={styles.section}>
        <h2 className={styles.sectionTitle}>{t("layout.splitterTitle")}</h2>
        <p className={styles.paragraph}>{t("layout.splitterDesc")}</p>
        <CodeBlock code={splitterExample} language=".ui Markup" />
      </div>

      <div className={styles.section}>
        <h2 className={styles.sectionTitle}>{t("layout.scrollTitle")}</h2>
        <p className={styles.paragraph}>{t("layout.scrollDesc")}</p>
        <CodeBlock code={scrollExample} language="C" />
      </div>
    </div>
  );
}
