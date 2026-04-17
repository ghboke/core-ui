import { useTranslation } from "react-i18next";
import {
  makeStyles,
  tokens,
  Button,
  Checkbox,
  Radio,
  RadioGroup,
  Switch,
  Slider,
  Input,
  Textarea,
  SpinButton,
  Combobox,
  Option,
  ProgressBar,
  Label,
  Divider,
  Accordion,
  AccordionItem,
  AccordionHeader,
  AccordionPanel,
  TabList,
  Tab,
  Badge,
  Dialog,
  DialogTrigger,
  DialogSurface,
  DialogTitle,
  DialogBody,
  DialogContent,
  DialogActions,
  Tooltip,
  Card,
  Text,
} from "@fluentui/react-components";
import {
  SettingsRegular,
  DeleteRegular,
  AddRegular,
  SaveRegular,
} from "@fluentui/react-icons";

const useStyles = makeStyles({
  preview: {
    border: `1px solid ${tokens.colorNeutralStroke2}`,
    borderRadius: tokens.borderRadiusLarge,
    padding: "24px",
    backgroundColor: tokens.colorNeutralBackground2,
  },
  row: {
    display: "flex",
    gap: "12px",
    alignItems: "center",
    flexWrap: "wrap",
  },
  col: {
    display: "flex",
    flexDirection: "column",
    gap: "12px",
  },
  box: {
    border: `1px dashed ${tokens.colorNeutralStroke1}`,
    borderRadius: tokens.borderRadiusMedium,
    padding: "12px",
    backgroundColor: tokens.colorNeutralBackground3,
    textAlign: "center",
    fontSize: "13px",
    color: tokens.colorNeutralForeground2,
  },
  splitDemo: {
    display: "flex",
    height: "160px",
    border: `1px solid ${tokens.colorNeutralStroke2}`,
    borderRadius: tokens.borderRadiusMedium,
    overflow: "hidden",
  },
  splitPane: {
    width: "160px",
    backgroundColor: tokens.colorNeutralBackground3,
    padding: "12px",
    borderRight: `1px solid ${tokens.colorNeutralStroke2}`,
    display: "flex",
    flexDirection: "column",
    gap: "4px",
  },
  splitContent: {
    flex: 1,
    padding: "12px",
    display: "flex",
    alignItems: "center",
    justifyContent: "center",
    color: tokens.colorNeutralForeground3,
  },
  navItem: {
    padding: "6px 10px",
    borderRadius: tokens.borderRadiusMedium,
    fontSize: "13px",
    cursor: "pointer",
    color: tokens.colorNeutralForeground2,
    ":hover": {
      backgroundColor: tokens.colorSubtleBackgroundHover,
    },
  },
  navItemActive: {
    padding: "6px 10px",
    borderRadius: tokens.borderRadiusMedium,
    fontSize: "13px",
    cursor: "pointer",
    fontWeight: 600,
    backgroundColor: tokens.colorSubtleBackgroundSelected,
    color: tokens.colorNeutralForeground1,
  },
  titleBarDemo: {
    display: "flex",
    alignItems: "center",
    height: "36px",
    backgroundColor: tokens.colorNeutralBackground3,
    borderRadius: tokens.borderRadiusMedium,
    paddingInline: "12px",
    gap: "8px",
  },
  titleBarTitle: {
    fontSize: "13px",
    fontWeight: 600,
    color: tokens.colorNeutralForeground1,
    flex: 1,
  },
  titleBarBtn: {
    width: "12px",
    height: "12px",
    borderRadius: "50%",
  },
  toastDemo: {
    backgroundColor: tokens.colorNeutralBackground1,
    border: `1px solid ${tokens.colorNeutralStroke2}`,
    borderRadius: tokens.borderRadiusMedium,
    padding: "10px 16px",
    display: "inline-flex",
    alignItems: "center",
    gap: "8px",
    boxShadow: `0 4px 8px rgba(0,0,0,0.14)`,
  },
  imageViewDemo: {
    width: "100%",
    height: "180px",
    backgroundColor: tokens.colorNeutralBackground3,
    borderRadius: tokens.borderRadiusMedium,
    display: "flex",
    alignItems: "center",
    justifyContent: "center",
    position: "relative",
    overflow: "hidden",
  },
  checkerboard: {
    position: "absolute",
    inset: 0,
    backgroundImage: `linear-gradient(45deg, ${tokens.colorNeutralBackground1} 25%, transparent 25%),
      linear-gradient(-45deg, ${tokens.colorNeutralBackground1} 25%, transparent 25%),
      linear-gradient(45deg, transparent 75%, ${tokens.colorNeutralBackground1} 75%),
      linear-gradient(-45deg, transparent 75%, ${tokens.colorNeutralBackground1} 75%)`,
    backgroundSize: "16px 16px",
    backgroundPosition: "0 0, 0 8px, 8px -8px, -8px 0px",
    opacity: 0.3,
  },
  imgPlaceholder: {
    width: "120px",
    height: "90px",
    backgroundColor: tokens.colorBrandBackground2,
    borderRadius: tokens.borderRadiusMedium,
    display: "flex",
    alignItems: "center",
    justifyContent: "center",
    color: tokens.colorBrandForeground2,
    fontSize: "24px",
    fontWeight: 700,
    zIndex: 1,
  },
});

export function ControlPreview({ controlName }: { controlName: string }) {
  const styles = useStyles();
  const { t } = useTranslation();

  const previews: Record<string, React.ReactNode> = {
    Button: (
      <div className={styles.row}>
        <Button>Default</Button>
        <Button appearance="primary">Primary</Button>
        <Button appearance="outline">Outline</Button>
        <Button appearance="subtle">Subtle</Button>
        <Button disabled>Disabled</Button>
      </div>
    ),
    IconButton: (
      <div className={styles.row}>
        <Button icon={<SettingsRegular />} appearance="subtle" />
        <Button icon={<DeleteRegular />} appearance="subtle" />
        <Button icon={<AddRegular />} appearance="primary" />
        <Button icon={<SaveRegular />} appearance="outline" />
      </div>
    ),
    CheckBox: (
      <div className={styles.col}>
        <Checkbox label="Unchecked option" />
        <Checkbox label="Checked option" checked />
        <Checkbox label="Disabled option" disabled />
      </div>
    ),
    RadioButton: (
      <RadioGroup defaultValue="opt1">
        <Radio value="opt1" label="Option A" />
        <Radio value="opt2" label="Option B" />
        <Radio value="opt3" label="Option C (disabled)" disabled />
      </RadioGroup>
    ),
    Toggle: (
      <div className={styles.col}>
        <Switch label="Feature enabled" defaultChecked />
        <Switch label="Feature disabled" />
        <Switch label="Locked" disabled defaultChecked />
      </div>
    ),
    Slider: (
      <div className={styles.col} style={{ width: "300px" }}>
        <Slider defaultValue={50} min={0} max={100} />
        <Slider defaultValue={30} min={0} max={100} step={10} />
        <Slider defaultValue={70} min={0} max={100} disabled />
      </div>
    ),
    TextInput: (
      <div className={styles.col} style={{ width: "320px" }}>
        <Input placeholder="Type here..." />
        <Input defaultValue="Read-only text" readOnly />
        <Input placeholder="Disabled" disabled />
      </div>
    ),
    TextArea: (
      <div style={{ width: "320px" }}>
        <Textarea placeholder="Multi-line text area..." rows={4} style={{ width: "100%" }} />
      </div>
    ),
    NumberBox: (
      <div className={styles.row}>
        <SpinButton defaultValue={42} min={0} max={100} step={1} />
        <SpinButton defaultValue={3.14} min={0} max={10} step={0.01} />
      </div>
    ),
    ComboBox: (
      <Combobox placeholder="Select an option" style={{ minWidth: "200px" }}>
        <Option>Option 1</Option>
        <Option>Option 2</Option>
        <Option>Option 3</Option>
        <Option disabled>Disabled Option</Option>
      </Combobox>
    ),
    Label: (
      <div className={styles.col}>
        <Text size={600} weight="bold">Title Label (24px bold)</Text>
        <Text size={400}>Body text label (16px)</Text>
        <Text size={200} style={{ color: tokens.colorNeutralForeground3 }}>Caption label (12px, muted)</Text>
      </div>
    ),
    ProgressBar: (
      <div className={styles.col} style={{ width: "320px" }}>
        <Label>Determinate (75%)</Label>
        <ProgressBar value={0.75} />
        <Label>Indeterminate</Label>
        <ProgressBar />
      </div>
    ),
    Separator: (
      <div className={styles.col} style={{ width: "320px" }}>
        <Text>Content above</Text>
        <Divider />
        <Text>Content below</Text>
        <Divider appearance="brand" />
        <Text>Brand separator</Text>
      </div>
    ),
    VBox: (
      <div className={styles.col} style={{ width: "240px" }}>
        <div className={styles.box}>Child 1</div>
        <div className={styles.box}>Child 2</div>
        <div className={styles.box}>Child 3</div>
      </div>
    ),
    HBox: (
      <div className={styles.row}>
        <div className={styles.box} style={{ flex: 1 }}>Child 1</div>
        <div className={styles.box} style={{ flex: 1 }}>Child 2</div>
        <div className={styles.box} style={{ flex: 1 }}>Child 3</div>
      </div>
    ),
    Grid: (
      <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr 1fr", gap: "8px", width: "300px" }}>
        <div className={styles.box}>1</div>
        <div className={styles.box}>2</div>
        <div className={styles.box}>3</div>
        <div className={styles.box} style={{ gridColumn: "span 2" }}>4 (colspan=2)</div>
        <div className={styles.box}>5</div>
      </div>
    ),
    Panel: (
      <div style={{ padding: "16px", backgroundColor: tokens.colorNeutralBackground3, borderRadius: tokens.borderRadiusMedium }}>
        <Text>Panel with background color</Text>
      </div>
    ),
    Spacer: (
      <div className={styles.row} style={{ width: "300px" }}>
        <Badge>Left</Badge>
        <div style={{ flex: 1, borderBottom: `1px dashed ${tokens.colorNeutralStroke1}` }} />
        <Badge>Right</Badge>
      </div>
    ),
    ScrollView: (
      <div style={{ height: "140px", overflowY: "auto", border: `1px solid ${tokens.colorNeutralStroke2}`, borderRadius: tokens.borderRadiusMedium, padding: "8px" }}>
        {Array.from({ length: 12 }, (_, i) => (
          <div key={i} style={{ padding: "6px 0", borderBottom: `1px solid ${tokens.colorNeutralStroke2}`, fontSize: "13px", color: tokens.colorNeutralForeground2 }}>
            Scrollable item {i + 1}
          </div>
        ))}
      </div>
    ),
    SplitView: (
      <div className={styles.splitDemo}>
        <div className={styles.splitPane}>
          <div className={styles.navItemActive}>Home</div>
          <div className={styles.navItem}>Settings</div>
          <div className={styles.navItem}>About</div>
        </div>
        <div className={styles.splitContent}>Content Area</div>
      </div>
    ),
    Splitter: (
      <div style={{ display: "flex", height: "120px", border: `1px solid ${tokens.colorNeutralStroke2}`, borderRadius: tokens.borderRadiusMedium, overflow: "hidden" }}>
        <div style={{ width: "40%", padding: "12px", backgroundColor: tokens.colorNeutralBackground3 }}>
          <Text size={200}>Left Panel (40%)</Text>
        </div>
        <div style={{ width: "4px", backgroundColor: tokens.colorNeutralStroke1, cursor: "col-resize" }} />
        <div style={{ flex: 1, padding: "12px" }}>
          <Text size={200}>Right Panel (60%)</Text>
        </div>
      </div>
    ),
    Stack: (
      <Card style={{ padding: "16px", width: "240px" }}>
        <TabList defaultSelectedValue="page1" size="small" style={{ marginBottom: "12px" }}>
          <Tab value="page1">Page 1</Tab>
          <Tab value="page2">Page 2</Tab>
        </TabList>
        <div className={styles.box}>Active page content</div>
      </Card>
    ),
    Expander: (
      <Accordion collapsible defaultOpenItems={["1"]}>
        <AccordionItem value="1">
          <AccordionHeader>Section A (expanded)</AccordionHeader>
          <AccordionPanel>
            <Text size={200}>Expanded content with details...</Text>
          </AccordionPanel>
        </AccordionItem>
        <AccordionItem value="2">
          <AccordionHeader>Section B (collapsed)</AccordionHeader>
          <AccordionPanel>
            <Text size={200}>More content here.</Text>
          </AccordionPanel>
        </AccordionItem>
      </Accordion>
    ),
    TitleBar: (
      <div className={styles.titleBarDemo}>
        <div style={{ width: "16px", height: "16px", borderRadius: "2px", backgroundColor: tokens.colorBrandBackground, display: "flex", alignItems: "center", justifyContent: "center", color: "white", fontSize: "9px", fontWeight: 700 }}>UI</div>
        <span className={styles.titleBarTitle}>My Application</span>
        <div className={styles.titleBarBtn} style={{ backgroundColor: tokens.colorNeutralForeground3 }} />
        <div className={styles.titleBarBtn} style={{ backgroundColor: tokens.colorNeutralForeground3 }} />
        <div className={styles.titleBarBtn} style={{ backgroundColor: "#d13438" }} />
      </div>
    ),
    NavItem: (
      <div style={{ width: "200px", display: "flex", flexDirection: "column", gap: "2px" }}>
        <div className={styles.navItemActive}>Home</div>
        <div className={styles.navItem}>Documents</div>
        <div className={styles.navItem}>Settings</div>
      </div>
    ),
    TabControl: (
      <div style={{ width: "320px" }}>
        <TabList defaultSelectedValue="tab1">
          <Tab value="tab1">General</Tab>
          <Tab value="tab2">Advanced</Tab>
          <Tab value="tab3">About</Tab>
        </TabList>
        <div style={{ padding: "16px 0" }}>
          <Text size={200}>Tab page content</Text>
        </div>
      </div>
    ),
    Dialog: (
      <Dialog>
        <DialogTrigger disableButtonEnhancement>
          <Button appearance="primary">Open Dialog</Button>
        </DialogTrigger>
        <DialogSurface>
          <DialogBody>
            <DialogTitle>Confirm Action</DialogTitle>
            <DialogContent>Are you sure you want to proceed? This action cannot be undone.</DialogContent>
            <DialogActions>
              <DialogTrigger disableButtonEnhancement>
                <Button appearance="secondary">Cancel</Button>
              </DialogTrigger>
              <Button appearance="primary">Confirm</Button>
            </DialogActions>
          </DialogBody>
        </DialogSurface>
      </Dialog>
    ),
    Toast: (
      <div className={styles.col}>
        <div className={styles.toastDemo}>
          <span style={{ color: "#107c10" }}>&#10004;</span>
          <Text size={200}>File saved successfully</Text>
        </div>
        <div className={styles.toastDemo}>
          <span style={{ color: "#d13438" }}>&#10008;</span>
          <Text size={200}>Operation failed</Text>
        </div>
        <div className={styles.toastDemo}>
          <span style={{ color: "#fde300" }}>&#9888;</span>
          <Text size={200}>Unsaved changes</Text>
        </div>
      </div>
    ),
    ContextMenu: (
      <Card style={{ width: "180px", padding: "4px" }}>
        <div style={{ padding: "6px 12px", fontSize: "13px", borderRadius: "4px", cursor: "pointer" }}>
          <span>Cut</span><span style={{ float: "right", color: tokens.colorNeutralForeground3, fontSize: "12px" }}>Ctrl+X</span>
        </div>
        <div style={{ padding: "6px 12px", fontSize: "13px", borderRadius: "4px", cursor: "pointer" }}>
          <span>Copy</span><span style={{ float: "right", color: tokens.colorNeutralForeground3, fontSize: "12px" }}>Ctrl+C</span>
        </div>
        <div style={{ padding: "6px 12px", fontSize: "13px", borderRadius: "4px", cursor: "pointer" }}>
          <span>Paste</span><span style={{ float: "right", color: tokens.colorNeutralForeground3, fontSize: "12px" }}>Ctrl+V</span>
        </div>
        <Divider style={{ margin: "4px 0" }} />
        <div style={{ padding: "6px 12px", fontSize: "13px", borderRadius: "4px", cursor: "pointer", color: "#d13438" }}>
          Delete
        </div>
      </Card>
    ),
    Flyout: (
      <Tooltip content="This is a flyout popup attached to the button" relationship="description" withArrow positioning="below">
        <Button appearance="outline">Hover for Flyout</Button>
      </Tooltip>
    ),
    ImageView: (
      <div className={styles.imageViewDemo}>
        <div className={styles.checkerboard} />
        <div className={styles.imgPlaceholder}>IMG</div>
      </div>
    ),
  };

  const content = previews[controlName];
  if (!content) {
    return (
      <div className={styles.preview} style={{ textAlign: "center", color: tokens.colorNeutralForeground3, fontSize: "13px", paddingBlock: "32px" }}>
        {t("controlDetail.noPreview")}
      </div>
    );
  }

  return <div className={styles.preview}>{content}</div>;
}
