# Core UI — `.uix` SFC 详细指南

`.uix` 单文件组件的完整参考手册。

> **想给 AI 喂的速查表**：见 [`uix-ai-guide.md`](uix-ai-guide.md)（1 页，AI 生成
> 友好）。本文是配套的详细解释 —— widget 映射、CSS 子集、cookbook、限制。

**1.5.0（build 30）开始 QuickJS 路径成为唯一脚本路径**。旧的 `data: {…}` /
`methods: { name: stmt }` shorthand DSL 已删，所有 `<script>` 必须是
Vue 3 SFC `export default { … }` 形态。`<import>` 子组件系统也已删除。

**这不是浏览器**。`.uix` 是 Vue 风格的单文件组件 —— 编译成 Windows 原生控件，
没有 DOM、`document`、`fetch`、`XMLHttpRequest`、CSS 动画、CSS grid、float。
脚本由 QuickJS-NG (ES2020+) 求值，模板语法接近 HTML 子集。

---

## 1. 文件骨架

一个合法的 `.uix` 文件由若干顶层块组成（顺序任意，每种最多出现一个，`<style>`
除外可多块）：

```vue
<window .../>              <!-- 静态窗口配置，自闭合；可省略 -->

<link rel="stylesheet" href="..."/>   <!-- 外部 CSS，可多个 -->

<script>
export default {
  data()    { return { /* ... */ }; },
  computed: { /* ... */ },
  methods:  { /* ... */ }
}
</script>

<style [scoped]>           <!-- CSS；可多块 -->
  ...
</style>

<template>
  <!-- 模板部分必须恰好一个根元素 -->
  <div class="root">
    ...
  </div>
</template>
```

**`<template>` 块强制存在，且内部只允许恰好一个根元素。** 有多个兄弟要用
`<div>` 包起来。
**所有标签必须显式闭合**（L3 严格模式）：
`<br/>`、`<hr/>`、`<input .../>`、`<img .../>` 必须带 `/>`。

**只有一种文件类型 — Page**。`ui_page_load_file()` 加载，`ui_page_open_window()`
开窗。`<import>` 子组件系统在 1.5.0 已经删除；多页面应用要么手动复制
模板，要么在 C 端组合多个 page。

---

## 2. `<window>` 标签 — 静态窗口配置

加载时读一次，不参与响应式。

```html
<window title="应用名"
        width="800" height="600"
        min-width="400" min-height="300"
        resizable="true"
        centered="true"
        frameless="true"
        theme="light"/>
```

| 属性 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `title` | 字符串 | `"Core UI"` | UTF-8，内部转 UTF-16 |
| `width` | 整数 | `800` | 初始宽（DIP）|
| `height` | 整数 | `600` | 初始高 |
| `min-width` | 整数 | 0（无限制）| 拖动最小宽度 |
| `min-height` | 整数 | 0 | 拖动最小高度 |
| `resizable` | `true`/`false` | `true` | 加 `WS_THICKFRAME` |
| `centered` | `true`/`false` | `true` | 屏幕居中开窗 |
| `frameless` | `true`/`false` | 看 caller | `true` = 自定义 chrome（需配 `<TitleBar>`）；`false` = 系统原生标题栏 |
| `theme` | `light` / `dark` | 继承 | 首次绘制前切主题 |

当 `frameless="true"` 时，模板里**必须**放 `<TitleBar>` 组件作为 min/max/close 按钮
+ 窗口拖动。按钮由 `WireTitleBar()` 在 `SetRoot` 时自动绑定。

当 `frameless="false"` 时，**不要**加 `<TitleBar>`——会和系统标题栏重叠。

---

## 3. `<script>` 块 — 响应式状态

Vue 3 Options API 形态，**必须**是 `export default { ... }`。QuickJS-NG 求值，
完整 ES2020+ 语法。

```html
<script>
export default {
  data() {
    return {
      count: 0,
      user: { name: "Alice", age: 30 },
      tags: ["a", "b", "c"]
    };
  },
  computed: {
    doubled() { return this.count * 2; },
    message() { return `Hello, ${this.user.name}!`; }
  },
  methods: {
    inc()    { this.count++; },
    greet()  { this.user = { ...this.user, name: this.user.name + "!" }; },
    bump()   {
      if (this.count > 5) this.count = 0;
      else                this.count++;
    }
  }
}
</script>
```

模板里写 `{{ count }}` —— 编译期由 expr-rewriter 自动加 `this.` 前缀。
**不要**自己写 `this.`，方法/computed 定义里因为是真实 JS 函数才需要。

### 3.1 `data()` — 初始值

- 必须是函数，返回对象（Vue 3 强约束，组件实例隔离）。
- Value：任意合法 JS 表达式 —— 字面量、数组/对象、模板字符串、外部 import（不支持）。
- 嵌套对象随意深：`user: { addr: { city: "X" } }`。
- 注释：JS 标准 `//` 或 `/* */`。`#` 注释**不工作**（QuickJS 解析器不认）。

### 3.2 `computed: { name() { ... } }` — 派生值

- 任意 `this.X` 读取自动收集依赖；该 X 变化时本 computed 标记 dirty，下次读时重算。
- **lazy memo** —— 没人读就不重算。
- 可链式依赖另一个 computed（依赖图自动解）。
- Body 是函数体，必须 `return`。

```js
computed: {
  filtered() { return this.tags.filter(t => t.includes(this.query)); },
  summary()  { return `${this.tags.length} items (${this.done.length} done)`; },
  total()    { return this.items.reduce((acc, x) => acc + x.price, 0); },
  // 链式：triple 依赖 doubled
  doubled()  { return this.count * 2; },
  triple()   { return this.count + this.doubled; }
}
```

### 3.3 `methods: { name() { ... } }` — 事件 handler

- 被 `@event="methodName"` 或 `@event="methodName(arg)"` 触发。
- Body 是任意 JS 函数体；可读写 `this.X`，可调其它 `this.method()`。
- 顶层状态赋值会触发响应式更新；嵌套对象 / 数组原地 mutation 不会触发父属性
  watcher，列表更新建议整体替换：

```js
methods: {
  inc()        { this.count++; },
  resetAll()   { this.count = 0; this.tags = []; },
  bump()       {
    if (this.count >= 10) this.count = 0;
    else                  this.count++;
  },
  addTag(tag)  {
    // ❌ this.tags.push(tag)        — 原地改数组，不触发依赖 tags 的 binding
    // ✅ 整体替换，触发 tags 写入
    this.tags = [...this.tags, tag];
  },
  total()      {
    // 计算逻辑也可以这么写，但改读 computed 更好
    return this.items.reduce((acc, x) => acc + x.price, 0);
  }
}
```

### 3.4 内置全局

模板里有两个内置全局（不需要在 `data()` 声明，但走 `this.` 前缀解析）：

| 名 | 作用 |
|----|------|
| `this.$t(key)` | i18n 翻译 —— 内部读 `this.$locale`，locale 切换自动重 fire |
| `this.$locale` | 当前 locale 字符串，反应式 |

### 3.5 不支持的特性（显式列出）

- `<script setup>` / 组合式 API —— 不支持，仅 Options API
- `ref()` / `reactive()` / `computed()` / `watch()` / `watchEffect()` —— 不暴露给用户
- lifecycle hooks (`mounted` / `updated` / `unmounted` / `beforeDestroy`) —— 不支持
- `props` / `emit` / `<slot>` / `<import>` 子组件系统 —— 1.5.0 已删
- `provide` / `inject` / `Teleport` / `KeepAlive` / `Suspense` —— 不支持
- 自定义 directive (`v-foo`) —— 不支持
- 异步组件 / router / npm 生态 —— 不接 webview，没法用
- 嵌套对象 mutation (`this.user.name = "X"` / `this.items[0].x = 1` /
  `this.items.push(...)`) —— **不响应**，必须整替
- 自动 batching / microtask flushing —— 同步触发

---

## 4. `<style>` 和 `<style scoped>`

可有多块，每块独立 scoped/global。`scoped` 会给所有规则追加属性选择器
（`.foo` → `.foo[data-scope~="p1"]`），同时给每个元素打 `data-scope="p1"`。
单页场景下 scoped 和 global 效果一样；组件系统就位后会有区别。

```html
<style scoped>
  :root {
    --accent: #0078d4;
    --bg: #f3f3f3;
  }
  .card {
    padding: 16px;
    background: white;
    border-radius: 8px;
    box-shadow: 0 2px 8px rgba(0,0,0,0.08);
  }
  .row { flex-direction: row; gap: 8px; }
  button:hover { opacity: 0.85; }
</style>
```

### 4.1 支持的选择器

| 形式 | 示例 |
|---|---|
| 类型 | `div`、`button`、`TitleBar` |
| 类 | `.primary` |
| ID | `#submit` |
| 后代 | `.card .title` |
| 子元素 | `.card > .title` |
| 属性 | `[type="text"]`、`[disabled]` |
| 伪类（状态）| `:hover`、`:pressed`、`:focus`、`:disabled`、`:checked`、`:root` |
| 并列 | `h1, h2, .title` |
| 复合 | `button.primary:hover` |

**不支持**：`+` 相邻兄弟、`~` 通用兄弟、`:nth-*`、`::before`、`::after`、`:not()`、
`*` 通配符。

### 4.2 支持的属性

**盒模型**：`width`、`height`、`min-width`、`max-width`、`min-height`、`max-height`
（**自 build 9 起 min/max-* 也接受 `%`**，按 parent 内容尺寸解析），
`padding`、`padding-top/right/bottom/left`、`margin`、`margin-*`、`border-radius`、
`box-sizing`（隐式 border-box）、`border` / `border-width/color/style`。

**背景**：`background`、`background-color`。

**文字**：`color`、`font-family`、`font-size`、`font-weight`（`bold` 或 `700`+）、
`font-style`、`line-height`、`text-align`、`white-space`。
`<label>` / `<span>` / `<small>` / `<strong>` / `<em>` / `<a>` 默认 `wrap=true`，
长文本自动换行；不想换行的设 `white-space: nowrap` 或固定 `width`。

**布局**（通过 VBox/HBox 映射为 flex）：
- `display: flex | block | none`
- `flex-direction: row | column`（决定 factory 阶段生成 HBox 还是 VBox）
- `justify-content: flex-start | flex-end | center | space-between | space-around | space-evenly`
- `align-items: flex-start | flex-end | center | stretch | baseline`（**自 build 12 起支持**；
  `baseline` 退化为 `center`，库无基线对齐）
- `flex-grow` / `flex-shrink` / `flex-basis` / `flex`（简写；当前 shrink 是简化实现）
- `flex-wrap: wrap | nowrap | wrap-reverse`（**自 build 10 起 HBox 支持**；
  超宽时按 children 自然宽度打包多行；`wrap-reverse` 等同 `wrap`）
- `gap` / `row-gap` / `column-gap`
- `position: absolute` + `top/right/bottom/left`（部分支持）

**视觉**：
- `opacity`、`visibility`、`overflow`、`cursor`
- `box-shadow: X Y blur spread color`（分层圆角矩形渲染）
- `backdrop-filter: blur(Npx)` / `backdrop-blur: Npx`（取当前控件背后已绘制内容做实时圆角背景模糊；子控件不参与模糊）
- `transform: translate(Xpx, Ypx) rotate(deg) scale(x, y)`
- `transition: prop duration easing`（`opacity`、`background-color`、`width`、
  `height` 等可动画属性走 `Animations().Animate`）

**SVG 属性**（**自 build 12 起 CSS 命中 `<path>`/`<circle>`/...**）：
- `fill` / `stroke`：颜色、`url(#gradient)` 引用、`currentColor`、`none`
- `fill-opacity` / `stroke-opacity`、`stroke-width`
- `stroke-dasharray`、`stroke-linecap` (`butt|round|square`)
- `stroke-linejoin` (`miter|round|bevel`)
- `opacity`（整体透明度）
- 优先级：inline `style="..."` > stylesheet rule > presentation attr (`fill="..."`) > 默认黑
- `currentColor` 沿父级链找最近的 `color: ...` 设置（匹配浏览器 inherit 语义）
- `:hover path { ... }` / `.parent.active .icon path { ... }` 等 descendant selector
  在 hover / 类切换时实时重算

**值类型**：
- 长度：`px`、`%`、`em`、`rem`、`vw`、`vh`、`auto`
- 颜色：`#RGB` / `#RRGGBB` / `#RRGGBBAA` / `rgb()` / `rgba()` / `hsl()` / `hsla()` /
  命名色（约 20 个标准色名）/ `currentColor`（仅 SVG fill/stroke）
- `var(--name)` 带可选 fallback：`var(--accent, blue)`
- `calc(100% - 16px)`—— 支持 `+ - * /` 和嵌套

### 4.3 不支持的 CSS

**不支持**：grid、float、`!important`、CSS 动画（`@keyframes`）、`filter`、`clip-path`、
`@media`、`@import`、`@font-face`、`content`、伪元素 (`::before`/`::after`)、
SVG `<g>` 分组、SVG `<filter>`/`<mask>`/`<clipPath>`、SMIL animate。

---

## 5. 模板指令

### 5.1 文本插值 `{{ expr }}`

```html
<p>Hello, {{ userName }}! Count: {{ count * 2 }}</p>
<span>{{ user.name.upper() }}</span>
<div>{{ `Active: ${active ? "yes" : "no"}` }}</div>
```

#### `@key` 语法糖（i18n，自 build 20）

整段文本是单一 i18n 引用时可以写 `@key`，编译期自动 desugar 成
`{{ $t('key') }}`：

```html
<label>@app.title</label>          <!-- == {{ $t('app.title') }} -->
<button>@btn.save</button>          <!-- == {{ $t('btn.save') }} -->
```

`@` 后只接 `[A-Za-z0-9_.-]`，混合内容不算（`"Hello @x"` 是字面量；想拼接
用 `{{ "Hello, " + $t('user.name') }}`）。属性值的 i18n 用显式 binding：
`:title="$t('window.title')"`。`@click` / `@change` 等事件 attr **不受影响**。

### 5.2 属性绑定 `:attr="expr"`

```html
<div :class="active ? 'on' : 'off'">...</div>
<button :disabled="loading">Save</button>
```

常用可绑属性：`class`、`visible`、`enabled`、`value`、`checked` / `selected` /
`on`、`width` / `height`、`left` / `top` / `right` / `bottom`、`opacity`、
`color`、`background-color`、`src`、`wrap`。`:style` 不做运行时 CSS 注入，
需要动态样式时优先切换 `:class` 或绑定具体属性。

### 5.3 事件绑定 `@event="..."`

```html
<button @click="save">Save</button>
<button @click="remove(item.id)">Delete</button>
```

支持的事件：`@click`、`@mousedown`、`@mousemove`、`@mouseup`、`@mouseenter`、
`@mouseleave`、`@contextmenu`、`@wheel`、`@dblclick`、`@submit`、`@focus`、
`@blur`、`@change`、`@input`。

鼠标事件的 handler 收到对标 DOM MouseEvent 的 `$event` 对象：
`offsetX/offsetY`（控件局部 DIP 坐标）、`clientX/clientY`（窗口坐标）、
`button`（0 左 / 1 中 / 2 右，move/wheel 为 -1）、`buttons`（位掩码 1 左 2 右 4 中）、
`ctrlKey/shiftKey/altKey`；`@wheel` 另有 `deltaY`（正值 = 向下滚，web 约定）和
`deltaX`（横向滚轮）。旧字段 `x/y/delta` 保留兼容。`@mouseleave` 无 `$event`
（光标已不在控件上，无有意义坐标）。`@mousemove` 与 DOM 一致悬停即触发；按住
拖动期间事件持续发给按下的控件（等价 pointer capture）。`@contextmenu` 在右键
释放时沿父链冒泡到最近的监听者，不影响宿主窗口的原生右键菜单回调。
`change/input` 收到控件标量值。

**拖放（HTML5 风格）**：任意元素设 `draggable="true"`（或直接给
`drag-data="payload"`，含载荷即隐含可拖）后按住移过 4 DIP 即开始拖动，窗口画
拖影胶囊。目标侧声明 `drop-target="true"` 或绑定 `@drop` 即接受投放（不需要
HTML 的 dragover-preventDefault）；拖动悬停时目标进入 CSS `:drag-over` 伪类。
事件：源侧 `@dragstart`（`$event.data` 为载荷，可改写——等价
dataTransfer.setData）、`@dragend`（`$event` 为 bool，是否成功投放）；目标侧
`@dragenter`、`@dragover`、`@dragleave`、`@drop`（`$event.data` + 鼠标坐标字段）。
拖动结束不触发 `@click`。C++ 宿主对应 `ui_widget_set_draggable /
set_drag_data / set_drop_target / on_drop / on_drag_end`。

### 5.4 `v-if="cond"`

条件为真时挂载元素及其子树；为假时**完全卸载**（不是隐藏）。状态在隐藏时销毁。
如果要保留 widget 身份和内部状态，用 `v-show` 或 `:visible`。

```html
<p v-if="count > 0">Clicked {{ count }} times</p>
<p v-if="count == 0">No clicks yet</p>
```

### 5.5 `v-for="(item, i) in list" :key="expr"`

遍历 `list`。`item` 和 `i`（可选）是循环局部变量。`:key` 启用 keyed diff（列表更新时
复用 widget）。

```html
<span v-for="tag in tags" :key="tag">{{ tag }}</span>
<li v-for="(todo, idx) in todos" :key="todo.id">#{{ idx }} {{ todo.text }}</li>
```

列表项是对象时用 `.` 访问字段：`{{ todo.text }}`。

**`v-if` 嵌套在 `v-for` 里工作正常**（每次迭代对循环作用域求值一次）。

### 5.6 `v-model="name"`

双向绑定。展开为 `:value="name"` + 一个把变化写回 `name` 的 `@change`/`@input`。
`name` 可以是点号路径；若不存在会自动创建。

支持的元素：
| 元素 | 值类型 | 调用的 widget 方法 |
|---|---|---|
| `<input type="text"/>` | 字符串 | `TextInputWidget::SetText` |
| `<textarea/>` | 字符串 | `TextAreaWidget::SetText` |
| `<input type="checkbox"/>` | 布尔 | `CheckBoxWidget::SetCheckedImmediate` |
| `<Toggle/>` | 布尔 | `ToggleWidget::SetOnImmediate` |
| `<input type="radio"/>` | 布尔 | `RadioButtonWidget` |
| `<input type="range"/>` | 数字 | `SliderWidget::SetValue`（写回时若 state 原值是 int 自动 round） |
| `<input type="number"/>` | 数字 | `NumberBoxWidget::SetValue` |
| `<select>` | 数字（索引）| `ComboBoxWidget::SetSelectedIndex` |

初始值通过 `:value` 路径从 `data` 读取。

#### `:selected` / `:checked` 反应式绑定（自 build 9）

不想要双向同步、只想"由 state 决定 radio/checkbox/toggle 的视觉 selected"，
用 `:selected="expr"` 或 `:checked="expr"`（两者别名等价）：

```html
<!-- radio 组用 :selected 比 :class="x=='val' ? 'sel' : ''" 直接 -->
<input type="radio" name="size" :selected="size=='small'"  @click="selSmall"/>
<input type="radio" name="size" :selected="size=='medium'" @click="selMedium"/>
<input type="radio" name="size" :selected="size=='large'"  @click="selLarge"/>
```

`:selected` 直接驱动 `RadioButtonWidget::selected_` / `CheckBoxWidget::checked_` /
`ToggleWidget::on_`，比 `:class` 加 CSS sel 模式更可靠（CSS class 切换不会改
内部 selected 状态，path 渲染等也不会更新）。

---

## 6. 表达式语言

### 6.1 字面量

```
123         # 数字
3.14
1e-5
"hello"     # 字符串（单引号或双引号）
'world'
true        false       null
[1, 2, 3]   # 数组
{ k: v }    # 对象
`template ${expr}`    # 模板字符串
```

### 6.2 运算符（优先级从高到低）

```
.  [ ]  ( )              成员、下标、函数调用
- !                      一元取负、逻辑非
* / %
+ -                      任一侧是字符串时做字符串拼接
< <= > >=
== !=
&&                       短路
||                       短路
? :                      三元（右结合）
=>                       箭头（最低；只在参数位置或直接作为表达式）
```

### 6.3 箭头函数

- `x => body`（单参，无括号）
- `(x, y) => body`（多参）
- `() => body`（无参）
- Body 是表达式（不能是语句块）
- 动态捕获作用域链；参数遮蔽外层同名变量。

```
tags.filter(t => t.length > 3)
items.reduce((acc, x) => acc + x.price, 0)
users.map(u => `${u.name} (${u.age})`)
```

### 6.4 模板字符串

```
`Hello ${user.name}`
`${count} of ${total} (${(100 * count / total).toFixed(1)}%)`
`outer ${`inner ${nested}`}`           # 嵌套 OK
`escape \`backtick\` and \${literal}`
```

转义序列：`\n \t \r \\ \` \$`。

### 6.5 成员和下标访问

```
user.name
user.addr.city
items[0]
items[i]
users["admin"]
obj.unknown          # 返回 null（不报错）
```

### 6.6 方法调用

`obj.method(args)` 会按对象类型分派到内置方法（见 §7）。可链式调用：
`tags.filter(...).map(...).join(",")`。

---

## 7. 内置方法

### 7.1 Array

| 方法 | 返回 | 说明 |
|---|---|---|
| `.length` | 数字 | 属性，不是方法 |
| `.push(x, …)` | 新数组 | **函数式**；用 `arr = arr.push(x)` |
| `.pop()` | 新数组 | 去掉末尾 |
| `.concat(other…)` | 新数组 | 数组一层拍平 |
| `.slice(start, end?)` | 新数组 | 负索引从末尾计 |
| `.indexOf(x)` | 数字 | 找不到返回 -1 |
| `.includes(x)` | 布尔 | |
| `.join(sep?)` | 字符串 | sep 默认 `","` |
| `.reverse()` | 新数组 | |
| `.filter(fn)` | 新数组 | `fn(item, i) → bool` |
| `.map(fn)` | 新数组 | `fn(item, i) → any` |
| `.find(fn)` | 元素或 null | 第一个匹配 |
| `.some(fn)` | 布尔 | |
| `.every(fn)` | 布尔 | |
| `.forEach(fn)` | null | |
| `.reduce(fn, init?)` | 任意 | `fn(acc, item, i) → any` |

所有方法都是**不可变**的——都返回新数组。要触发响应式必须赋值回去：
`tags = tags.push("x")`。

### 7.2 String

| 方法 | 返回 |
|---|---|
| `.length` | 数字（属性）|
| `.upper()` / `.toUpperCase()` | 字符串 |
| `.lower()` / `.toLowerCase()` | 字符串 |
| `.trim()` | 字符串 |
| `.slice(start, end?)` / `.substring(start, end?)` | 字符串 |
| `.indexOf(s)` | 数字 |
| `.includes(s)` | 布尔 |
| `.split(sep)` | 数组 |
| `.replace(from, to)` | 字符串 —— **只替换第一个匹配** |

### 7.3 Number

| 方法 | 返回 |
|---|---|
| `.toFixed(n)` | 字符串 |
| `.toString()` | 字符串 |

### 7.4 Object

没有方法。用 `.field` 成员访问或 `["field"]` 下标访问。

---

## 8. 模板标签 → Widget 映射

| 标签 | Widget | 说明 |
|---|---|---|
| `<div>` | `VBoxWidget`（或 `HBoxWidget` 如果 `flex-direction: row`）| 通用容器 |
| `<span>` | `LabelWidget` | |
| `<p>` | `LabelWidget`（支持换行）| **没有 inline 流**——子元素 `<strong>`/`<em>` 会堆叠显示，不会 inline 混排 |
| `<h1>`–`<h6>` | `LabelWidget` | 预设字号 + 粗体 |
| `<a>` | `LabelWidget` | 加 cursor:pointer（不自动导航）|
| `<button>` | `ButtonWidget` | CSS `background-color` 映射 `SetCustomBgColor` |
| `<input type="text"/>` | `TextInputWidget` | |
| `<input type="checkbox"/>` | `CheckBoxWidget` | |
| `<input type="radio"/>` | `RadioButtonWidget` | `name` 属性作为同组标识 |
| `<input type="range"/>` | `SliderWidget` | 解析 `min`/`max`/`value` 静态属性（自 build 9 起，之前硬编码 0..100） |
| `<input type="number"/>` | `NumberBoxWidget` | 解析 `min`/`max`/`value`/`step`；按 `step` 自动推导显示小数位（0.1→1, 0.01→2）|
| `<textarea/>` | `TextAreaWidget` | 多行 |
| `<select>` | `ComboBoxWidget` | `<option>Label</option>` 子节点自动填充；`value` 属性目前不参与选择值，`v-model` 使用索引 |
| `<ul>` / `<ol>` | `VBoxWidget` | |
| `<li>` | `HBoxWidget` | |
| `<hr/>` | `SeparatorWidget` | |
| `<img src="名字" object-fit=".."/>` | `ImageWidget` | **走资源解析器**，src 是 ui::asset 的 key，不是路径；object-fit 支持 `fill` / `contain`(默认) / `cover` / `none`（自 build 19）|
| `<custom id="..."/>` 或 `<Custom>` | `CustomWidget` | 自绘 widget，C 端用 `ui_widget_find_by_id` 拿 handle 后挂 `ui_custom_on_draw` / `ui_custom_on_mouse_*` 回调（自 build 21）|
| `<menu id="..." trigger="#elem" event="click\|rclick">` 含 `<menuitem>` / `<separator/>` / 嵌套 `<menu text="...">` | `ContextMenu`（不渲染） | 见下方 §17 完整菜单参考 |
| `<link rel="stylesheet" href="名字"/>` | (元数据，无 widget) | href 走资源解析器，CSS 内容并入页面 stylesheet；inline `<style>` 在 cascade 里覆盖外部 link（自 build 19）|
| `<label>` / `<span>` / `<small>` / `<strong>` / `<em>` / `<a>` | `LabelWidget` | **默认 wrap=true**（自 build 9）；嵌套 element 子节点编译期报错（自 build 11） |
| `<p>` | `LabelWidget` | wrap=true；嵌套 element 子节点同上 |
| `<TitleBar title="..."/>` | `TitleBarWidget` | 自动绑定 min/max/close 到窗口 |
| `<ScrollView>` | `ScrollViewWidget` | 自动把子节点包装成单一 content；多子自动套 VBox |
| `<Toggle>text</Toggle>` | `ToggleWidget` | 开关 |
| `<ProgressBar min max value indeterminate/>` | `ProgressBarWidget` | 静态 value 或 `indeterminate="true"`（自 build 9）滑动 stripe |
| `<Expander>header</Expander>` | `ExpanderWidget` | 可展开头部 |
| `<Separator/>` | `SeparatorWidget` | |
| `<svg>` + `<path>`/`<circle>`/`<rect>`/`<ellipse>`/`<line>`/`<polygon>`/`<polyline>` | `SvgWidget` + `SvgShape` | 子图形 fold 进 widget；CSS 命中 path（自 build 12）|
| `<gh-img-view id="..."/>` | `GhImgViewWidget` | 通用瓦块画布；数据全部由宿主经 `ui_gh_img_view_*` C API 喂 |
| `<ocr-img-view id="..."/>` | `OcrImgViewWidget` | 图片划词画布；数据经 `ui_ocr_img_view_*` C API 喂（自 build 277，见下方 §8.1）|
| 未知 `CamelCase` 标签 | 报错，给 edit-distance 提示 | 只有预注册的 C++ widget 能映射 |

### 特殊构造属性

| 标签 | 属性 | 作用 |
|---|---|---|
| `<TitleBar>` | `title`、`show-minimize`、`show-maximize`、`show-close`、`show-icon`（都是 `true`/`false`）| |
| `<ProgressBar>` | `min`、`max`、`value`（数字）、`indeterminate`（true/1 = 动画 stripe）| 构造时解析 |
| `<input type="range">` | `min`、`max`、`value` | |
| `<input type="number">` | `min`、`max`、`value`、`step` | step 决定显示小数位 |
| `<input type="radio">` | `name` | 单选组标识；同 group 互斥跨整树（不限同 parent） |
| `<svg>` | `width`、`height`、`viewBox` | viewBox 决定坐标系，width/height 决定 widget rect |
| `<ocr-img-view>` | `highlight-color`（CSS 颜色，含 `#RRGGBBAA`）、`rotation`（90° 倍数）| 构造时解析；不传则用主题 accent + 0.35 alpha |

### 8.1 `<ocr-img-view>` — 图片划词画布（自 build 277）

标签只负责把 widget 放进布局树；**图像和 OCR 文本项全部由宿主经 C API 喂**，
跟 `<gh-img-view>` 同款分工。别名：`ocr-img-view` / `ocr_img_view` /
`OcrImgView` / `ocr-img`。

```html
<div class="body">
  <ocr-img-view id="ocr" class="canvas" highlight-color="#2680ebA0"/>
  <ScrollView class="side">
    <label id="line_row" :class="l.cls" v-for="l in lines">{{ l.text }}</label>
  </ScrollView>
</div>
```

宿主侧（C）：

```c
UiWidget view = ui_widget_find_by_id(ui_page_root(page), "ocr");
ui_ocr_img_view_set_image(view, win, bgra, w, h, w * 4);   /* 检查返回值！ */
ui_ocr_img_view_set_text(view, items, count, sizeof(UiOcrTextItem),
                          UI_OCR_GRANULARITY_BLOCK);
ui_ocr_img_view_fit(view);
```

**列表 ↔ 画布双向联动**（`select_range` / `selection_range` / `item_meta`）：

```c
/* 列表 → 画布: 点第 i 行 → 找出该行的字下标区间 → 选中 */
ui_ocr_img_view_select_range(view, firstItemOfLine, lastItemOfLine);

/* 画布 → 列表: 划词后反查行号 */
uint32_t first, last, block, line, word;
if (ui_ocr_img_view_selection_range(view, &first, &last) &&
    ui_ocr_img_view_item_meta(view, first, &block, &line, &word)) {
    /* line == 宿主喂入时写的 line 字段 —— BLOCK 粒度拆字后仍然保留 */
}
```

下标空间是**拆字后**的 item（`ui_ocr_img_view_item_count`），BLOCK 粒度下
远大于宿主喂入的块数，必须靠 `item_meta` 建映射，不能假设一一对应。

完整可运行例子：`demo/ocr_uix_demo.cpp` + `demo/ocr_uix_demo.uix`，
自动化回归 `test/scripts/ocr_uix_demo_test.ps1`。

---

## 9. 标准模板

新建 `.uix` 页面的样板：

```vue
<window title="My App" width="800" height="600"
        min-width="400" min-height="300"
        resizable="true" centered="true"
        frameless="true" theme="light"/>

<script>
export default {
  data()    { return { /* 初始状态 */ }; },
  computed: { /* 派生值 */ },
  methods:  { /* handler */ }
}
</script>

<style scoped>
  :root { --accent: #0078d4; }
  .shell { gap: 0; }
  ScrollView { flex: 1; }
  .content { padding: 20px; gap: 16px; }
  .card {
    padding: 16px; background: white; border-radius: 8px;
    gap: 10px; box-shadow: 0 2px 8px rgba(0,0,0,0.08);
  }
  .row { flex-direction: row; gap: 8px; }
  .primary { background: var(--accent); color: white; }
</style>

<template>
  <div class="shell">
    <TitleBar title="My App"/>
    <ScrollView>
      <div class="content">
        <!-- 页面主体 -->
      </div>
    </ScrollView>
  </div>
</template>
```

---

## 10. 常用模式

> 下面每个示例只展示 `<script>` 块 + 模板片段。实际写入 `.uix` 文件时模板部分必须
> 用 `<template>...</template>` 包起来，并且整个文件可能还要加上 `<window/>` 和
> `<style>` 块（参考 §9 标准模板）。

### 10.1 计数器

```html
<script>
export default {
  data() { return { count: 0 }; },
  methods: {
    inc()   { this.count++; },
    dec()   { this.count--; },
    reset() { this.count = 0; }
  }
}
</script>
<div class="row">
  <button class="primary" @click="inc">+1</button>
  <button @click="dec">−1</button>
  <button @click="reset">Reset</button>
  <label>{{ count }}</label>
</div>
```

### 10.2 表单 + v-model

```html
<script>
export default {
  data() { return { name: "", agree: false, role: 0 }; }
}
</script>
<input v-model="name" placeholder="Your name"/>
<input type="checkbox" v-model="agree"/>
<label>I agree</label>
<select v-model="role">
  <option value="0">Admin</option>
  <option value="1">User</option>
</select>
<label v-if="agree">{{ `Thanks, ${name}!` }}</label>
```

### 10.3 带过滤的列表

```html
<script>
export default {
  data() {
    return {
      query: "",
      items: [{name:"Alice"},{name:"Bob"},{name:"Carol"}]
    };
  },
  computed: {
    matching() {
      var q = this.query.toLowerCase();
      return this.items.filter(x => x.name.toLowerCase().includes(q));
    }
  }
}
</script>
<input v-model="query" placeholder="Search..."/>
<label>{{ `${matching.length} of ${items.length}` }}</label>
<div v-for="item in matching" :key="item.name">
  <label class="pill">{{ item.name }}</label>
</div>
```

### 10.4 条件渲染

```html
<script>
export default {
  data() { return { loading: true, error: null, data: null }; },
  computed: {
    hasError() { return this.error != null; },
    hasData()  { return this.data  != null; }
  }
}
</script>
<label v-if="loading">Loading...</label>
<label v-if="hasError">{{ `Error: ${error}` }}</label>
<div v-if="hasData">
  <label>{{ `Got ${data.length} results` }}</label>
</div>
```

### 10.5 computed 链式派生

```html
<script>
export default {
  data() { return { items: [{price:5},{price:3},{price:10}] }; },
  computed: {
    subtotal()  { return this.items.reduce((a,x) => a + x.price, 0); },
    tax()       { return this.subtotal * 0.08; },
    total()     { return this.subtotal + this.tax; },
    formatted() { return `$${this.total.toFixed(2)}`; }
  }
}
</script>
<label>{{ formatted }}</label>
```

### 10.6 带控制流的 handler

```html
<script>
export default {
  data() { return { count: 0, tags: [] }; },
  methods: {
    bump() {
      if (this.count < 10) {
        this.count++;
        // 数组要整替才反应：用 spread 或 slice + push
        this.tags = [...this.tags, `item-${this.count}`];
      } else {
        this.count = 0;
        this.tags = [];
      }
    }
  }
}
</script>
```

---

## 11. C API（宿主侧）

跑页面所需的最小代码：

```c
#include <ui_core.h>

int WinMain(...) {
    ui_init_with_theme(UI_THEME_LIGHT);
    UiPage page = ui_page_load_file(L"app.uix");
    if (!page) return 1;
    UiWindow win = ui_page_open_window(page, NULL);  // 按 <window> 标签开窗
    ui_run();
    ui_page_destroy(page);
    return 0;
}
```

从 C 向页面推送状态：

```c
ui_page_set_bool  (page, "loading", 0);
ui_page_set_int   (page, "count",   42);
ui_page_set_float (page, "ratio",   0.75);
ui_page_set_text  (page, "user.name", L"Alice");   // 点号路径 → 嵌套对象
ui_page_set_text_list(page, "tags", items, count);  // wchar_t* 数组

// 一坨数据用 JSON 一次推过去（推荐：业务结构复杂时不用逐字段 set）
ui_page_set_json(page, "extensions",
    "[{\"id\":1,\"name\":\"AdBlock\",\"on\":true},"
    " {\"id\":2,\"name\":\"DarkMode\",\"on\":false}]");

// 反向回读: 把当前 reactive 子树序列化成 JSON, 用完调 ui_page_free 释放
char* dump = ui_page_get_json(page, "extensions");
fputs(dump, stderr);
ui_page_free(dump);
```

**1.5.0 移除**：`ui_page_set_handler` / `ui_page_set_handler_ex`。

业务逻辑应该写在 `<script>` 的 `methods{}` 里。需要 C 端联动时用：
- `ui_page_set_*` 写状态 → 触发 `methods{}` 内部读取
- C 端轮询 `ui_page_get_json` 取关心的字段
- 直接绕开 page 系统，自己手挂 `widget->onClick`（`docs/c-api.md`）

也可以写一个 method 通知 C 端：

```js
// .uix
methods: {
  save() {
    // 标记一个 flag，C 端从 set_int 后轮询
    this.savedAt = Date.now();
  }
}
```

```c
// C 端
char* json = ui_page_get_json(page, "savedAt");
// parse + 触发自己的逻辑
ui_page_free(json);
```

热重载：

```c
ui_page_reload(page);   // 重新解析；按名字保留响应式变量值
```

### 资源解析器（图片、外部 CSS）

`.uix` 模板里 `<img src="logo.png">` / `<link rel="stylesheet" href="theme.css">` 这类
按"名字"引用的资源，库**不假设任何路径**，统一走 `ui_asset_*` 注册的解析器
chain。注册顺序就是匹配优先级（先注册先匹配）。

dev 工作流（边改边看）：

```c
ui_init();
ui_asset_register_dir("E:/myapp/assets");   // <img src="logo.png"> → assets/logo.png
UiPage p = ui_page_load_file(L"app.uix");
ui_run();
```

ship 工作流（CMake 烤进 exe，单文件分发）：

```cmake
# CMakeLists.txt
include(${UI_CORE_DIR}/cmake/UiCoreHelpers.cmake)
ui_core_embed_binary(my_app FILE assets/logo.png OUT logo_png.embed.h VAR k_logo_png)
ui_core_embed_text  (my_app FILE assets/theme.css OUT theme_css.embed.h VAR k_theme_css)
```

```c
// main.cpp
#include "logo_png.embed.h"
#include "theme_css.embed.h"

int main() {
    ui_init();
    ui_asset_register_blob("logo.png",  k_logo_png,  k_logo_png_size);
    ui_asset_register_blob("theme.css", k_theme_css, k_theme_css_size);
    UiPage p = ui_page_load_file(L"app.uix");   // .uix 完全不变
    ui_run();
}
```

两个工作流**用同一份 `.uix`**。dev 注册 dir，ship 注册 blob，模板里 `src` /
`href` 始终是同一个名字。

自定义解析（远程下载、ZIP 包等）：

```c
static int my_resolver(const char* name, const void** bytes, size_t* size, void* ud) {
    /* 找到就填 *bytes/*size 返回 1，找不到返回 0 */
}
ui_asset_register_resolver(my_resolver, my_userdata);
```

---

## 12. 已知限制

| 限制 | workaround |
|---|---|
| 没有 inline 文本流 —— `<label>`/`<span>`/`<p>` 里嵌套 inline 元素会编译出独立 widget 矩形（视觉叠加）。**自 build 10 起，编译期会报错并跳过嵌套元素**，看 `ui_page_last_error()` | 拆成 sibling `<label>` 放在 `<div style="flex-direction:row">` 里 |
| SVG `<g>` 分组 / `<filter>` / `<mask>` / `<clipPath>` / animate / 动态 `:class` 在 path 上 | 用 sibling shapes；颜色用 currentColor 或 CSS rule 命中 path |
| 没有横向滚动 | 只能用 `<ScrollView>` 竖向滚 |
| CSS `overflow: auto` 不生效 | 显式包 `<ScrollView>` |
| 没有 `@media` 查询 | 布局固定；响应式靠 `flex-grow` / `flex-shrink` |
| 没有 `@keyframes` / CSS 动画 | 只能用 `transition: prop duration easing` 在可动画属性上 |
| 没有浏览器 DOM / Web API | `.uix` 脚本运行在 QuickJS 中，没有 `window` / `document` / npm 包 |
| 模板表达式不写 `this.` 前缀 | `<script>` 的 methods/computed 内正常使用 `this`；模板里由编译器自动改写 |
| 没有 `<Component>` import 其它 `.uix` | 用 C++ 注册 CamelCase tag；或复制粘贴 |
| 没有 `position: sticky` | 固定 header 放在 `ScrollView` 外面 |
| 没有 CSS Grid / float | 用 flex（row/column）+ 嵌套容器 |
| `v-show` 只切换 visible | 要销毁/重建子树时用 `v-if` |
| 运行时切 frameless 会让 `<TitleBar>` 自动布局失效 | 用响应式变量 + `v-if` 手动控制 `<TitleBar>` 出现/消失 |
| v-for 和 v-if 在同一个元素上 | 分开：外层 v-if 包 v-for；或在 computed 里预过滤 |
| `<option value="x">` 的 value | attr 解析但不使用；v-model 返回的是**索引**数字 |
| 对象字面量 key 用反引号 | 不要这么做；用普通字符串或标识符 |

---

## 13. 反模式（不要）

### 嵌套 inline 元素

core-ui 没有 inline 文本流——每个 `<label>` / `<span>` / `<small>` / `<strong>` /
`<em>` / `<a>` 都编译成自己的 widget 矩形。把它们嵌进父 inline 元素，两个矩形落在
同一起点 → 视觉叠加。

**自 build 10 起，编译期检测到嵌套就跳过子元素并往 `ui_page_last_error()`
推一条带行号 + 改法的错误**。下面是写法对比：

```html
<!-- 错：嵌套 label，子 label 被跳过编译，编译报错 -->
<label class="title">主文本 <label class="meta">v1.0</label></label>

<!-- 错：段落里放 inline 元素，同样会被跳过 -->
<p>Hello <strong>world</strong>!</p>

<!-- 对：拆成 sibling label，包在 flex-row 里横排 -->
<div style="flex-direction:row; align-items:baseline; gap:6px">
  <label class="title">主文本</label>
  <label class="meta">v1.0</label>
</div>

<!-- 对：如果只是想要"主文本 灰色注脚"风格，列方向更自然 -->
<div style="flex-direction:column; gap:2px">
  <label class="title">主文本</label>
  <label class="meta">v1.0 · 2026-04</label>
</div>
```

### 为什么 core-ui 不做 inline 文本流

每个 widget 是独立的 D2D draw target + 独立 hit-test 矩形。inline flow 需要
DirectWrite `IDWriteTextLayout` 跨多个 style range 拼成一个 text run，并把
hover/click 反映射回 element——和 widget 模型耦合度太高，不在当前路线图里。
解法是显式 flex 容器，写法直观、CSS class 命中规则也清晰。

```html
<!-- 错：<script> 没有运行时 -->
<script>function foo() { ... }</script>

<!-- 对：<script>.methods 或 C handler -->
```

```html
<!-- 错：原地修改数组 -->
<script>
  methods: {
    addOne: tags.push("new")      <!-- 忘了赋值回去 -->
  }
</script>

<!-- 对：函数式 -->
<script>
  methods: {
    addOne: tags = tags.push("new")
  }
</script>
```

```html
<!-- 错：幻想 CSS 动画 -->
<style>
  @keyframes spin { from { ... } to { ... } }
  .icon { animation: spin 1s infinite; }
</style>

<!-- 对：transition 特定属性变化 -->
<style>
  .icon { transition: opacity 200ms ease-out; }
</style>
```

```html
<!-- 错：多根（必须单一根元素） -->
<script>...</script>
<h1>Title</h1>
<p>Body</p>

<!-- 对：单一根 -->
<script>...</script>
<div>
  <h1>Title</h1>
  <p>Body</p>
</div>
```

```html
<!-- 错：不显式包裹就想滚 -->
<div class="long-content">...</div>   <!-- 内容超出窗口 → 被裁 -->

<!-- 对：包 ScrollView -->
<ScrollView>
  <div class="content">...</div>
</ScrollView>
```

```html
<!-- 错：v-for 和 v-if 在同一元素 -->
<div v-for="x in list" v-if="x.visible">...</div>

<!-- 对：在 computed 里过滤 -->
<script>
  computed: { visible: list.filter(x => x.visible) }
</script>
<div v-for="x in visible">...</div>
```

```html
<!-- 错：在 <script>.data 里用点号路径 key（只会被当成平面字符串 key）-->
<script>
  data: {
    "user.name": "Alice"    <!-- 错 -->
  }
</script>

<!-- 对：嵌套对象 -->
<script>
  data: {
    user: { name: "Alice" }
  }
</script>

<!-- 注意：ui_page_set_text(page, "user.name", L"Bob") 从 C 端是可以的
     （点号路径自动创建嵌套）。这种不对称是有意的：state 块声明式，
     C API 命令式。 -->
```

### 资源系统（自 build 19）

```html
<!-- 错：模板里写绝对路径，假设宿主"自然能找到" -->
<img src="C:\\Users\\me\\proj\\assets\\logo.png"/>

<!-- 对：写资源名字，宿主用 ui_asset_register_dir/blob 决定怎么解析 -->
<img src="logo.png"/>
```

```html
<!-- 错：把 PNG / JPG 包到 ui_core_embed_text() —— 文本路径会在二进制里
     遇到 0x00 时把数组结束符插早 -->
ui_core_embed_text(my_app FILE assets/logo.png ...)

<!-- 对：用 ui_core_embed_binary()，无终结符 unsigned char[] -->
ui_core_embed_binary(my_app FILE assets/logo.png ...)
```

```html
<!-- 错：忘了注册任何 resolver 就期望 <img> / <link> 工作 -->
ui_init();
UiPage p = ui_page_load_file(L"app.uix");  // <img> 全部不显示

<!-- 对：load 之前注册一个目录或 blob -->
ui_init();
ui_asset_register_dir("assets");
UiPage p = ui_page_load_file(L"app.uix");
```

```html
<!-- 错：<link href> 用错文件类型 —— 库不验证 MIME，把任何字节当 CSS 喂给
     parser，PNG 字节会触发 CSS parse error -->
<link rel="stylesheet" href="logo.png"/>

<!-- 对：href 就是 .css 文件 -->
<link rel="stylesheet" href="theme.css"/>
```

---

## 17. `<menu>` 完整参考

声明式上下文菜单 / 下拉菜单。一条标签搞定 trigger 挂载、icon、submenu、
键盘快捷键提示、点击派发到 `<script>.methods`，C 端零代码。

> 渲染管线（自 1.5.0）：popup 走 DirectComposition + 透明 swap chain —
> 圆角是真 D2D 抗锯齿（不是 SetWindowRgn 的 1-bit mask），阴影是 D2D
> 多层叠加合成，跟主题自动 light/dark 切换。

### 完整示例

```html
<script>
export default {
  data() { return { last: "(none)" }; },
  methods: {
    onSave() { this.last = "Saved"; },
    onOpen() { this.last = "Opened"; },
    onQuit() { this.last = "Quit"; }
  }
}
</script>

<div class="root">
  <!-- File 按钮下拉菜单 -->
  <menu trigger="#fileBtn" event="click">
    <menuitem id="1" onclick="onSave" shortcut="Ctrl+S">
      <svg viewBox="0 0 24 24"><path fill="currentColor" d="..."/></svg>
      Save
    </menuitem>
    <menuitem id="2" onclick="onOpen" shortcut="Ctrl+O">Open</menuitem>
    <separator/>
    <menu text="Recent">                                <!-- 嵌套 = submenu -->
      <menuitem id="10" onclick="onOpen">file1.txt</menuitem>
      <menuitem id="11" onclick="onOpen">file2.txt</menuitem>
    </menu>
    <separator/>
    <menuitem id="3" onclick="onQuit" shortcut="Alt+F4" style="color: #d63a26">
      Quit
    </menuitem>
  </menu>

  <!-- 区域右键菜单（icon 走 asset resolver） -->
  <menu trigger="#panel" event="rclick">
    <menuitem id="11" icon="cut.png">Cut</menuitem>     <!-- ui_asset_register_* -->
    <menuitem id="12" icon="copy.png">Copy</menuitem>
  </menu>

  <button id="fileBtn">File ▾</button>
  <div id="panel">Right-click here</div>
  <label>{{ last }}</label>
</div>
```

### 反应式菜单（自 1.6.0 build 73-89）

菜单内容可以跟 `data()` 状态联动——`v-for` 生成 menuitem 列表、`v-if` 条件显示、
`:disabled` 双向绑定。右键弹出时永远读最新值，不需要手动 "先关菜单 → 改 menu → 再弹"：

```vue
<script>
export default {
  data() {
    return {
      commands: [
        { id: 'copy',  label: '复制', enabled: true  },
        { id: 'paste', label: '粘贴', enabled: false }
      ],
      isAdmin: true
    };
  },
  methods: {
    run(cmd) { /* ... */ },
    del()    { /* ... */ }
  }
}
</script>

<template>
  <div id="tree-item">tree</div>

  <menu trigger="#tree-item" event="rclick">
    <menuitem v-for="cmd in commands" :key="cmd.id"
              :disabled="!cmd.enabled" @click="run(cmd)">
      <label>{{ cmd.label }}</label>
    </menuitem>
    <separator/>
    <menuitem v-if="isAdmin" @click="del">
      <label style="color: #d63a26">删除</label>
    </menuitem>
  </menu>
</template>
```

**重要的破坏性变动 (从 1.5.0 公开版本升级时注意):**

- **`menuitem` 改成 widget 模板** (BREAKING, build 75)：必须包子 widget，不能
  `<menuitem>文本</menuitem>` 直接收文本节点：

  ```vue
  <!-- 1.5.0 老写法 (已不支持) -->
  <menuitem>复制</menuitem>

  <!-- 1.6.0 新写法 -->
  <menuitem><label>复制</label></menuitem>
  ```

- **`WireMenus` 静态简化路径已删** (BREAKING, build 73)：C 端不能再手挂菜单。
  全部走 `<menu>` 声明式 + Vue 3 binding，包括动态修改 menuitem 列表也用 `v-for`，
  不要走 `ui_menu_clear_items` + 手动重灌的旧路径
- **rclick dispatch 改 deepest-match** (build 107-108)：嵌套 `<menu trigger>` 时
  子 widget 的 menu 不再被祖先抢走 (跟 DOM event bubbling 语义一致)

```vue
<div id="canvas_body">
  <div id="minimap">小地图</div>
</div>

<menu trigger="#canvas_body" event="rclick">…</menu>   <!-- 父 trigger -->
<menu trigger="#minimap"     event="rclick">…</menu>   <!-- 子 trigger 现在能弹出 -->
```

之前父 `Contains(x, y)` 对子区域也返 true 抢先 ShowMenu，子 trigger 没机会；
现在按 widget tree 深度选最深的 trigger。

### 属性参考

#### `<menu>`

| 属性 | 说明 |
|---|---|
| `id` | 菜单名，C API `ui_page_menu(page, id) → UiMenu` 用得到 |
| `trigger="#elemId"` | 自动挂到该元素的 click / rclick |
| `event="click" \| "rclick"` | trigger 事件类型，默认 `click` |
| `text="..."` | 仅嵌套 `<menu>` 当 submenu 时用，作为父项的显示文字 |
| `row-class="..."` | 追加到自动生成的 `.menuitem-row` wrapper；子菜单默认继承，用于只调整某棵菜单树的行宽/样式 |
| `share-width="false"` | 关闭默认的父子菜单同宽传播，让子菜单按自身内容宽度收缩 |

`<menu>` 在 `<template>` 单根元素约束下必须放在根 `<div>` 里。

#### `<menuitem>`

| 属性 | 说明 |
|---|---|
| `id="N"` | 数字 id（必须能 `stoi`），C 端 `ui_window_on_menu` 回调按 id 派发；不写自动从 1 自增 |
| `shortcut="Ctrl+S"` | 右侧灰色快捷键文本（**只显示，不绑实际快捷键**，要绑请走 `ui_window_on_key`）|
| `onclick="methodName"` | 触发时调 `<script>.methods[methodName]`（C 端 handler 在 1.5.0 已删，全走 `methods{}`）|
| `icon="logo.png"` | 走资源解析器（同 `<img src>`）；PNG/JPG 不变色 |
| `style="color: #d63a26"` | per-item 文字色覆盖；SVG icon 跟着变色（currentColor），PNG 不跟 |

`<menuitem>` 子节点：

- 第一个 `<svg>...</svg>` 当 SVG icon（推荐 `fill="currentColor"` 让 icon 跟文字色变）
- `<img src="...">` 当光栅 icon（覆盖 `icon=` 属性）
- 文本节点是显示文字

#### `<separator/>`

横线分隔，跟 `<hr/>` 同义。

#### 嵌套 `<menu text="...">`

直接当 submenu。菜单悬停展开。submenu 里所有规则（icon / shortcut / onclick）同顶层。

### C API（宿主侧）

```c
// 拿菜单 handle 手动 show（无 trigger 时用）
UiMenu m = ui_page_menu(page, "ctx");
ui_menu_show(win, m, x, y);

// 收 menu 点击（onclick 派发完之后还会调这个，可以 chain 处理 id）
ui_window_on_menu(win, on_item, NULL);
```

### 自动化测试

```c
// 截 popup 内容到 PNG（D2D RT 直接 readback，不依赖桌面 / 前台）
ui_debug_screenshot_menu(win, L"popup.png");
```

Pipe 命令同名：`screenshot_menu <path>`。配合 `click <id>` / `rclick_at x y` /
`menu_click_id <n>` / `menu_click_path 0/1/2` 做端到端验证（参见
[debug-simulation.md](debug-simulation.md)）。

### 注意事项

- `trigger` 元素必须有 `id` 能被 `ui_widget_find_by_id` 找到。如果元素在
  `v-if` 块里，框架会在 mount 之后自动接 trigger（自 build 25）。
- `<script>.methods.onMenuSave` 这类方法名在 menu 编译期就解析；mount 后
  改动不会重新绑（跟 `@click="..."` 一致）。
- icon 渲染走 D2D，跟 popup 共用工厂。**不要在 paint 之外手动调
  `Renderer::ParseSvgIcon`**——库内部已经在 `WireSubtreeMenus` 里走的是
  Window renderer，跟 paint 状态解耦。
- `style="color: ..."` 只看 `color` 一个属性；其他 CSS（background / font-size
  等）目前不影响 menu，因为 menu 走自己的 popup 主题。

### 默认外观（自 1.5.0）

| 维度 | Light | Dark | 备注 |
|---|---|---|---|
| 卡片背景 | `#FFFFFF` | `#2C2C2C` | `ui_menu_set_bg_color` 可以单菜单覆盖，alpha 控制半透明 tint |
| Backdrop blur | 自动半径 | 自动半径 | `ui_menu_set_backdrop_blur(menu, radius)` 控制；`<0` 默认，`0` 关闭，`>0` 指定 DIP 半径 |
| 文字色 | `theme.btnText`（#141414）| 白色 | per-item `style="color"` 优先 |
| 快捷键文本 | `theme.foreground3` | 同 | 12 px |
| 分隔线 | `theme.dividerSubtle` | 同 | 跨容器宽度 |
| Hover bg | 黑 6% | 白 10% | 圆角 6 px，行内 inset |
| 行高 | 30 px | 同 | `kItemHeight` |
| 字体 | 13 px | 同 | `kFontSize`，文字抗锯齿强制 GRAYSCALE |
| 图标尺寸 | 16 px | 同 | 距左边 12 px 固定 inset |
| 圆角 | 10 px | 同 | D2D 抗锯齿 |
| 卡片阴影 | 软投影 | 同 | 18 px halo，alpha 衰减 |
| 默认最小宽 | 180 px | 同 | `kMinWidth`；窄文本菜单不会过窄 |

`ui_theme_set_mode(UI_THEME_DARK / UI_THEME_LIGHT)` 切换主题后菜单自动重绘
（popup 内部读 `theme::CurrentMode()` 决定卡片色 + hover 色，文字 / 分隔线 /
快捷键走 token 自动跟）。

### C API（仅运行时构造）

```c
UiMenu m = ui_menu_create();
ui_menu_add_item(m, 1, L"复制");
ui_menu_add_item_ex(m, 2, L"剪切", L"Ctrl+X", "<svg viewBox='...'><path .../></svg>");
ui_menu_add_separator(m);
ui_menu_add_submenu(m, L"最近", subMenu);
ui_menu_set_enabled(m, 1, 0);             // 禁用某项
ui_menu_set_bg_color(m, (UiColor){.r=...});  // 强制覆盖卡片背景
ui_menu_set_backdrop_blur(m, 18.0f);      // 背景磨砂强度；0 关闭，负数恢复默认
ui_menu_show(win, m, x, y);               // 弹出（DIP 坐标，相对窗口 client）
ui_window_on_menu(win, on_click, ud);     // 全窗口共用回调，按 id 派发
ui_menu_destroy(m);                        // 不再使用时
```

`.uix` 里 `<menu id="myCtx">` 编译出来的菜单可以从 C 端拿 handle：

```c
UiMenu m = ui_page_menu(page, "myCtx");   // page-owned, 不要 destroy
ui_menu_show(win, m, x, y);
```

---

## 18. 主题 CSS 变量（自 1.5.0）

库提供一组语义级 CSS 变量，跟 `theme::Current()` 同步。`.uix` 直接写
`background: var(--bg)` / `color: var(--fg)` 即可，调
`ui_theme_set_mode(UI_THEME_DARK / UI_THEME_LIGHT)` 全部页面自动重 cascade。
不需要自己写 `.dark` 后代选择器。

| 类别 | 变量 |
|---|---|
| 表面 | `--window-bg` `--window-border` `--bg` `--bg-2` `--bg-3` `--bg-4` |
| 文字 | `--fg` `--fg-2` `--fg-3` `--fg-4` `--fg-on-accent` |
| 边框 | `--border` `--border-subtle` |
| 侧栏 | `--sidebar-bg` `--sidebar-text` `--sidebar-hover` |
| 输入 | `--input-bg` `--input-border` `--input-border-hover` `--input-border-focus` |
| 卡片 | `--card-bg` `--card-border` |
| 按钮 | `--btn-bg` `--btn-hover` `--btn-press` `--btn-text` |
| 品牌 | `--accent` `--accent-hover` `--accent-press` `--accent-text` `--accent-selected` |
| 禁用 | `--disabled-bg` `--disabled-text` |

`:root { --x: ... }` 用户显式定义优先于库默认。半透明 token 序列化为 8-char
hex `#rrggbbaa` 保留 alpha 通道。

例：

```css
.shell    { background: var(--bg); }
.sidebar  { background: var(--sidebar-bg); }
.h1       { color: var(--fg); }
.card     { background: var(--card-bg); }
.nav-icon { color: var(--fg-2); }    /* svg currentColor 跟着变 */
```

`<svg fill="currentColor">` 且父链没显式 `color` 时 fallback 取
`theme.foreground1`，自动跟主题。

---

## 19. 生命周期 hooks（C API，自 1.5.0）

针对 v-if / v-for 子树里的 widget 用 `ui_widget_find_by_id` + `ui_widget_on_click`
注册 C 回调时，跨 mount 重建会丢失。两条路径解决：

### A. id 持久化注册表（懒人路径）

`ui_widget_on_click` / `ui_checkbox_on_changed` / `ui_slider_on_changed` /
`ui_toggle_on_changed` 设置 callback 时**自动**按 widget 的 HTML id 持久化。
v-if/v-for 重 mount 时引擎按 id 把回调回填到新实例。**前提**：widget 必须有
`id`。无 id 的 widget 还是绑实例（一次性，跨 mount 失效）。

注意：同一 id 在不同 page 出现 → 共享同一份 handler。

### B. Lifecycle hooks（Vue parity 路径）

```c
typedef void (*UiWidgetLifecycleCallback)(UiPage page, UiWidget w, void* userdata);
UI_API void ui_page_on_widget_mount(UiPage p, const char* widget_id,
                                     UiWidgetLifecycleCallback cb, void* userdata);
UI_API void ui_page_on_widget_unmount(UiPage p, const char* widget_id,
                                       UiWidgetLifecycleCallback cb, void* userdata);
```

调用时机：

- 初始 page mount → 对 root 树所有有 id 的 widget 触发 `onMount`
- v-if 表达式 false → true → 子树 mount，触发 `onMount`
- v-if 表达式 true → false → 子树 unmount，触发 `onUnmount`
- v-for iteration build / destroy

callback 传入的 `UiWidget` 是**新**实例 handle —— 不要缓存到下次 unmount 之后。

```c
void on_mount(UiPage p, UiWidget w, void* ud) {
    /* 拿到的 w 是新实例。在这里 wire callbacks。 */
    ui_widget_on_click(w, on_click_cb, ud);
}
ui_page_on_widget_mount(page, "btn_x", on_mount, NULL);
```

每次 v-if remount，`on_mount` 都会重新触发，跟 Vue 3 `watch(ref, ...)` 等价。

**何时用哪条**：

| 场景 | 选 |
|---|---|
| 一次性写好 → 跨 mount 自动延续 | A |
| 严格 Vue 心智，需要在 mount/unmount 时跑额外逻辑（追踪状态、external resource lifecycle） | B |
| 模板里能用 JS methods 表达 | 都不用 — 直接 `@click="method"` |

A + B 可同时存在不冲突。

---

## 14. 最小可调试示例

页面不渲染时，先用这个最简版测试：

```vue
<window title="Test" width="300" height="200"/>
<template>
  <div><h1>OK</h1></div>
</template>
```

能跑就逐步加 `<script>`、`<style>`、指令。

---

## 15. 错误报告

- 模板解析错误：`ui_page_last_error(page)` 返回 `"line N:C: message"`（SFC 块切分错误 + `<template>` 内 HTML 子集解析错误共用同一格式）。
- 未知标签：`"unknown tag <Foo> — did you mean <Bar>?"`（带 edit-distance 建议）。
- 表达式解析错误：编译期报告，带 attr 或文本字面量内的行/列号。
- 运行时错误（未定义标识符、类型不匹配）**静默**——绑定跳过，UI 显示上一次已知值
  （或首次求值失败时的 null）。

生成 `.uix` 后先用宿主的 `ui_page_load_file` + `ui_page_last_error` 跑一遍验证，
不要假设无错。

---

## 16. 生成前 checklist

生成 `.uix` 页面时，检查：

1. ☐ 模板内容用 `<template>...</template>` 包裹（强制）
2. ☐ `<template>` 内**恰好一个**根元素（多个兄弟用 `<div>` 包起来）
3. ☐ 所有标签闭合（void 用 `<br/>`，容器用 `<div></div>`）
4. ☐ 如果 `frameless="true"`，`<template>` 内有 `<TitleBar/>`
5. ☐ 如果内容可能超出窗口，用 `<ScrollView>` 包 + CSS `flex: 1`
5. ☐ `v-for` 带稳定 `:key`
6. ☐ 数组修改整体替换：`x = [...x, y]`，不是 `x.push(y)`
7. ☐ handler body 是语句或 `{}` 块；不是裸表达式
8. ☐ computed 之间无循环依赖
9. ☐ 模板表达式里不写 `this.` 前缀；复杂逻辑放到 `methods` / `computed`
10. ☐ 没有 CSS grid / float / keyframes / media queries
11. ☐ `<select v-model="x">` 的 `x` 是**数字**（索引）
12. ☐ 没有在 `<p>` 里混合 inline 元素（会堆叠）

每条都要过。过不了就改。
