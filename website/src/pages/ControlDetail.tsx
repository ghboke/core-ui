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
    "TextInput": ["ui_text_input", "ui_text_input_get_text", "ui_text_input_set_text", "ui_text_input_set_read_only", "ui_text_input_set_password", "ui_text_input_select_all"],
    "TextArea": ["ui_text_area", "ui_text_area_get_text", "ui_text_area_set_text", "ui_text_area_set_read_only"],
    "ComboBox": ["ui_combobox", "ui_combobox_get_selected", "ui_combobox_set_selected", "ui_combobox_on_changed"],
    "ProgressBar": ["ui_progress_bar", "ui_progress_get_value", "ui_progress_set_value"],
    "ImageView": ["ui_image_view", "ui_image_load_file", "ui_image_set_pixels", "ui_image_clear", "ui_image_get_zoom", "ui_image_set_zoom", "ui_image_fit", "ui_image_reset", "ui_image_set_rotation", "ui_image_set_checkerboard"],
    "IconButton": ["ui_icon_button", "ui_icon_button_set_svg", "ui_icon_button_set_ghost", "ui_icon_button_set_icon_color", "ui_icon_button_set_icon_padding"],
    "TitleBar": ["ui_titlebar", "ui_titlebar_set_title", "ui_titlebar_show_buttons", "ui_titlebar_show_icon", "ui_titlebar_set_bg_color", "ui_titlebar_add_widget"],
    "Dialog": ["ui_msgbox", "ui_msgbox_ex"],
    "Toast": ["ui_toast", "ui_toast_ex"],
    "ContextMenu": ["ui_page_menu", "ui_menu_show", "ui_menu_close", "ui_window_on_menu", "ui_menu_item_id", "ui_menu_item_attr", "ui_menu_set_bg_color", "ui_menu_set_backdrop_blur"],
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
  lang: "C" | ".uix" | "C++";
}

function getExamples(controlName: string): CodeExample[] {
  const map: Record<string, CodeExample[]> = {
    "Button": [
      { lang: ".uix", code: `<button @click="onCancel">Cancel</button>

<!-- Primary (accent filled) — class swap drives style -->
<button class="primary" @click="onSave">Save</button>

<!-- Inline style override -->
<button style="background: #cc3333; color: #ffffff" @click="onDel">Delete</button>` },
      { lang: "C", code: `UiWidget btn = ui_button(L"Click Me");
ui_button_set_type(btn, 1);  // 0=default, 1=primary
ui_widget_on_click(btn, my_callback, NULL);
ui_widget_add_child(root, btn);` },
    ],
    "Label": [
      { lang: ".uix", code: `<label class="h1">Title</label>
<label class="body">Body text with wrapping</label>
<label class="accent">Centered accent text</label>

<!-- text interpolation is reactive — updates with state -->
<label>Hello, {{ name }}</label>` },
      { lang: "C", code: `UiWidget title = ui_label(L"Hello World");
ui_label_set_font_size(title, 24);
ui_label_set_bold(title, 1);
ui_label_set_align(title, 2);  // 0=left 1=right 2=center
ui_widget_add_child(root, title);` },
    ],
    "CheckBox": [
      { lang: ".uix", code: `<!-- v-model two-way binds to state.agree -->
<input type="checkbox" v-model="agree"/>
<label>I agree to the terms</label>

<!-- Or one-way :checked + @change -->
<input type="checkbox" :checked="agree" @change="onToggle($event)"/>` },
      { lang: "C", code: `UiWidget cb = ui_checkbox(L"Enable feature");
ui_checkbox_set_checked(cb, 1);
ui_checkbox_on_changed(cb, on_check, NULL);
ui_widget_add_child(root, cb);` },
    ],
    "RadioButton": [
      { lang: ".uix", code: `<input type="radio" name="grp" :checked="pick === 'a'" @change="pick = 'a'"/>
<label>Option A</label>
<input type="radio" name="grp" :checked="pick === 'b'" @change="pick = 'b'"/>
<label>Option B</label>` },
      { lang: "C", code: `UiWidget r1 = ui_radio_button(L"Option A", "group1");
UiWidget r2 = ui_radio_button(L"Option B", "group1");
ui_widget_add_child(root, r1);
ui_widget_add_child(root, r2);` },
    ],
    "Toggle": [
      { lang: ".uix", code: `<!-- v-model bound to a boolean — animation runs on change -->
<toggle v-model="dark"/>
<label>Dark mode</label>` },
      { lang: "C", code: `UiWidget toggle = ui_toggle(L"Dark Mode");
ui_toggle_set_on(toggle, 1);
ui_toggle_on_changed(toggle, on_toggle, NULL);
ui_widget_add_child(root, toggle);` },
    ],
    "Slider": [
      { lang: ".uix", code: `<input type="range" min="0" max="100" v-model="vol" class="grow"/>
<label>{{ vol }}%</label>` },
      { lang: "C", code: `UiWidget slider = ui_slider(0, 100, 50);
ui_slider_on_changed(slider, on_value, NULL);
ui_widget_add_child(root, slider);` },
    ],
    "TextInput": [
      { lang: ".uix", code: `<input v-model="name" placeholder="Enter your name..." class="grow"/>
<label>Hello, {{ name }}</label>` },
      { lang: "C", code: `UiWidget input = ui_text_input(L"Type here...");
ui_widget_set_width(input, 300);
ui_widget_add_child(root, input);

// Get text (internal pointer, do not free)
const wchar_t* text = ui_text_input_get_text(input);` },
    ],
    "TextArea": [
      { lang: ".uix", code: `<textarea v-model="notes" placeholder="Type notes..."
          style="height: 120px"/>` },
      { lang: "C", code: `UiWidget area = ui_text_area(L"Enter description...");
ui_widget_set_width(area, 400);
ui_widget_set_height(area, 200);
ui_widget_add_child(root, area);` },
    ],
    "ComboBox": [
      { lang: ".uix", code: `<!-- v-model bound to selected index -->
<select v-model="theme">
  <option>Dark</option>
  <option>Light</option>
  <option>System</option>
</select>` },
      { lang: "C", code: `const wchar_t* items[] = { L"Red", L"Green", L"Blue" };
UiWidget combo = ui_combobox(items, 3);
ui_combobox_on_changed(combo, on_select, NULL);
ui_widget_add_child(root, combo);` },
    ],
    "NumberBox": [
      { lang: ".uix", code: `<input type="number" v-model="qty" min="0" max="100" step="1"/>
<input type="number" v-model="ratio" min="0" max="1" step="0.1"/>` },
    ],
    "ProgressBar": [
      { lang: ".uix", code: `<!-- :value bound to a reactive number; mounting snaps, later updates animate -->
<progressbar min="0" max="100" :value="dl"/>
<progressbar indeterminate="true"/>` },
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
      { lang: ".uix", code: `<hr/>
<!-- Library element with a vertical flag -->
<separator vertical="true"/>` },
      { lang: "C", code: `UiWidget sep = ui_separator();
ui_widget_add_child(root, sep);

UiWidget vsep = ui_vseparator();
ui_widget_add_child(hbox, vsep);` },
    ],
    "VBox": [
      { lang: ".uix", code: `<!-- Vertical column = a div with flex-direction: column (default) -->
<style>
  .col { gap: 12px; padding: 16px; align-items: center; flex: 1; }
</style>
<div class="col">
  <label>Item 1</label>
  <label>Item 2</label>
  <label>Item 3</label>
</div>` },
      { lang: "C", code: `UiWidget col = ui_vbox();
ui_widget_set_padding_uniform(col, 16);
ui_widget_set_gap(col, 12);
ui_widget_add_child(col, ui_label(L"Item 1"));
ui_widget_add_child(col, ui_label(L"Item 2"));
ui_window_set_root(win, col);` },
    ],
    "HBox": [
      { lang: ".uix", code: `<style>
  .row { flex-direction: row; gap: 8px; align-items: center; }
  .grow { flex: 1; }
</style>
<div class="row">
  <button>OK</button>
  <button>Cancel</button>
  <div class="grow"/>     <!-- elastic spacer -->
  <label>Status</label>
</div>` },
      { lang: "C", code: `UiWidget row = ui_hbox();
ui_widget_set_gap(row, 8);
ui_widget_add_child(row, ui_button(L"OK"));
ui_widget_add_child(row, ui_button(L"Cancel"));
ui_widget_add_child(root, row);` },
    ],
    "Grid": [
      { lang: ".uix", code: `<!-- core-ui flex doesn't ship a true CSS Grid; use multi-row flex columns
     or wrap with v-for to lay out a "grid" of items. Two-row × two-card
     pattern below — flex stretches each row to equal height. -->
<style>
  .grid { gap: 16px; }
  .grow { flex-direction: row; gap: 16px; }
  .card { flex: 1; padding: 16px; background: var(--card-bg); border-radius: 6px; }
</style>
<div class="grid">
  <div class="grow">
    <div class="card"><label>A</label></div>
    <div class="card"><label>B</label></div>
  </div>
  <div class="grow">
    <div class="card"><label>C</label></div>
    <div class="card"><label>D</label></div>
  </div>
</div>` },
    ],
    "Stack": [
      { lang: ".uix", code: `<!-- v-if mounts/unmounts; <Tabs> below shows another option -->
<button @click="page = 0">Page 0</button>
<button @click="page = 1">Page 1</button>

<div v-if="page === 0" class="page"><label>Page 0</label></div>
<div v-if="page === 1" class="page"><label>Page 1</label></div>` },
      { lang: "C++", code: `auto* stack = g_layout.FindAs<ui::StackWidget>("pages");
stack->SetActiveIndex(1);
stack->DoLayout();` },
    ],
    "ScrollView": [
      { lang: ".uix", code: `<!-- ScrollView is a built-in widget; lowercase tag is fine -->
<scrollview style="flex: 1">
  <div class="col">
    <label v-for="i in 100" :key="i">Item {{ i }}</label>
  </div>
</scrollview>` },
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
      { lang: ".uix", code: `<!-- No public SplitView C API is exported currently.
     Compose a navigation view with a row container, fixed sidebar,
     and flex: 1 content pane. -->
<style>
  .shell { flex-direction: row; flex: 1; }
  .side  { width: 240px; background: var(--sidebar-bg); }
  .main  { flex: 1; background: var(--bg); }
</style>
<div class="shell">
  <div class="side"><label>Home</label></div>
  <div class="main"><label>Content</label></div>
</div>` },
    ],
    "Splitter": [
      { lang: ".uix", code: `<!-- No public Splitter widget is exported currently.
     Use fixed/flex panels, or implement a draggable divider as <custom>. -->
<div style="flex-direction: row; flex: 1">
  <div style="width: 280px"><label>Left panel</label></div>
  <div style="flex: 1"><label>Right panel</label></div>
</div>` },
    ],
    "Panel": [
      { lang: ".uix", code: `<!-- "Panel" in .uix is just a div with a background -->
<style>
  .panel { background: var(--card-bg); padding: 16px; gap: 8px; border-radius: 6px; }
</style>
<div class="panel">
  <label>Inside a themed panel</label>
</div>` },
      { lang: "C", code: `UiWidget panel = ui_panel((UiColor){0.12f, 0.12f, 0.12f, 1.0f});
ui_widget_set_padding_uniform(panel, 16);
ui_widget_set_gap(panel, 8);
ui_widget_add_child(panel, ui_label(L"Inside panel"));

// Or use themed panel:
UiWidget sidebar = ui_panel_themed(0);  // 0=sidebar 1=toolbar 2=content` },
    ],
    "Spacer": [
      { lang: ".uix", code: `<!-- Fixed gap → just style="width: 12px" or column gap on parent -->
<div style="width: 12px"/>

<!-- Elastic spacer in a flex row → flex: 1 -->
<div style="flex: 1"/>` },
      { lang: "C", code: `UiWidget gap = ui_spacer(12);       // fixed 12px
ui_widget_add_child(root, gap);

UiWidget elastic = ui_spacer(0);     // elastic
ui_widget_set_expand(elastic, 1);
ui_widget_add_child(hbox, elastic);` },
    ],
    "Expander": [
      { lang: ".uix", code: `<expander header="Advanced Options" expanded="true">
  <div class="col">
    <input type="checkbox" v-model="featA"/>
    <toggle v-model="experimental"/>
  </div>
</expander>` },
    ],
    "TitleBar": [
      { lang: ".uix", code: `<!-- core-ui auto-renders a TitleBar from <window> when frameless="true".
     For a custom one, use the TitleBar tag and add inner widgets. -->
<TitleBar title="My App"/>

<TitleBar title="My App">
  <button style="width: 36px; height: 28px">★</button>
</TitleBar>` },
      { lang: "C", code: `UiWidget tb = ui_titlebar(L"My App");
ui_titlebar_show_buttons(tb, 1, 1, 1);  // min, max, close
ui_titlebar_set_bg_color(tb, (UiColor){0.12f, 0.12f, 0.12f, 1.0f});
ui_titlebar_add_widget(tb, custom_btn);` },
    ],
    "NavItem": [
      { lang: ".uix", code: `<!-- Compose a nav item in .uix from a div + svg + label, drive selection
     with a class binding. -->
<style>
  .nav { flex-direction: row; align-items: center; gap: 10px;
         padding: 0 12px; height: 36px; border-radius: 4px; cursor: pointer; }
  .nav:hover { background: var(--sidebar-hover); }
  .nav.on    { background: var(--bg-3); }
  .nav.on .icon { color: var(--fg); }
  .nav-icon  { width: 20px; height: 20px; color: var(--sidebar-text); }
  .nav-text  { font-size: 13px; color: var(--sidebar-text); }
</style>

<div class="nav" :class="page === 'home' ? 'on' : ''" @click="page = 'home'">
  <svg class="nav-icon" viewBox="0 0 24 24"><path fill="currentColor" d="..."/></svg>
  <label class="nav-text">Home</label>
</div>` },
    ],
    "TabControl": [
      { lang: ".uix", code: `<tabs>
  <tab title="General">
    <div class="page"><label>General settings</label></div>
  </tab>
  <tab title="Advanced">
    <div class="page"><label>Advanced settings</label></div>
  </tab>
</tabs>` },
      { lang: "C", code: `UiWidget tabs = ui_tab_control();
ui_tab_add(tabs, L"General", page1);
ui_tab_add(tabs, L"Advanced", page2);
ui_tab_set_active(tabs, 0);
ui_widget_add_child(root, tabs);` },
    ],
    "Dialog": [
      { lang: "C", code: `const wchar_t* buttons[] = { L"Cancel", L"Delete" };
int result = ui_msgbox(win,
    L"Delete?",
    L"This action cannot be undone.",
    buttons, 2,
    1, 0,
    UI_MSGBOX_ICON_WARNING);

if (result == 1) {
    // Delete confirmed.
}` },
    ],
    "Toast": [
      { lang: "C", code: `// Top + slide-in/out, auto-dismiss
ui_toast(win, L"Saved!", 2000);

// Full form: position (0=top 1=center 2=bottom),
// icon (0=none 1=success 2=error 3=warning),
// anim (UI_TOAST_ANIM_SLIDE=0 | UI_TOAST_ANIM_FADE=1)
ui_toast_ex(win, L"Notice", 3000, 0, 0, UI_TOAST_ANIM_SLIDE);
ui_toast_ex(win, L"Error occurred", 3000, 0, 2, UI_TOAST_ANIM_SLIDE);

// Fade-only: pure alpha transition, position locked
ui_toast_ex(win, L"Copied", 1500, 1, 0, UI_TOAST_ANIM_FADE);` },
    ],
    "ContextMenu": [
      { lang: ".uix", code: `<!-- Declarative menu — trigger="#elem" auto-attaches click/rclick. -->
<div id="myArea">Right-click me</div>

<menu trigger="#myArea" event="rclick">
  <menuitem id="copy" @click="onCopy" shortcut="Ctrl+C">
    <svg viewBox="0 0 24 24"><path fill="currentColor" d="..."/></svg>
    <label>Copy</label>
  </menuitem>
  <menuitem id="cut" @click="onCut" shortcut="Ctrl+X">
    <label>Cut</label>
  </menuitem>
  <separator/>
  <menu text="Recent">                   <!-- nested = submenu -->
    <menuitem id="recent-1" @click="onOpen"><label>file1.txt</label></menuitem>
  </menu>
  <separator/>
  <menuitem id="quit" @click="onQuit" style="color: #d63a26">
    <label>Quit</label>
  </menuitem>
</menu>` },
      { lang: "C", code: `static void on_menu(UiWindow win, UiMenuItem item, void* ud) {
    const char* id = ui_menu_item_id(item);
    if (id && strcmp(id, "copy") == 0) {
        // Handle Copy.
    }
}

ui_window_on_menu(win, on_menu, NULL);

// For a <menu id="ctx"> declared in .uix:
UiMenu menu = ui_page_menu(page, "ctx");
ui_menu_show(win, menu, x, y);` },
    ],
    "IconButton": [
      { lang: ".uix", code: `<!-- Inline SVG icon as a button — fill="currentColor" picks up the
     button's text color, so it follows light/dark theme automatically. -->
<button class="icon-btn" @click="onSettings">
  <svg viewBox="0 0 24 24" width="18" height="18">
    <path fill="currentColor" d="..."/>
  </svg>
</button>` },
      { lang: "C", code: `UiWidget btn = ui_icon_button(svg_settings, 1);  // 1=ghost
ui_icon_button_set_icon_color(btn, (UiColor){0.6f, 0.6f, 0.6f, 1.0f});
ui_icon_button_set_icon_padding(btn, 8);
ui_widget_on_click(btn, on_settings, NULL);` },
    ],
    "Flyout": [
      { lang: ".uix", code: `<!-- Build a flyout with v-if + a class-driven popup. -->
<button id="flyoutBtn" @click="show = !show">Show Flyout</button>

<div v-if="show" class="flyout">
  <label>Popup content</label>
  <button @click="show = false">Close</button>
</div>` },
      { lang: ".uix", code: `<!-- No public Flyout widget is exported currently.
     Use a v-if popup panel anchored by layout. -->
<button id="flyoutBtn" @click="show = !show">Show Flyout</button>
<div v-if="show" class="flyout">
  <label>Popup content</label>
  <button @click="show = false">Close</button>
</div>` },
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
