import { makeStyles, tokens, MessageBar, MessageBarBody, Badge } from "@fluentui/react-components";
import { useTranslation } from "react-i18next";
import { CodeBlock } from "../components/CodeBlock";

const useStyles = makeStyles({
  page: {
    maxWidth: "960px",
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
  tip: {
    marginBottom: "24px",
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
  subSectionTitle: {
    fontSize: "16px",
    fontWeight: 600,
    color: tokens.colorNeutralForeground1,
    marginTop: "20px",
    marginBottom: "8px",
  },
  paragraph: {
    color: tokens.colorNeutralForeground2,
    lineHeight: "24px",
    marginBottom: "16px",
  },
  list: {
    color: tokens.colorNeutralForeground2,
    lineHeight: "28px",
    paddingInlineStart: "24px",
    marginBottom: "16px",
  },
  table: {
    width: "100%",
    borderCollapse: "collapse",
    marginBottom: "16px",
    fontSize: "13px",
  },
  th: {
    textAlign: "start",
    padding: "10px 12px",
    borderBottomWidth: "1px",
    borderBottomStyle: "solid",
    borderBottomColor: tokens.colorNeutralStroke2,
    color: tokens.colorNeutralForeground2,
    fontWeight: 600,
    fontSize: "12px",
    textTransform: "uppercase",
    letterSpacing: "0.4px",
    backgroundColor: tokens.colorNeutralBackground2,
  },
  td: {
    padding: "10px 12px",
    borderBottomWidth: "1px",
    borderBottomStyle: "solid",
    borderBottomColor: tokens.colorNeutralStroke3,
    color: tokens.colorNeutralForeground1,
    verticalAlign: "top",
  },
  code: {
    fontFamily: "'Cascadia Code', Consolas, 'Courier New', monospace",
    fontSize: "12px",
    backgroundColor: tokens.colorNeutralBackground3,
    paddingInline: "5px",
    paddingBlock: "2px",
    borderRadius: "3px",
  },
  badgeRow: {
    display: "flex",
    gap: "8px",
    flexWrap: "wrap",
    marginBottom: "16px",
  },
});

const cApiExample = `#include <ui_core.h>

// 假设窗口已经创建、root 树里有 id="submit" 的按钮和 id="email" 的输入框
void run_e2e(UiWindow win) {
    UiWidget root  = ui_window_get_root(win);
    UiWidget email = ui_widget_find_by_id(root, "email");
    UiWidget btn   = ui_widget_find_by_id(root, "submit");

    // 1) 聚焦 + 逐字符输入
    ui_debug_focus(win, email);
    ui_debug_type_text(win, L"user@example.com");

    // 2) 点击提交，真正触发 onClick 回调
    ui_debug_click(win, btn);

    // 3) 弹出上下文菜单并选子菜单里的项
    ui_debug_right_click_at(win, 400, 300);
    int path[] = { 2, 0 };  // "Paste Special" -> "Paste as Plain"
    ui_debug_menu_click_path(win, path, 2);

    // 4) 截图保存证据
    ui_debug_screenshot(win, L"after-submit.png");
}`;

const pipeExamplePs = `# PowerShell: 发送一条命令，读 JSON 响应
function Send-UiCmd($cmd) {
    $pipe = New-Object System.IO.Pipes.NamedPipeClientStream '.', 'ui_core_debug', 'InOut'
    $pipe.Connect(2000)
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($cmd)
    $pipe.Write($bytes, 0, $bytes.Length); $pipe.Flush()
    $buf = New-Object byte[] 65536
    $ms  = New-Object System.IO.MemoryStream
    while ($true) {
        try { $n = $pipe.Read($buf, 0, $buf.Length) } catch { break }
        if ($n -le 0) { break }
        $ms.Write($buf, 0, $n)
    }
    $pipe.Dispose()
    [System.Text.Encoding]::UTF8.GetString($ms.ToArray())
}

Send-UiCmd 'nav 2'                        # 切到第 2 页
Send-UiCmd 'check chk_sample toggle'      # 切换复选框
Send-UiCmd 'combo lang_combo 1'           # 下拉选第 2 项
Send-UiCmd 'rclick_at 600 400'            # 弹出右键菜单
Send-UiCmd 'menu_click_path 2/0'          # 点子菜单里的第 0 项
Send-UiCmd 'screenshot after.png'`;

const pipeExamplePy = `import win32file

def send(cmd: str) -> str:
    h = win32file.CreateFile(r'\\\\.\\pipe\\ui_core_debug',
        win32file.GENERIC_READ | win32file.GENERIC_WRITE,
        0, None, win32file.OPEN_EXISTING, 0, None)
    win32file.WriteFile(h, cmd.encode('utf-8'))
    _, data = win32file.ReadFile(h, 65536)
    win32file.CloseHandle(h)
    return data.decode('utf-8')

print(send('click nav_home'))
print(send('toggle tg_theme 1'))`;

const threadSafeC = `// 工作线程里有任何 ui_debug_* 或 widget mutation？包一层 invoke_sync：
static void impl(void* ud) {
    UiWidget btn = (UiWidget)(uintptr_t)ud;
    ui_debug_click(the_window, btn);
}
// 从任意线程调用：
ui_window_invoke_sync(the_window, impl, (void*)(uintptr_t)btn);`;

type ApiRow = { sig: string; desc: string };
const mouseApis: ApiRow[] = [
  { sig: "ui_debug_click(win, w)", desc: "MouseDown + MouseUp，触发 onClick" },
  { sig: "ui_debug_click_at(win, x, y)", desc: "窗口坐标点击" },
  { sig: "ui_debug_double_click(win, w)", desc: "连点两下" },
  { sig: "ui_debug_right_click(win, w) / _at(x, y)", desc: "等同 WM_RBUTTONUP，触发 onRightClick" },
  { sig: "ui_debug_hover(win, w)", desc: "MouseMove 进入到控件中心" },
  { sig: "ui_debug_drag(win, w, dx, dy) / drag_to(...)", desc: "拖拽，带中间 MouseMove" },
  { sig: "ui_debug_wheel(win, w, delta)", desc: "滚轮，自动向上查找 ScrollView" },
];

const keyApis: ApiRow[] = [
  { sig: "ui_debug_focus(win, w)", desc: "设置焦点并显示焦点环" },
  { sig: "ui_debug_blur(win)", desc: "移除焦点" },
  { sig: "ui_debug_key(win, vk)", desc: "WM_KEYDOWN 给焦点控件（复用 WndProc 派发）" },
  { sig: "ui_debug_type_char(win, ch) / type_text(win, text)", desc: "逐字符输入 WM_CHAR 等效" },
];

const controlApis: ApiRow[] = [
  { sig: "ui_debug_checkbox_set(win, w, 0|1) / _toggle", desc: "CheckBox" },
  { sig: "ui_debug_toggle_set(win, w, 0|1)", desc: "Toggle（Switch）" },
  { sig: "ui_debug_radio_select(win, w)", desc: "RadioButton，自动取消同组" },
  { sig: "ui_debug_combo_select(win, w, idx) / open / close", desc: "ComboBox" },
  { sig: "ui_debug_slider_set(win, w, v)", desc: "Slider 值变更 + onFloatChanged" },
  { sig: "ui_debug_number_set(win, w, v)", desc: "NumberBox" },
  { sig: "ui_debug_tab_set(w, idx)", desc: "TabControl 切页" },
  { sig: "ui_debug_expander_set(w, 0|1)", desc: "Expander 展开/折叠" },
  { sig: "ui_debug_splitview_set(w, 0|1)", desc: "SplitView 侧栏" },
  { sig: "ui_debug_flyout_show(fw, anchor) / hide", desc: "Flyout" },
  { sig: "ui_debug_text_set(w, wstr)", desc: "TextInput / TextArea / Label 赋值" },
  { sig: "ui_debug_scroll_set(w, y)", desc: "ScrollView" },
];

const menuApis: ApiRow[] = [
  { sig: "ui_debug_menu_is_open(win)", desc: "查询当前有无菜单打开" },
  { sig: "ui_debug_menu_item_count(win)", desc: "顶层项数量" },
  { sig: "ui_debug_menu_click_index / _id", desc: "按索引 / item_id 点击顶层项" },
  { sig: "ui_debug_menu_item_count_at(win, path, depth)", desc: "任意层级的子项数量" },
  { sig: "ui_debug_menu_click_path(win, path, depth)", desc: "按路径 [i0, i1, ...] 点击叶子（支持任意深度）" },
  { sig: "ui_debug_menu_close(win)", desc: "关闭菜单" },
  { sig: "ui_debug_set_menu_autoclose(0|1)", desc: "禁用 / 恢复菜单的前台变化自动关闭（自动化脚本必需）" },
];

const hwndApis: ApiRow[] = [
  { sig: "ui_debug_post_click / post_right_click(win, x, y)", desc: "Win32 PostMessage 方式注入鼠标" },
  { sig: "ui_debug_post_key(win, vk) / post_char(win, cp)", desc: "异步键盘" },
  { sig: "ui_debug_pump()", desc: "处理窗口消息队列（让 post_* 生效）" },
  { sig: "ui_window_invoke_sync(win, fn, ud)", desc: "跨线程调用 marshal 到 UI 线程" },
];

type CmdRow = { cmd: string; example: string; desc: string };
const pipeCmds: CmdRow[] = [
  { cmd: "tree / widget <id>", example: "widget flyoutBtn", desc: "整棵或单控件 JSON" },
  { cmd: "highlight <id>", example: "highlight flyoutBtn", desc: "红框高亮" },
  { cmd: "screenshot <path>", example: "screenshot out.png", desc: "保存 PNG" },
  { cmd: "nav <0-N>", example: "nav 3", desc: "切换 demo 页面" },
  { cmd: "click / hover / rclick <id>", example: "click flyoutBtn", desc: "鼠标模拟" },
  { cmd: "focus <id> / blur / key <vk|name>", example: "key enter", desc: "焦点 + 键盘" },
  { cmd: "type <text>", example: "type Hello", desc: "逐字符输入（需先 focus）" },
  { cmd: "check / toggle / radio <id>", example: "check chk1 toggle", desc: "勾选 / 单选" },
  { cmd: "combo <id> <idx>", example: "combo lang 1", desc: "下拉选中" },
  { cmd: "slider / number <id> <v>", example: "slider vol 0.75", desc: "数值控件" },
  { cmd: "tab / expander / splitview <id> ...", example: "expander e1 toggle", desc: "容器状态" },
  { cmd: "input / textarea / set_text <id> <text>", example: "input name Alice", desc: "文本控件赋值" },
  { cmd: "zoom / pan / rotate <id> ...", example: "rotate img 90", desc: "ImageView" },
  { cmd: "menu_is_open / menu_count / menu_count_at <path>", example: "menu_count_at 2", desc: "菜单查询" },
  { cmd: "menu_click <idx> / menu_click_path <i0/i1/...>", example: "menu_click_path 2/0", desc: "菜单点击（支持子菜单）" },
  { cmd: "post_click / post_rclick / post_key / post_char", example: "post_click 100 100", desc: "HWND 通道" },
  { cmd: "pump / invalidate / help", example: "pump", desc: "工具" },
];

export function Debug() {
  const styles = useStyles();
  const { t } = useTranslation();

  return (
    <div className={styles.page}>
      <h1 className={styles.title}>{t("debug.title")}</h1>
      <p className={styles.subtitle}>{t("debug.subtitle")}</p>

      <div className={styles.badgeRow}>
        <Badge appearance="outline">since 1.1.0</Badge>
        <Badge appearance="outline">60+ APIs</Badge>
        <Badge appearance="outline">45+ pipe commands</Badge>
      </div>

      <MessageBar intent="info" className={styles.tip}>
        <MessageBarBody>{t("debug.tip")}</MessageBarBody>
      </MessageBar>

      {/* Section 1: 两条通道 */}
      <div className={styles.section}>
        <h2 className={styles.sectionTitle}>{t("debug.channelsTitle")}</h2>
        <p className={styles.paragraph}>{t("debug.channelsDesc")}</p>
        <ul className={styles.list}>
          <li><strong>{t("debug.channelInternal")}</strong> {t("debug.channelInternalDesc")}</li>
          <li><strong>{t("debug.channelHwnd")}</strong> {t("debug.channelHwndDesc")}</li>
          <li><strong>{t("debug.channelPipe")}</strong> {t("debug.channelPipeDesc")}</li>
        </ul>
      </div>

      {/* Section 2: C API 快速开始 */}
      <div className={styles.section}>
        <h2 className={styles.sectionTitle}>{t("debug.cApiTitle")}</h2>
        <p className={styles.paragraph}>{t("debug.cApiDesc")}</p>
        <CodeBlock code={cApiExample} language="C" />
      </div>

      {/* Section 3: C API 分类参考 */}
      <div className={styles.section}>
        <h2 className={styles.sectionTitle}>{t("debug.apiRefTitle")}</h2>

        <h3 className={styles.subSectionTitle}>{t("debug.mouseGroup")}</h3>
        <ApiTable rows={mouseApis} sigLabel={t("debug.colSig")} descLabel={t("debug.colDesc")} styles={styles} />

        <h3 className={styles.subSectionTitle}>{t("debug.keyboardGroup")}</h3>
        <ApiTable rows={keyApis} sigLabel={t("debug.colSig")} descLabel={t("debug.colDesc")} styles={styles} />

        <h3 className={styles.subSectionTitle}>{t("debug.controlGroup")}</h3>
        <ApiTable rows={controlApis} sigLabel={t("debug.colSig")} descLabel={t("debug.colDesc")} styles={styles} />

        <h3 className={styles.subSectionTitle}>{t("debug.menuGroup")}</h3>
        <ApiTable rows={menuApis} sigLabel={t("debug.colSig")} descLabel={t("debug.colDesc")} styles={styles} />

        <h3 className={styles.subSectionTitle}>{t("debug.hwndGroup")}</h3>
        <ApiTable rows={hwndApis} sigLabel={t("debug.colSig")} descLabel={t("debug.colDesc")} styles={styles} />
      </div>

      {/* Section 4: pipe 协议 */}
      <div className={styles.section}>
        <h2 className={styles.sectionTitle}>{t("debug.pipeTitle")}</h2>
        <p className={styles.paragraph}>{t("debug.pipeDesc")}</p>

        <table className={styles.table}>
          <thead>
            <tr>
              <th className={styles.th} style={{ width: "42%" }}>{t("debug.colCmd")}</th>
              <th className={styles.th} style={{ width: "28%" }}>{t("debug.colExample")}</th>
              <th className={styles.th}>{t("debug.colDesc")}</th>
            </tr>
          </thead>
          <tbody>
            {pipeCmds.map((r) => (
              <tr key={r.cmd}>
                <td className={styles.td}><code className={styles.code}>{r.cmd}</code></td>
                <td className={styles.td}><code className={styles.code}>{r.example}</code></td>
                <td className={styles.td}>{r.desc}</td>
              </tr>
            ))}
          </tbody>
        </table>

        <h3 className={styles.subSectionTitle}>PowerShell</h3>
        <CodeBlock code={pipeExamplePs} language="PowerShell" />

        <h3 className={styles.subSectionTitle}>Python</h3>
        <CodeBlock code={pipeExamplePy} language="Python" />
      </div>

      {/* Section 5: 线程安全 */}
      <div className={styles.section}>
        <h2 className={styles.sectionTitle}>{t("debug.threadTitle")}</h2>
        <p className={styles.paragraph}>{t("debug.threadDesc")}</p>
        <CodeBlock code={threadSafeC} language="C" />
      </div>

      {/* Section 6: 已知限制 */}
      <div className={styles.section}>
        <h2 className={styles.sectionTitle}>{t("debug.limitsTitle")}</h2>
        <ul className={styles.list}>
          <li>{t("debug.limit1")}</li>
          <li>{t("debug.limit2")}</li>
          <li>{t("debug.limit3")}</li>
        </ul>
      </div>

      <p className={styles.paragraph}>
        {t("debug.fullRef")}{" "}
        <code className={styles.code}>docs/debug-simulation.md</code>
        {" · "}
        <code className={styles.code}>scripts/debug-smoke.ps1</code>
      </p>
    </div>
  );
}

function ApiTable({
  rows,
  sigLabel,
  descLabel,
  styles,
}: {
  rows: ApiRow[];
  sigLabel: string;
  descLabel: string;
  styles: ReturnType<typeof useStyles>;
}) {
  return (
    <table className={styles.table}>
      <thead>
        <tr>
          <th className={styles.th} style={{ width: "48%" }}>{sigLabel}</th>
          <th className={styles.th}>{descLabel}</th>
        </tr>
      </thead>
      <tbody>
        {rows.map((r) => (
          <tr key={r.sig}>
            <td className={styles.td}><code className={styles.code}>{r.sig}</code></td>
            <td className={styles.td}>{r.desc}</td>
          </tr>
        ))}
      </tbody>
    </table>
  );
}
