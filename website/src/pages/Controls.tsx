import { useState } from "react";
import { makeStyles, tokens, Text, TabList, Tab, SearchBox, Field } from "@fluentui/react-components";
import { useTranslation } from "react-i18next";
import { ControlCard } from "../components/ControlCard";
import { controls } from "../data/controls";

const useStyles = makeStyles({
  page: {
    maxWidth: "1000px",
  },
  title: {
    fontSize: "32px",
    fontWeight: 700,
    color: tokens.colorNeutralForeground1,
    marginBottom: "8px",
  },
  subtitle: {
    color: tokens.colorNeutralForeground2,
    marginBottom: "24px",
    lineHeight: "24px",
  },
  toolbar: {
    display: "flex",
    alignItems: "center",
    gap: "16px",
    marginBottom: "24px",
    flexWrap: "wrap",
  },
  grid: {
    display: "grid",
    gridTemplateColumns: "repeat(auto-fill, minmax(320px, 1fr))",
    gap: "12px",
  },
  count: {
    color: tokens.colorNeutralForeground3,
    fontSize: "13px",
    marginBottom: "16px",
  },
});

const categoryKeys = ["all", "container", "input", "display", "navigation"] as const;

export function Controls() {
  const styles = useStyles();
  const { t } = useTranslation();
  const [category, setCategory] = useState("all");
  const [search, setSearch] = useState("");

  const filtered = controls.filter((c) => {
    const matchCategory = category === "all" || c.category === category;
    if (!matchCategory) return false;
    if (!search) return true;
    const lower = search.toLowerCase();
    const localName = t(c.nameKey).toLowerCase();
    const localDesc = t(c.descKey).toLowerCase();
    return (
      c.name.toLowerCase().includes(lower) ||
      localName.includes(lower) ||
      localDesc.includes(lower)
    );
  });

  return (
    <div className={styles.page}>
      <h1 className={styles.title}>{t("controls.title")}</h1>
      <p className={styles.subtitle}>{t("controls.subtitle")}</p>

      <div className={styles.toolbar}>
        <TabList
          selectedValue={category}
          onTabSelect={(_, data) => setCategory(data.value as string)}
          size="small"
        >
          {categoryKeys.map((key) => (
            <Tab key={key} value={key}>
              {t(`controls.category.${key}`)}
            </Tab>
          ))}
        </TabList>
        <Field style={{ minWidth: "200px" }}>
          <SearchBox
            placeholder={t("controls.searchPlaceholder")}
            value={search}
            onChange={(_, data) => setSearch(data.value)}
            size="small"
          />
        </Field>
      </div>

      <Text className={styles.count} block>
        {t("controls.count_other", { count: filtered.length })}
      </Text>

      <div className={styles.grid}>
        {filtered.map((c) => (
          <ControlCard key={c.name} control={c} />
        ))}
      </div>
    </div>
  );
}
