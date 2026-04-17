import { useState, useEffect, useCallback } from "react";
import { makeStyles, tokens, Tooltip } from "@fluentui/react-components";
import { CopyRegular, CheckmarkRegular } from "@fluentui/react-icons";
import { useTranslation } from "react-i18next";
import { createHighlighter, type Highlighter } from "shiki";
import { useThemeMode } from "../ThemeContext";

const useStyles = makeStyles({
  wrapper: {
    position: "relative",
    borderRadius: "10px",
    overflow: "hidden",
    borderTopWidth: "1px",
    borderRightWidth: "1px",
    borderBottomWidth: "1px",
    borderLeftWidth: "1px",
    borderTopStyle: "solid",
    borderRightStyle: "solid",
    borderBottomStyle: "solid",
    borderLeftStyle: "solid",
    borderTopColor: tokens.colorNeutralStroke2,
    borderRightColor: tokens.colorNeutralStroke2,
    borderBottomColor: tokens.colorNeutralStroke2,
    borderLeftColor: tokens.colorNeutralStroke2,
    backgroundColor: tokens.colorNeutralBackground1,
  },
  toolbar: {
    display: "flex",
    alignItems: "center",
    justifyContent: "space-between",
    height: "36px",
    paddingInline: "16px",
    backgroundColor: tokens.colorNeutralBackground2,
    borderBottomWidth: "1px",
    borderBottomStyle: "solid",
    borderBottomColor: tokens.colorNeutralStroke2,
  },
  langLabel: {
    fontSize: "11px",
    fontWeight: 700,
    color: tokens.colorNeutralForeground3,
    letterSpacing: "0.5px",
    textTransform: "uppercase",
    fontFamily: "'Cascadia Code', Consolas, monospace",
  },
  copyBtn: {
    display: "inline-flex",
    alignItems: "center",
    gap: "5px",
    height: "26px",
    borderTopWidth: "1px",
    borderRightWidth: "1px",
    borderBottomWidth: "1px",
    borderLeftWidth: "1px",
    borderTopStyle: "solid",
    borderRightStyle: "solid",
    borderBottomStyle: "solid",
    borderLeftStyle: "solid",
    borderTopColor: tokens.colorNeutralStroke2,
    borderRightColor: tokens.colorNeutralStroke2,
    borderBottomColor: tokens.colorNeutralStroke2,
    borderLeftColor: tokens.colorNeutralStroke2,
    borderRadius: "6px",
    backgroundColor: tokens.colorNeutralBackground1,
    color: tokens.colorNeutralForeground3,
    cursor: "pointer",
    fontSize: "12px",
    fontWeight: 500,
    paddingInline: "8px",
    fontFamily: "inherit",
    transitionProperty: "background-color, color",
    transitionDuration: "150ms",
    ":hover": {
      backgroundColor: tokens.colorSubtleBackgroundHover,
      color: tokens.colorNeutralForeground1,
    },
  },
  copied: {
    color: tokens.colorPaletteGreenForeground1,
    borderTopColor: tokens.colorPaletteGreenBorder1,
    borderRightColor: tokens.colorPaletteGreenBorder1,
    borderBottomColor: tokens.colorPaletteGreenBorder1,
    borderLeftColor: tokens.colorPaletteGreenBorder1,
  },
  copyIcon: {
    fontSize: "14px",
  },
  codeArea: {
    paddingBlock: "16px",
    paddingInline: "18px",
    overflowX: "auto",
  },
  fallback: {
    fontFamily: "'Cascadia Code', Consolas, 'Courier New', monospace",
    fontSize: "13px",
    lineHeight: "21px",
    whiteSpace: "pre",
    color: tokens.colorNeutralForeground1,
    marginTop: "0",
    marginBottom: "0",
  },
});

const langMap: Record<string, string> = {
  "C": "c",
  "C API": "c",
  "C++": "cpp",
  "Shell": "shellscript",
  "CMake": "cmake",
  ".ui Markup": "xml",
  "XML": "xml",
};

const displayLabel: Record<string, string> = {
  "C": "C",
  "C API": "C",
  "C++": "C++",
  "Shell": "SHELL",
  "CMake": "CMAKE",
  ".ui Markup": "XML",
  "XML": "XML",
};

let highlighterPromise: Promise<Highlighter> | null = null;

function getHighlighter() {
  if (!highlighterPromise) {
    highlighterPromise = createHighlighter({
      themes: ["github-dark", "github-light"],
      langs: ["c", "cpp", "xml", "shellscript", "cmake"],
    });
  }
  return highlighterPromise;
}

interface CodeBlockProps {
  code: string;
  language?: string;
}

export function CodeBlock({ code, language }: CodeBlockProps) {
  const styles = useStyles();
  const themeMode = useThemeMode();
  const { t } = useTranslation();
  const [html, setHtml] = useState("");
  const [copied, setCopied] = useState(false);

  useEffect(() => {
    const lang = language ? (langMap[language] ?? "c") : "c";
    getHighlighter().then((h) => {
      const result = h.codeToHtml(code, {
        lang,
        theme: themeMode === "dark" ? "github-dark" : "github-light",
      });
      setHtml(result);
    });
  }, [code, language, themeMode]);

  const handleCopy = useCallback(() => {
    navigator.clipboard.writeText(code).then(() => {
      setCopied(true);
      setTimeout(() => setCopied(false), 2000);
    });
  }, [code]);

  const label = language ? (displayLabel[language] ?? language.toUpperCase()) : "CODE";

  return (
    <div className={styles.wrapper}>
      <div className={styles.toolbar}>
        <span className={styles.langLabel}>{label}</span>
        <Tooltip content={copied ? t("code.copied") : t("code.copy")} relationship="label">
          <button className={`${styles.copyBtn} ${copied ? styles.copied : ""}`} onClick={handleCopy}>
            {copied ? (
              <><CheckmarkRegular className={styles.copyIcon} /> {t("code.copied")}</>
            ) : (
              <><CopyRegular className={styles.copyIcon} /> {t("code.copy")}</>
            )}
          </button>
        </Tooltip>
      </div>
      <div className={styles.codeArea}>
        {html ? (
          <div dangerouslySetInnerHTML={{ __html: html }} />
        ) : (
          <pre className={styles.fallback}>
            <code>{code}</code>
          </pre>
        )}
      </div>
    </div>
  );
}
