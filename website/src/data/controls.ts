export interface ControlInfo {
  name: string;
  nameKey: string;
  descKey: string;
  category: "container" | "input" | "display" | "navigation";
  cApiCreate: string;
  keyFeatures: string[];
}

export const controls: ControlInfo[] = [
  // Containers
  { name: "VBox", nameKey: "ctrl.VBox.name", descKey: "ctrl.VBox.desc", category: "container", cApiCreate: "ui_vbox()", keyFeatures: ["gap", "padding", "align", "justify", "flex"] },
  { name: "HBox", nameKey: "ctrl.HBox.name", descKey: "ctrl.HBox.desc", category: "container", cApiCreate: "ui_hbox()", keyFeatures: ["gap", "padding", "align", "justify", "flex"] },
  { name: "Grid", nameKey: "ctrl.Grid.name", descKey: "ctrl.Grid.desc", category: "container", cApiCreate: "ui_grid(cols)", keyFeatures: ["cols", "colGap", "rowGap", "colspan", "rowspan"] },
  { name: "Stack", nameKey: "ctrl.Stack.name", descKey: "ctrl.Stack.desc", category: "container", cApiCreate: "ui_stack()", keyFeatures: ["active index", "page navigation"] },
  { name: "ScrollView", nameKey: "ctrl.ScrollView.name", descKey: "ctrl.ScrollView.desc", category: "container", cApiCreate: "ui_scroll_view()", keyFeatures: ["auto scrollbar", "WinUI 3 style", "mouse wheel"] },
  { name: "SplitView", nameKey: "ctrl.SplitView.name", descKey: "ctrl.SplitView.desc", category: "container", cApiCreate: "ui_split_view()", keyFeatures: ["4 display modes", "pane animations", "overlay/inline"] },
  { name: "Splitter", nameKey: "ctrl.Splitter.name", descKey: "ctrl.Splitter.desc", category: "container", cApiCreate: "ui_splitter()", keyFeatures: ["ratio", "vertical/horizontal", "real-time resize"] },
  { name: "Panel", nameKey: "ctrl.Panel.name", descKey: "ctrl.Panel.desc", category: "container", cApiCreate: "ui_panel()", keyFeatures: ["background color", "theme-aware"] },
  { name: "Spacer", nameKey: "ctrl.Spacer.name", descKey: "ctrl.Spacer.desc", category: "container", cApiCreate: "ui_spacer(size)", keyFeatures: ["fixed size", "elastic expand"] },
  { name: "Expander", nameKey: "ctrl.Expander.name", descKey: "ctrl.Expander.desc", category: "container", cApiCreate: "ui_expander()", keyFeatures: ["expand/collapse animation", "header + content"] },

  // Input
  { name: "Button", nameKey: "ctrl.Button.name", descKey: "ctrl.Button.desc", category: "input", cApiCreate: "ui_button(text)", keyFeatures: ["default/primary styles", "custom colors", "hover/press states"] },
  { name: "IconButton", nameKey: "ctrl.IconButton.name", descKey: "ctrl.IconButton.desc", category: "input", cApiCreate: "ui_icon_button(svg, ghost)", keyFeatures: ["SVG-based", "ghost mode", "custom colors"] },
  { name: "CheckBox", nameKey: "ctrl.CheckBox.name", descKey: "ctrl.CheckBox.desc", category: "input", cApiCreate: "ui_checkbox(text)", keyFeatures: ["20x20 box", "check animation", "accent fill"] },
  { name: "RadioButton", nameKey: "ctrl.RadioButton.name", descKey: "ctrl.RadioButton.desc", category: "input", cApiCreate: "ui_radio_button(text, group)", keyFeatures: ["group mutual exclusion", "scale animation"] },
  { name: "Toggle", nameKey: "ctrl.Toggle.name", descKey: "ctrl.Toggle.desc", category: "input", cApiCreate: "ui_toggle()", keyFeatures: ["40x20 track", "slide animation", "color transition"] },
  { name: "Slider", nameKey: "ctrl.Slider.name", descKey: "ctrl.Slider.desc", category: "input", cApiCreate: "ui_slider(min, max)", keyFeatures: ["4px track", "18px thumb", "exponential easing"] },
  { name: "TextInput", nameKey: "ctrl.TextInput.name", descKey: "ctrl.TextInput.desc", category: "input", cApiCreate: "ui_text_input(placeholder)", keyFeatures: ["placeholder", "read-only", "caret", "selection"] },
  { name: "TextArea", nameKey: "ctrl.TextArea.name", descKey: "ctrl.TextArea.desc", category: "input", cApiCreate: "ui_text_area(placeholder)", keyFeatures: ["word wrap", "multi-line", "selection"] },
  { name: "NumberBox", nameKey: "ctrl.NumberBox.name", descKey: "ctrl.NumberBox.desc", category: "input", cApiCreate: "ui_number_box()", keyFeatures: ["min/max/step", "decimal places", "buttons"] },
  { name: "ComboBox", nameKey: "ctrl.ComboBox.name", descKey: "ctrl.ComboBox.desc", category: "input", cApiCreate: "ui_combobox()", keyFeatures: ["item list", "single selection", "indexed access"] },

  // Display
  { name: "Label", nameKey: "ctrl.Label.name", descKey: "ctrl.Label.desc", category: "display", cApiCreate: "ui_label(text)", keyFeatures: ["word-wrap", "max lines", "bold", "font size", "alignment"] },
  { name: "ProgressBar", nameKey: "ctrl.ProgressBar.name", descKey: "ctrl.ProgressBar.desc", category: "display", cApiCreate: "ui_progress_bar()", keyFeatures: ["determinate/indeterminate", "animated transitions"] },
  { name: "ImageView", nameKey: "ctrl.ImageView.name", descKey: "ctrl.ImageView.desc", category: "display", cApiCreate: "ui_image_view()", keyFeatures: ["zoom levels", "pan", "rotation", "tiled rendering", "GDI+ streaming"] },
  { name: "Separator", nameKey: "ctrl.Separator.name", descKey: "ctrl.Separator.desc", category: "display", cApiCreate: "ui_separator()", keyFeatures: ["horizontal/vertical", "theme-aware color"] },

  // Navigation
  { name: "TitleBar", nameKey: "ctrl.TitleBar.name", descKey: "ctrl.TitleBar.desc", category: "navigation", cApiCreate: "ui_titlebar(title)", keyFeatures: ["draggable", "min/max/close", "custom widgets"] },
  { name: "NavItem", nameKey: "ctrl.NavItem.name", descKey: "ctrl.NavItem.desc", category: "navigation", cApiCreate: "ui_nav_item()", keyFeatures: ["SVG icon + label", "selected state", "hover"] },
  { name: "TabControl", nameKey: "ctrl.TabControl.name", descKey: "ctrl.TabControl.desc", category: "navigation", cApiCreate: "ui_tab_control()", keyFeatures: ["multiple tabs", "active switching", "content per tab"] },
  { name: "Dialog", nameKey: "ctrl.Dialog.name", descKey: "ctrl.Dialog.desc", category: "navigation", cApiCreate: "ui_dialog()", keyFeatures: ["custom buttons", "callback-based", "modal overlay"] },
  { name: "Toast", nameKey: "ctrl.Toast.name", descKey: "ctrl.Toast.desc", category: "navigation", cApiCreate: "ui_toast(win, text, ms)", keyFeatures: ["3 positions", "4 icons", "auto-fade"] },
  { name: "ContextMenu", nameKey: "ctrl.ContextMenu.name", descKey: "ctrl.ContextMenu.desc", category: "navigation", cApiCreate: "ui_menu_create()", keyFeatures: ["items + shortcuts", "submenus", "separators"] },
  { name: "Flyout", nameKey: "ctrl.Flyout.name", descKey: "ctrl.Flyout.desc", category: "navigation", cApiCreate: "ui_flyout()", keyFeatures: ["4 placements", "auto-flip", "anchor attachment"] },
];
