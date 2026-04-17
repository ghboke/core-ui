import { useParams, Link } from "react-router";
import { makeStyles, tokens, Text, Badge, Breadcrumb, BreadcrumbItem, BreadcrumbButton, BreadcrumbDivider, Card } from "@fluentui/react-components";
import { useTranslation } from "react-i18next";
import { controls } from "../data/controls";
import { apiGroups } from "../data/api-functions";
import { CodeBlock } from "../components/CodeBlock";
import { ControlPreview } from "../components/ControlPreview";

const useStyles = makeStyles({
  page: {
    maxWidth: "880px",
  },
  header: {
    marginBottom: "32px",
  },
  title: {
    fontSize: "32px",
    fontWeight: 700,
    color: tokens.colorNeutralForeground1,
    marginBottom: "8px",
    display: "flex",
    alignItems: "center",
    gap: "12px",
  },
  description: {
    color: tokens.colorNeutralForeground2,
    lineHeight: "24px",
    marginBottom: "16px",
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
  featureList: {
    display: "flex",
    flexWrap: "wrap",
    gap: "8px",
    marginBottom: "16px",
  },
  featureTag: {
    fontSize: "13px",
    color: tokens.colorNeutralForeground2,
    backgroundColor: tokens.colorNeutralBackground3,
    padding: "4px 10px",
    borderRadius: tokens.borderRadiusMedium,
  },
  apiCard: {
    padding: "12px 16px",
    marginBottom: "8px",
  },
  apiName: {
    fontFamily: "Consolas, 'Courier New', monospace",
    fontSize: "13px",
    fontWeight: 600,
    color: tokens.colorBrandForeground1,
  },
  apiSig: {
    fontFamily: "Consolas, 'Courier New', monospace",
    fontSize: "12px",
    color: tokens.colorNeutralForeground3,
    marginTop: "2px",
  },
  apiDesc: {
    fontSize: "13px",
    color: tokens.colorNeutralForeground2,
    marginTop: "4px",
  },
  notFound: {
    textAlign: "center",
    paddingBlock: "64px",
    color: tokens.colorNeutralForeground3,
  },
});

const categoryColors: Record<string, "brand" | "success" | "warning" | "informative"> = {
  container: "informative",
  input: "brand",
  display: "success",
  navigation: "warning",
};

function getRelatedApiFunctions(controlName: string) {
  const nameMap: Record<string, string[]> = {
    "VBox": ["ui_vbox"],
    "HBox": ["ui_hbox"],
    "Panel": ["ui_panel", "ui_panel_themed"],
    "Spacer": ["ui_spacer"],
    "ScrollView": ["ui_scroll_view", "ui_scroll_set_content"],
    "TabControl": ["ui_tab_control", "ui_tab_add", "ui_tab_get_active", "ui_tab_set_active"],
    "Label": ["ui_label", "ui_label_set_text", "ui_label_set_font_size", "ui_label_set_bold", "ui_label_set_wrap", "ui_label_set_max_lines", "ui_label_set_text_color", "ui_label_set_align"],
    "Button": ["ui_button", "ui_button_set_font_size", "ui_button_set_type", "ui_button_set_text_color", "ui_button_set_bg_color"],
    "CheckBox": ["ui_checkbox", "ui_checkbox_get_checked", "ui_checkbox_set_checked", "ui_checkbox_on_changed"],
    "RadioButton": ["ui_radio_button", "ui_radio_get_selected", "ui_radio_set_selected"],
    "Toggle": ["ui_toggle", "ui_toggle_get_on", "ui_toggle_set_on", "ui_toggle_on_changed"],
    "Slider": ["ui_slider", "ui_slider_get_value", "ui_slider_set_value", "ui_slider_on_changed"],
    "TextInput": ["ui_text_input", "ui_text_input_get_text", "ui_text_input_set_text", "ui_text_input_set_read_only"],
    "TextArea": ["ui_text_area", "ui_text_area_get_text", "ui_text_area_set_text", "ui_text_area_set_read_only"],
    "ComboBox": ["ui_combobox", "ui_combobox_get_selected", "ui_combobox_set_selected", "ui_combobox_on_changed"],
    "ProgressBar": ["ui_progress_bar", "ui_progress_get_value", "ui_progress_set_value"],
    "ImageView": ["ui_image_view", "ui_image_load_file", "ui_image_set_pixels", "ui_image_clear", "ui_image_get_zoom", "ui_image_set_zoom", "ui_image_fit", "ui_image_reset", "ui_image_set_rotation", "ui_image_set_checkerboard"],
    "IconButton": ["ui_icon_button", "ui_icon_button_set_svg", "ui_icon_button_set_ghost", "ui_icon_button_set_icon_color", "ui_icon_button_set_icon_padding"],
    "TitleBar": ["ui_titlebar", "ui_titlebar_set_title", "ui_titlebar_show_buttons", "ui_titlebar_show_icon", "ui_titlebar_set_bg_color", "ui_titlebar_add_widget"],
    "Dialog": ["ui_dialog", "ui_dialog_show", "ui_dialog_hide", "ui_dialog_set_ok_text", "ui_dialog_set_cancel_text", "ui_dialog_set_show_cancel"],
    "Toast": ["ui_toast", "ui_toast_at", "ui_toast_ex"],
    "ContextMenu": ["ui_menu_create", "ui_menu_destroy", "ui_menu_add_item", "ui_menu_add_item_ex", "ui_menu_add_separator", "ui_menu_add_submenu", "ui_menu_set_enabled", "ui_menu_show", "ui_menu_close"],
    "Separator": ["ui_separator", "ui_vseparator"],
  };

  const explicitNames = nameMap[controlName];
  if (explicitNames) {
    const allFns = apiGroups.flatMap((g) => g.functions);
    return allFns.filter((f) => explicitNames.includes(f.name));
  }
  return [];
}

interface CodeExample {
  code: string;
  lang: "C" | ".ui Markup" | "C++";
}

function getExamples(controlName: string): CodeExample[] {
  const map: Record<string, CodeExample[]> = {
    "Button": [
      { lang: ".ui Markup", code: `<Button text="Cancel" width="100" onClick="onCancel" />

<!-- Primary (accent filled) -->
<Button text="Save" type="primary" width="100" onClick="onSave" />

<!-- Custom color -->
<Button text="Delete" bgColor="0.8,0.2,0.2,1" textColor="1,1,1,1" />` },
      { lang: "C", code: `UiWidget btn = ui_button(L"Click Me");
ui_button_set_type(btn, 1);  // 0=default, 1=primary
ui_widget_on_click(btn, my_callback, NULL);
ui_widget_add_child(root, btn);` },
    ],
    "Label": [
      { lang: ".ui Markup", code: `<Label text="Title" fontSize="20" bold="true" />
<Label text="Body text with wrapping" wrap="true" maxLines="3" />
<Label text="Centered" align="center" textColor="theme.accent" />` },
      { lang: "C", code: `UiWidget title = ui_label(L"Hello World");
ui_label_set_font_size(title, 24);
ui_label_set_bold(title, 1);
ui_label_set_align(title, 2);  // 0=left 1=right 2=center
ui_widget_add_child(root, title);` },
    ],
    "CheckBox": [
      { lang: ".ui Markup", code: `<CheckBox text="Enable feature" checked="true" onChanged="onToggle" />` },
      { lang: "C", code: `UiWidget cb = ui_checkbox(L"Enable feature");
ui_checkbox_set_checked(cb, 1);
ui_checkbox_on_changed(cb, on_check, NULL);
ui_widget_add_child(root, cb);` },
    ],
    "RadioButton": [
      { lang: ".ui Markup", code: `<RadioButton text="Option A" group="myGroup" selected="true" />
<RadioButton text="Option B" group="myGroup" />
<RadioButton text="Option C" group="myGroup" />` },
      { lang: "C", code: `UiWidget r1 = ui_radio_button(L"Option A", "group1");
UiWidget r2 = ui_radio_button(L"Option B", "group1");
UiWidget r3 = ui_radio_button(L"Option C", "group1");
ui_widget_add_child(root, r1);
ui_widget_add_child(root, r2);
ui_widget_add_child(root, r3);` },
    ],
    "Toggle": [
      { lang: ".ui Markup", code: `<Toggle text="Dark Mode" on="true" onChanged="onDarkMode" />` },
      { lang: "C", code: `UiWidget toggle = ui_toggle(L"Dark Mode");
ui_toggle_set_on(toggle, 1);
ui_toggle_on_changed(toggle, on_toggle, NULL);
ui_widget_add_child(root, toggle);` },
    ],
    "Slider": [
      { lang: ".ui Markup", code: `<Slider min="0" max="100" value="50" onChanged="onVolume" expand="true" />` },
      { lang: "C", code: `UiWidget slider = ui_slider(0, 100, 50);
ui_slider_on_changed(slider, on_value, NULL);
ui_widget_add_child(root, slider);` },
    ],
    "TextInput": [
      { lang: ".ui Markup", code: `<TextInput placeholder="Enter your name..." expand="true" />` },
      { lang: "C", code: `UiWidget input = ui_text_input(L"Type here...");
ui_widget_set_width(input, 300);
ui_widget_add_child(root, input);

// Get text (internal pointer, do not free)
const wchar_t* text = ui_text_input_get_text(input);` },
    ],
    "TextArea": [
      { lang: ".ui Markup", code: `<TextArea placeholder="Type notes..." height="120" expand="true" />` },
      { lang: "C", code: `UiWidget area = ui_text_area(L"Enter description...");
ui_widget_set_width(area, 400);
ui_widget_set_height(area, 200);
ui_widget_add_child(root, area);` },
    ],
    "ComboBox": [
      { lang: ".ui Markup", code: `<ComboBox items="Dark,Light,System" selected="0"
          onChanged="onThemeSelect" width="180" />` },
      { lang: "C", code: `const wchar_t* items[] = { L"Red", L"Green", L"Blue" };
UiWidget combo = ui_combobox(items, 3);
ui_combobox_on_changed(combo, on_select, NULL);
ui_widget_add_child(root, combo);` },
    ],
    "NumberBox": [
      { lang: ".ui Markup", code: `<NumberBox min="0" max="100" value="42" step="1" width="120" />
<NumberBox min="0" max="1" value="0.5" step="0.1" decimals="2" width="120" />` },
    ],
    "ProgressBar": [
      { lang: ".ui Markup", code: `<ProgressBar min="0" max="100" value="73" expand="true" />
<ProgressBar indeterminate="true" expand="true" />` },
      { lang: "C", code: `UiWidget bar = ui_progress_bar(0, 100, 0);
ui_widget_set_width(bar, 300);
ui_widget_add_child(root, bar);

// Update progress
ui_progress_set_value(bar, 75.0f);` },
    ],
    "ImageView": [
      { lang: "C", code: `UiWidget img = ui_image_view();
ui_image_load_file(img, win, L"photo.png");
ui_image_set_checkerboard(img, 1);
ui_image_fit(img);
ui_widget_set_expand(img, 1);
ui_widget_add_child(root, img);` },
    ],
    "Separator": [
      { lang: ".ui Markup", code: `<Separator />
<Separator vertical="true" />` },
      { lang: "C", code: `UiWidget sep = ui_separator();
ui_widget_add_child(root, sep);

UiWidget vsep = ui_vseparator();
ui_widget_add_child(hbox, vsep);` },
    ],
    "VBox": [
      { lang: ".ui Markup", code: `<VBox gap="12" padding="16" align="center" expand="true">
  <Label text="Item 1" />
  <Label text="Item 2" />
  <Label text="Item 3" />
</VBox>` },
      { lang: "C", code: `UiWidget col = ui_vbox();
ui_widget_set_padding_uniform(col, 16);
ui_widget_set_gap(col, 12);
ui_widget_add_child(col, ui_label(L"Item 1"));
ui_widget_add_child(col, ui_label(L"Item 2"));
ui_window_set_root(win, col);` },
    ],
    "HBox": [
      { lang: ".ui Markup", code: `<HBox gap="8" align="center">
  <Button text="OK" />
  <Button text="Cancel" />
  <Spacer expand="true" />
  <Label text="Status" />
</HBox>` },
      { lang: "C", code: `UiWidget row = ui_hbox();
ui_widget_set_gap(row, 8);
ui_widget_add_child(row, ui_button(L"OK"));
ui_widget_add_child(row, ui_button(L"Cancel"));
ui_widget_add_child(root, row);` },
    ],
    "Grid": [
      { lang: ".ui Markup", code: `<!-- cols: column count, children auto-fill left to right -->
<Grid cols="2" colGap="12" rowGap="8" padding="16">
  <Label text="Name" />
  <TextInput placeholder="..." />
  <Label text="Email" />
  <TextInput placeholder="..." />
  <Label text="Notes" />
  <TextArea placeholder="..." colspan="2" />
</Grid>` },
    ],
    "Stack": [
      { lang: ".ui Markup", code: `<!-- Shows one child at a time, switch via active index -->
<Stack id="pages" active="0" expand="true">
  <VBox padding="16"><!-- Page 0 --></VBox>
  <VBox padding="16"><!-- Page 1 --></VBox>
</Stack>` },
      { lang: "C++", code: `auto* stack = g_layout.FindAs<ui::StackWidget>("pages");
stack->SetActiveIndex(1);
stack->DoLayout();` },
    ],
    "ScrollView": [
      { lang: ".ui Markup", code: `<ScrollView expand="true">
  <VBox padding="16" gap="8">
    <!-- Content that exceeds view height will scroll -->
  </VBox>
</ScrollView>` },
      { lang: "C", code: `UiWidget scroll = ui_scroll_view();
UiWidget content = ui_vbox();
ui_widget_set_gap(content, 8);
for (int i = 0; i < 100; i++) {
    ui_widget_add_child(content, ui_label(L"Item"));
}
ui_scroll_set_content(scroll, content);
ui_widget_set_expand(scroll, 1);` },
    ],
    "SplitView": [
      { lang: ".ui Markup", code: `<!-- mode: overlay / inline / compactOverlay / compactInline -->
<SplitView id="nav" mode="compactInline"
           openPaneLength="260" compactPaneLength="48"
           open="true" expand="true">
  <!-- Pane (sidebar) -->
  <VBox padding="4" gap="2">
    <NavItem text="Home" svg="..." selected="true" onClick="onHome" />
    <NavItem text="Settings" svg="..." onClick="onSettings" />
  </VBox>
  <!-- Content -->
  <Stack id="pages" active="0" expand="true">
    <VBox><!-- Home page --></VBox>
    <VBox><!-- Settings page --></VBox>
  </Stack>
</SplitView>` },
      { lang: "C++", code: `auto* sv = g_layout.FindAs<ui::SplitViewWidget>("nav");
sv->TogglePane();          // toggle open/close
sv->SetPaneOpen(true);     // open with animation
sv->SetPaneOpenImmediate(false);  // close without animation` },
    ],
    "Splitter": [
      { lang: ".ui Markup", code: `<!-- Draggable divider between two panels -->
<Splitter ratio="0.3" expand="true">
  <VBox><!-- Left panel, 30% --></VBox>
  <VBox><!-- Right panel, 70% --></VBox>
</Splitter>

<!-- Vertical (top/bottom) split -->
<Splitter ratio="0.5" vertical="true" expand="true">
  <VBox><!-- Top --></VBox>
  <VBox><!-- Bottom --></VBox>
</Splitter>` },
    ],
    "Panel": [
      { lang: ".ui Markup", code: `<Panel bgColor="theme.sidebarBg" padding="16" gap="8">
  <Label text="Inside a themed panel" />
</Panel>` },
      { lang: "C", code: `UiWidget panel = ui_panel((UiColor){0.12f, 0.12f, 0.12f, 1.0f});
ui_widget_set_padding_uniform(panel, 16);
ui_widget_set_gap(panel, 8);
ui_widget_add_child(panel, ui_label(L"Inside panel"));

// Or use themed panel:
UiWidget sidebar = ui_panel_themed(0);  // 0=sidebar 1=toolbar 2=content` },
    ],
    "Spacer": [
      { lang: ".ui Markup", code: `<Spacer size="12" />          <!-- Fixed 12px gap -->
<Spacer expand="true" />      <!-- Elastic, fills remaining space -->` },
      { lang: "C", code: `UiWidget gap = ui_spacer(12);       // fixed 12px
ui_widget_add_child(root, gap);

UiWidget elastic = ui_spacer(0);     // elastic
ui_widget_set_expand(elastic, 1);
ui_widget_add_child(hbox, elastic);` },
    ],
    "Expander": [
      { lang: ".ui Markup", code: `<Expander header="Advanced Options" expanded="true">
  <VBox padding="12" gap="8">
    <CheckBox text="Feature A" checked="true" />
    <Toggle text="Experimental" />
  </VBox>
</Expander>` },
    ],
    "TitleBar": [
      { lang: ".ui Markup", code: `<TitleBar title="My App" />

<!-- With custom widget -->
<TitleBar title="My App">
  <Button text="★" width="36" height="28" />
</TitleBar>` },
      { lang: "C", code: `UiWidget tb = ui_titlebar(L"My App");
ui_titlebar_show_buttons(tb, 1, 1, 1);  // min, max, close
ui_titlebar_set_bg_color(tb, (UiColor){0.12f, 0.12f, 0.12f, 1.0f});
ui_titlebar_add_widget(tb, custom_btn);` },
    ],
    "NavItem": [
      { lang: ".ui Markup", code: `<!-- Used inside SplitView pane -->
<NavItem text="Home"
         svg="<svg viewBox='0 0 24 24'><path d='...'/></svg>"
         selected="true"
         onClick="onNavHome" />

<!-- 40px height, left accent indicator when selected -->
<!-- Icon area 48px wide (centered in compact mode) -->` },
    ],
    "TabControl": [
      { lang: ".ui Markup", code: `<TabControl id="tabs">
  <Tab title="General">
    <VBox padding="16"><!-- Page content --></VBox>
  </Tab>
  <Tab title="Advanced">
    <VBox padding="16"><!-- Page content --></VBox>
  </Tab>
</TabControl>` },
      { lang: "C", code: `UiWidget tabs = ui_tab_control();
ui_tab_add(tabs, L"General", page1);
ui_tab_add(tabs, L"Advanced", page2);
ui_tab_set_active(tabs, 0);
ui_widget_add_child(root, tabs);` },
    ],
    "Dialog": [
      { lang: "C", code: `UiWidget dlg = ui_dialog();
ui_dialog_set_ok_text(dlg, L"Confirm");
ui_dialog_set_cancel_text(dlg, L"Cancel");
ui_dialog_set_show_cancel(dlg, 1);
ui_dialog_show(dlg, win, L"Delete?",
    L"This action cannot be undone.",
    on_confirm, NULL);` },
    ],
    "Toast": [
      { lang: "C", code: `// Bottom center, auto-fade
ui_toast(win, L"Saved!", 2000);

// Position: 0=top 1=center 2=bottom
ui_toast_at(win, L"Notice", 3000, 0);

// With icon: 0=none 1=success 2=error 3=warning
ui_toast_ex(win, L"Error occurred", 3000, 0, 2);` },
    ],
    "ContextMenu": [
      { lang: "C", code: `UiMenu menu = ui_menu_create();
ui_menu_add_item(menu, 1, L"Cut");
ui_menu_add_item_ex(menu, 2, L"Copy", L"Ctrl+C", svg_icon);
ui_menu_add_separator(menu);

UiMenu sub = ui_menu_create();
ui_menu_add_item(sub, 10, L"Sub Item");
ui_menu_add_submenu(menu, L"More", sub);

ui_menu_show(win, menu, x, y);
ui_menu_destroy(menu);` },
    ],
    "IconButton": [
      { lang: ".ui Markup", code: `<IconButton svg="<svg>...</svg>" ghost="true" width="36" height="36" />` },
      { lang: "C", code: `UiWidget btn = ui_icon_button(svg_settings, 1);  // 1=ghost
ui_icon_button_set_icon_color(btn, (UiColor){0.6f, 0.6f, 0.6f, 1.0f});
ui_icon_button_set_icon_padding(btn, 8);
ui_widget_on_click(btn, on_settings, NULL);` },
    ],
    "Flyout": [
      { lang: ".ui Markup", code: `<!-- Trigger button -->
<Button id="flyoutBtn" text="Show Flyout" onClick="onShowFlyout" />

<!-- Flyout (place at root level to avoid clipping) -->
<Flyout id="demoFlyout" placement="bottom">
  <VBox gap="8" padding="8">
    <Label text="Popup content" bold="true" />
    <Button text="Close" onClick="onDismiss" />
  </VBox>
</Flyout>` },
      { lang: "C++", code: `auto* flyout = g_layout.FindAs<ui::FlyoutWidget>("demoFlyout");
auto* anchor = g_layout.FindById("flyoutBtn");
flyout->Show(anchor);   // show attached to anchor
flyout->Hide();
// placement: top / bottom / left / right / auto` },
    ],
  };
  return map[controlName] ?? [];
}

export function ControlDetail() {
  const styles = useStyles();
  const { t } = useTranslation();
  const { name } = useParams<{ name: string }>();

  const control = controls.find((c) => c.name.toLowerCase() === name?.toLowerCase());

  if (!control) {
    return (
      <div className={styles.notFound}>
        <Text size={500}>Control not found: {name}</Text>
      </div>
    );
  }

  const relatedFns = getRelatedApiFunctions(control.name);
  const examples = getExamples(control.name);

  return (
    <div className={styles.page}>
      <Breadcrumb>
        <BreadcrumbItem>
          <BreadcrumbButton
            // @ts-expect-error Fluent UI 'as' prop vs react-router Link
            as={Link}
            to="/docs/controls"
          >
            {t("nav.controls")}
          </BreadcrumbButton>
        </BreadcrumbItem>
        <BreadcrumbDivider />
        <BreadcrumbItem>
          <BreadcrumbButton current>{control.name}</BreadcrumbButton>
        </BreadcrumbItem>
      </Breadcrumb>

      <div className={styles.header}>
        <h1 className={styles.title}>
          {t(control.nameKey)}
          <Badge appearance="tint" color={categoryColors[control.category]}>
            {t(`controls.category.${control.category}`)}
          </Badge>
        </h1>
        <p className={styles.description}>{t(control.descKey)}</p>

        <div className={styles.featureList}>
          {control.keyFeatures.map((f) => (
            <span key={f} className={styles.featureTag}>{f}</span>
          ))}
        </div>
      </div>

      {/* Live Preview */}
      <div className={styles.section}>
        <h2 className={styles.sectionTitle}>{t("controlDetail.preview")}</h2>
        <ControlPreview controlName={control.name} />
      </div>

      {examples.length > 0 && (
        <div className={styles.section}>
          <h2 className={styles.sectionTitle}>{t("controlDetail.example")}</h2>
          <div style={{ display: "flex", flexDirection: "column", gap: "12px" }}>
            {examples.map((ex, i) => (
              <CodeBlock key={i} code={ex.code} language={ex.lang} />
            ))}
          </div>
        </div>
      )}

      {relatedFns.length > 0 && (
        <div className={styles.section}>
          <h2 className={styles.sectionTitle}>
            {t("controlDetail.relatedApi")} ({relatedFns.length})
          </h2>
          {relatedFns.map((fn) => (
            <Card key={fn.name} className={styles.apiCard}>
              <div className={styles.apiName}>{fn.name}</div>
              <div className={styles.apiSig}>{fn.signature}</div>
              <div className={styles.apiDesc}>{fn.description}</div>
            </Card>
          ))}
        </div>
      )}
    </div>
  );
}
