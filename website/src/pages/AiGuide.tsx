import { useState, useCallback, useRef } from "react";
import { makeStyles, tokens, Button, Text, MessageBar, MessageBarBody, Tooltip } from "@fluentui/react-components";
import { CopyRegular, CheckmarkRegular, ArrowDownloadRegular } from "@fluentui/react-icons";
import { useTranslation } from "react-i18next";
import { AI_SKILL_CONTENT } from "../data/ai-skill";

const useStyles = makeStyles({
  page: { maxWidth: "880px" },
  title: { fontSize: "32px", fontWeight: 700, color: tokens.colorNeutralForeground1, marginBottom: "8px" },
  subtitle: { color: tokens.colorNeutralForeground2, marginBottom: "24px", lineHeight: "24px" },
  tip: { marginBottom: "24px" },
  toolbar: {
    display: "flex",
    alignItems: "center",
    justifyContent: "space-between",
    marginBottom: "8px",
    flexWrap: "wrap",
    gap: "8px",
  },
  toolbarLeft: {
    display: "flex",
    alignItems: "center",
    gap: "8px",
  },
  toolbarRight: {
    display: "flex",
    alignItems: "center",
    gap: "8px",
  },
  stats: {
    fontSize: "13px",
    color: tokens.colorNeutralForeground3,
  },
  textareaWrap: {
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
    borderRadius: "10px",
    overflow: "hidden",
    backgroundColor: tokens.colorNeutralBackground1,
  },
  textarea: {
    width: "100%",
    height: "600px",
    fontFamily: "'Cascadia Code', Consolas, 'Courier New', monospace",
    fontSize: "12px",
    lineHeight: "18px",
    color: tokens.colorNeutralForeground1,
    backgroundColor: "transparent",
    borderTopWidth: "0",
    borderRightWidth: "0",
    borderBottomWidth: "0",
    borderLeftWidth: "0",
    paddingBlock: "16px",
    paddingInline: "18px",
    resize: "vertical",
    outlineStyle: "none",
    display: "block",
  },
  section: { marginTop: "32px", marginBottom: "16px" },
  sectionTitle: { fontSize: "20px", fontWeight: 600, color: tokens.colorNeutralForeground1, marginBottom: "8px" },
  paragraph: { color: tokens.colorNeutralForeground2, lineHeight: "24px", marginBottom: "12px" },
  steps: {
    color: tokens.colorNeutralForeground2,
    lineHeight: "28px",
    paddingInlineStart: "24px",
    marginBottom: "16px",
  },
});

export function AiGuide() {
  const styles = useStyles();
  const { t } = useTranslation();
  const [copied, setCopied] = useState(false);
  const textareaRef = useRef<HTMLTextAreaElement>(null);

  const handleCopy = useCallback(() => {
    navigator.clipboard.writeText(AI_SKILL_CONTENT).then(() => {
      setCopied(true);
      setTimeout(() => setCopied(false), 2000);
    });
  }, []);

  const handleDownload = useCallback(() => {
    const blob = new Blob([AI_SKILL_CONTENT], { type: "text/markdown;charset=utf-8" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = "UI_CORE_SKILL.md";
    a.click();
    URL.revokeObjectURL(url);
  }, []);

  const lineCount = AI_SKILL_CONTENT.split("\n").length;
  const charCount = AI_SKILL_CONTENT.length;

  return (
    <div className={styles.page}>
      <h1 className={styles.title}>{t("ai.title")}</h1>
      <p className={styles.subtitle}>{t("ai.subtitle")}</p>

      <MessageBar intent="info" className={styles.tip}>
        <MessageBarBody>{t("ai.tip")}</MessageBarBody>
      </MessageBar>

      {/* Usage instructions */}
      <div className={styles.section}>
        <h2 className={styles.sectionTitle}>{t("ai.usageTitle")}</h2>
        <p className={styles.paragraph}>{t("ai.usageDesc")}</p>
        <ol className={styles.steps}>
          <li>{t("ai.step1")}</li>
          <li>{t("ai.step2")}</li>
          <li>{t("ai.step3")}</li>
          <li>{t("ai.step4")}</li>
        </ol>
      </div>

      {/* Skill content */}
      <div className={styles.toolbar}>
        <div className={styles.toolbarLeft}>
          <Text weight="semibold">UI_CORE_SKILL.md</Text>
          <Text className={styles.stats}>{lineCount} {t("ai.lines")} / {(charCount / 1024).toFixed(1)} KB</Text>
        </div>
        <div className={styles.toolbarRight}>
          <Tooltip content={copied ? t("code.copied") : t("code.copy")} relationship="label">
            <Button
              appearance={copied ? "primary" : "outline"}
              icon={copied ? <CheckmarkRegular /> : <CopyRegular />}
              size="small"
              onClick={handleCopy}
            >
              {copied ? t("code.copied") : t("code.copy")}
            </Button>
          </Tooltip>
          <Button
            appearance="primary"
            icon={<ArrowDownloadRegular />}
            size="small"
            onClick={handleDownload}
          >
            {t("ai.download")}
          </Button>
        </div>
      </div>

      <div className={styles.textareaWrap}>
        <textarea
          ref={textareaRef}
          className={styles.textarea}
          value={AI_SKILL_CONTENT}
          readOnly
          spellCheck={false}
        />
      </div>

      <MessageBar intent="success" className={styles.tip} style={{ marginTop: "16px" }}>
        <MessageBarBody>{t("ai.summary")}</MessageBarBody>
      </MessageBar>
    </div>
  );
}
