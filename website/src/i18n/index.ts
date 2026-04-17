import i18n from "i18next";
import { initReactI18next } from "react-i18next";
import zhCN from "./zh-CN";
import en from "./en";

i18n.use(initReactI18next).init({
  resources: {
    "zh-CN": { translation: zhCN },
    en: { translation: en },
  },
  lng: "zh-CN",
  fallbackLng: "en",
  interpolation: {
    escapeValue: false,
  },
});

export default i18n;
