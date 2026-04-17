import { useState, useMemo, useCallback, useEffect } from "react";
import {
  makeStyles,
  tokens,
  Dialog,
  DialogSurface,
  DialogBody,
  SearchBox,
  Text,
} from "@fluentui/react-components";
import { useNavigate } from "react-router";
import { useTranslation } from "react-i18next";
import { controls } from "../data/controls";
import { apiGroups } from "../data/api-functions";

const useStyles = makeStyles({
  surface: {
    maxWidth: "520px",
    width: "100%",
    maxHeight: "440px",
    paddingTop: "0",
    paddingRight: "0",
    paddingBottom: "0",
    paddingLeft: "0",
    borderRadius: "14px",
    overflow: "hidden",
  },
  body: {
    paddingTop: "0",
    paddingRight: "0",
    paddingBottom: "0",
    paddingLeft: "0",
  },
  searchWrap: {
    paddingBlock: "12px",
    paddingInline: "14px",
    borderBottomWidth: "1px",
    borderBottomStyle: "solid",
    borderBottomColor: tokens.colorNeutralStroke2,
  },
  results: {
    overflowY: "auto",
    maxHeight: "340px",
    paddingBlock: "6px",
    paddingInline: "6px",
  },
  resultItem: {
    display: "flex",
    flexDirection: "column",
    gap: "1px",
    paddingBlock: "8px",
    paddingInline: "12px",
    borderRadius: "8px",
    cursor: "pointer",
    ":hover": {
      backgroundColor: tokens.colorSubtleBackgroundHover,
    },
  },
  resultName: {
    fontWeight: 600,
    fontSize: "13px",
    color: tokens.colorNeutralForeground1,
  },
  resultDesc: {
    fontSize: "12px",
    color: tokens.colorNeutralForeground3,
  },
  resultType: {
    fontSize: "10px",
    color: tokens.colorBrandForeground1,
    fontWeight: 700,
    textTransform: "uppercase",
    letterSpacing: "0.5px",
  },
  empty: {
    paddingBlock: "32px",
    textAlign: "center",
    color: tokens.colorNeutralForeground4,
  },
  hint: {
    paddingBlock: "24px",
    paddingInline: "16px",
    textAlign: "center",
    color: tokens.colorNeutralForeground4,
    fontSize: "13px",
  },
});

interface SearchResult {
  type: "control" | "api" | "page";
  name: string;
  description: string;
  path: string;
}

interface SearchDialogProps {
  open: boolean;
  onClose: () => void;
}

export function SearchDialog({ open, onClose }: SearchDialogProps) {
  const styles = useStyles();
  const navigate = useNavigate();
  const { t } = useTranslation();
  const [query, setQuery] = useState("");

  const index = useMemo(() => {
    const results: SearchResult[] = [];

    const pages: SearchResult[] = [
      { type: "page", name: t("search.page.gettingStarted"), description: "Build and integrate UI Core", path: "/docs/getting-started" },
      { type: "page", name: t("search.page.markup"), description: "Declarative UI markup syntax", path: "/docs/markup" },
      { type: "page", name: t("search.page.layout"), description: "Layout containers and flex system", path: "/docs/layout" },
      { type: "page", name: t("search.page.controls"), description: "29+ built-in controls overview", path: "/docs/controls" },
      { type: "page", name: t("search.page.cApi"), description: "Pure C function reference", path: "/docs/c-api" },
      { type: "page", name: t("search.page.designSystem"), description: "Fluent 2 design tokens", path: "/docs/design-system" },
      { type: "page", name: t("search.page.ai"), description: "AI coding assistant guide", path: "/docs/ai" },
    ];
    results.push(...pages);

    for (const c of controls) {
      results.push({
        type: "control",
        name: c.name,
        description: t(c.descKey),
        path: `/docs/controls/${c.name.toLowerCase()}`,
      });
    }

    for (const group of apiGroups) {
      for (const fn of group.functions) {
        results.push({
          type: "api",
          name: fn.name,
          description: fn.description,
          path: "/docs/c-api",
        });
      }
    }

    return results;
  }, [t]);

  const resultList = useMemo(() => {
    if (!query.trim()) return [];
    const lower = query.toLowerCase();
    return index
      .filter((r) => r.name.toLowerCase().includes(lower) || r.description.toLowerCase().includes(lower))
      .slice(0, 20);
  }, [query, index]);

  const handleSelect = useCallback(
    (path: string) => {
      navigate(path);
      onClose();
      setQuery("");
    },
    [navigate, onClose]
  );

  useEffect(() => {
    if (!open) setQuery("");
  }, [open]);

  return (
    <Dialog open={open} onOpenChange={(_, data) => { if (!data.open) onClose(); }}>
      <DialogSurface className={styles.surface}>
        <DialogBody className={styles.body}>
          <div className={styles.searchWrap}>
            <SearchBox
              placeholder={t("search.placeholder")}
              value={query}
              onChange={(_, data) => setQuery(data.value)}
              autoFocus
              size="medium"
            />
          </div>
          <div className={styles.results}>
            {query && resultList.length === 0 && (
              <div className={styles.empty}>
                <Text size={200}>{t("search.noResults")}</Text>
              </div>
            )}
            {!query && (
              <div className={styles.hint}>{t("search.hint")}</div>
            )}
            {resultList.map((r, i) => (
              <div key={`${r.name}-${i}`} className={styles.resultItem} onClick={() => handleSelect(r.path)}>
                <span className={styles.resultType}>
                  {r.type === "control" ? "CONTROL" : r.type === "api" ? "API" : "PAGE"}
                </span>
                <span className={styles.resultName}>{r.name}</span>
                <span className={styles.resultDesc}>{r.description}</span>
              </div>
            ))}
          </div>
        </DialogBody>
      </DialogSurface>
    </Dialog>
  );
}
