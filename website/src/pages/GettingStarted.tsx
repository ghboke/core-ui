import { makeStyles, tokens, MessageBar, MessageBarBody } from "@fluentui/react-components";
import { useTranslation } from "react-i18next";
import { CodeBlock } from "../components/CodeBlock";

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
    marginBottom: "32px",
  },
  sectionTitle: {
    fontSize: "20px",
    fontWeight: 600,
    color: tokens.colorNeutralForeground1,
    marginBottom: "12px",
  },
  paragraph: {
    color: tokens.colorNeutralForeground2,
    lineHeight: "24px",
    marginBottom: "16px",
  },
  list: {
    color: tokens.colorNeutralForeground2,
    lineHeight: "28px",
    paddingInlineStart: "24px",
    marginBottom: "16px",
  },
});

const buildCode = `# Clone the repository
git clone https://github.com/user/core-ui.git
cd core-ui

# Build with CMake
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Output: build/Release/ui-core.dll + ui-core.lib`;

const minimalExample = `#include "ui_core.h"

void on_click(UiWidget w, void* data) {
    UiWindow win = (UiWindow)(uintptr_t)data;
    ui_toast(win, L"Button clicked!", 2000);
}

int main() {
    ui_init();  // Initialize with dark theme

    UiWindowConfig cfg = {
        .title = L"My First App",
        .width = 640,
        .height = 480,
    };
    UiWindow win = ui_window_create(&cfg);

    // Build UI tree
    UiWidget root = ui_vbox();
    ui_widget_set_padding(root, 24);
    ui_widget_set_gap(root, 16);

    UiWidget title = ui_label(L"Welcome to UI Core");
    ui_label_set_font_size(title, 28);
    ui_label_set_bold(title, true);
    ui_widget_add_child(root, title);

    UiWidget btn = ui_button(L"Say Hello");
    ui_widget_on_click(btn, on_click, (void*)(uintptr_t)win);
    ui_widget_add_child(root, btn);

    ui_window_set_root(win, root);
    ui_window_show(win);
    return ui_run();
}`;

const markupWay = `<!-- app.ui -->
<VBox padding="24" gap="16">
  <Label text="Welcome to UI Core"
         fontSize="28"
         bold="true" />

  <Button text="Say Hello"
          onClick="onHello" />

  <HBox gap="8" align="center">
    <CheckBox text="Dark Mode"
              id="themeToggle" />
    <Toggle id="featureFlag" />
  </HBox>
</VBox>`;

const cmakeIntegration = `# CMakeLists.txt
find_package(ui-core REQUIRED)

add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE ui-core::ui-core)`;

export function GettingStarted() {
  const styles = useStyles();
  const { t } = useTranslation();

  return (
    <div className={styles.page}>
      <h1 className={styles.title}>{t("gs.title")}</h1>
      <p className={styles.subtitle}>{t("gs.subtitle")}</p>

      <MessageBar intent="info" style={{ marginBottom: "24px" }}>
        <MessageBarBody>{t("gs.requirements")}</MessageBarBody>
      </MessageBar>

      <div className={styles.section}>
        <h2 className={styles.sectionTitle}>{t("gs.buildTitle")}</h2>
        <CodeBlock code={buildCode} language="Shell" />
      </div>

      <div className={styles.section}>
        <h2 className={styles.sectionTitle}>{t("gs.cmakeTitle")}</h2>
        <p className={styles.paragraph}>{t("gs.cmakeDesc")}</p>
        <CodeBlock code={cmakeIntegration} language="CMake" />
      </div>

      <div className={styles.section}>
        <h2 className={styles.sectionTitle}>{t("gs.minimalTitle")}</h2>
        <p className={styles.paragraph}>{t("gs.minimalDesc")}</p>
        <CodeBlock code={minimalExample} language="C" />
      </div>

      <div className={styles.section}>
        <h2 className={styles.sectionTitle}>{t("gs.markupTitle")}</h2>
        <p className={styles.paragraph}>{t("gs.markupDesc")}</p>
        <CodeBlock code={markupWay} language=".ui Markup" />
      </div>

      <div className={styles.section}>
        <h2 className={styles.sectionTitle}>{t("gs.distTitle")}</h2>
        <ul className={styles.list}>
          <li><strong>{t("gs.distDynamic")}</strong> {t("gs.distDynamicDesc")}</li>
          <li><strong>{t("gs.distStatic")}</strong> {t("gs.distStaticDesc")}</li>
        </ul>
      </div>

      <div className={styles.section}>
        <h2 className={styles.sectionTitle}>{t("gs.depsTitle")}</h2>
        <p className={styles.paragraph}>{t("gs.depsDesc")}</p>
        <ul className={styles.list}>
          <li>Direct2D, Direct3D 11, DXGI, DirectWrite</li>
          <li>DWM, UxTheme, WindowsCodecs</li>
          <li>GDI+ (for image streaming mode)</li>
        </ul>
      </div>
    </div>
  );
}
