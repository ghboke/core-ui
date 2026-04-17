import { useState } from "react";
import { makeStyles, tokens, Text, SearchBox, Field, Accordion, AccordionItem, AccordionHeader, AccordionPanel, Badge } from "@fluentui/react-components";
import { useTranslation } from "react-i18next";
import { apiGroups, totalFunctions } from "../data/api-functions";

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
    marginBottom: "24px",
    lineHeight: "24px",
  },
  toolbar: {
    marginBottom: "24px",
  },
  fnRow: {
    paddingBlock: "8px",
    borderBottom: `1px solid ${tokens.colorNeutralStroke2}`,
    ":last-child": {
      borderBottom: "none",
    },
  },
  fnName: {
    fontFamily: "Consolas, 'Courier New', monospace",
    fontSize: "13px",
    fontWeight: 600,
    color: tokens.colorBrandForeground1,
  },
  fnSig: {
    fontFamily: "Consolas, 'Courier New', monospace",
    fontSize: "12px",
    color: tokens.colorNeutralForeground3,
    display: "block",
    marginTop: "2px",
  },
  fnDesc: {
    fontSize: "13px",
    color: tokens.colorNeutralForeground2,
    marginTop: "4px",
  },
  count: {
    color: tokens.colorNeutralForeground3,
    fontSize: "13px",
    marginBottom: "16px",
  },
  groupHeader: {
    display: "flex",
    alignItems: "center",
    gap: "8px",
  },
});

export function CApi() {
  const styles = useStyles();
  const { t } = useTranslation();
  const [search, setSearch] = useState("");

  const lowerSearch = search.toLowerCase();
  const filteredGroups = apiGroups
    .map((g) => ({
      ...g,
      functions: g.functions.filter(
        (f) =>
          !search ||
          f.name.toLowerCase().includes(lowerSearch) ||
          f.description.toLowerCase().includes(lowerSearch)
      ),
    }))
    .filter((g) => g.functions.length > 0);

  const matchCount = filteredGroups.reduce((s, g) => s + g.functions.length, 0);

  return (
    <div className={styles.page}>
      <h1 className={styles.title}>{t("api.title")}</h1>
      <p className={styles.subtitle}>{t("api.subtitle", { count: totalFunctions })}</p>

      <div className={styles.toolbar}>
        <Field>
          <SearchBox
            placeholder={t("api.searchPlaceholder")}
            value={search}
            onChange={(_, data) => setSearch(data.value)}
          />
        </Field>
      </div>

      <Text className={styles.count} block>
        {matchCount} / {totalFunctions} {t("api.functions")}
      </Text>

      <Accordion multiple collapsible defaultOpenItems={apiGroups.map((_, i) => i.toString())}>
        {filteredGroups.map((group, i) => (
          <AccordionItem key={group.name} value={i.toString()}>
            <AccordionHeader>
              <div className={styles.groupHeader}>
                <span>{group.name}</span>
                <Badge appearance="tint" size="small" color="informative">
                  {group.functions.length}
                </Badge>
              </div>
            </AccordionHeader>
            <AccordionPanel>
              {group.functions.map((fn) => (
                <div key={fn.name} className={styles.fnRow}>
                  <span className={styles.fnName}>{fn.name}</span>
                  <code className={styles.fnSig}>{fn.signature}</code>
                  <div className={styles.fnDesc}>{fn.description}</div>
                </div>
              ))}
            </AccordionPanel>
          </AccordionItem>
        ))}
      </Accordion>
    </div>
  );
}
