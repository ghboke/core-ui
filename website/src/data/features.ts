export interface Feature {
  titleKey: string;
  descKey: string;
  icon: string;
}

export const features: Feature[] = [
  { titleKey: "feature.gpu.title", descKey: "feature.gpu.desc", icon: "gpu" },
  { titleKey: "feature.design.title", descKey: "feature.design.desc", icon: "design" },
  { titleKey: "feature.markup.title", descKey: "feature.markup.desc", icon: "markup" },
  { titleKey: "feature.api.title", descKey: "feature.api.desc", icon: "api" },
  { titleKey: "feature.controls.title", descKey: "feature.controls.desc", icon: "controls" },
  { titleKey: "feature.dpi.title", descKey: "feature.dpi.desc", icon: "dpi" },
];
