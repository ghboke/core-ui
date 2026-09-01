# Core UI `.uix` — AI Agent Guide

> **这一份文件够 LLM 写出可跑的 `.uix` 单文件组件。**
> 不需要扫整个仓库 —— 把这一页喂给模型 + 速查 widget 列表即可。

---

## 1. 30 秒速览

- **文件后缀**：`.uix` (Vue 3 风格 Single File Component)
- **四块**：`<window>` / `<script>` / `<style>` / `<template>` —— 顺序无关，`<template>` 必须有
- **脚本**：Vue 3 Options API (`export default { data, computed, methods }`)，QuickJS-NG 求值
- **响应式**：Proxy + WatchEffect，`this.X` 自动收集依赖
- **加载**：C 端 `ui_page_load_file(L"path/foo.uix")` → `ui_page_open_window(page, NULL)`
- **i18n**：模板里 `{{ $t('key') }}`，`ui_page_load_language_string(p, "en", "...")` 注入

---

## 2. 最小骨架

```vue
<window title="Hello" width="380" height="200" centered="true" theme="light"/>

<script>
export default {
  data() { return { count: 0 }; },
  methods: {
    inc() { this.count++; }
  }
}
</script>

<style>
  .root { padding: 18px; gap: 10px; background: #f5f5f7; }
  .h1   { font-size: 22px; color: #1f2937; font-weight: 600; }
  button { background: #2563eb; color: #fff; padding: 6px 14px;
           border-radius: 4px; cursor: pointer; }
</style>

<template>
  <div class="root">
    <label class="h1">{{ count }}</label>
    <button @click="inc">+1</button>
  </div>
</template>
```

C 端三行即跑：

```cpp
ui_init_with_theme(UI_THEME_LIGHT);
UiPage page = ui_page_load_file(L"foo.uix");
UiWindow win = ui_page_open_window(page, NULL);
ui_run();
```

---

## 3. `<window>` 属性

```html
<window title="..." width="500" height="300"
        min-width="320" min-height="200"
        resizable="true" frameless="false" centered="true"
        theme="light"/>      <!-- light | dark -->
```

任意属性都可省略，未给即默认（`width=800 height=600 resizable=true centered=false theme=light`）。

---

## 4. `<script>` 必须是 Vue 3 SFC 形态

**只支持** `export default { ... }` 默认导出，任何旧式 `data: { ... }` shorthand
**不工作**（QuickJS module-eval 解析）。

```js
export default {
  // 1) 反应式数据初始化器
  data() {
    return {
      count: 0,
      name: "Alice",
      items: [{ id: 1, label: "α" }]
    };
  },

  // 2) 计算属性 — lazy memo, 链式依赖也支持
  computed: {
    doubled() { return this.count * 2; },
    greeting() { return "Hi, " + this.name; }
  },

  // 3) 方法 — Vue 3 method shorthand
  methods: {
    inc()    { this.count++; },
    reset()  { this.count = 0; },
    addN(n)  { this.count += n; },
    rename() { this.name = "Bob"; }
  }
}
```

**约束**：
- 没有 `<script setup>` / 组合式 API（`ref` / `reactive` / `watch`）
- 没有 lifecycle hooks（`mounted` / `updated` / `unmounted`）
- 没有 props / emit / `<slot>`（component 系统已删）
- `data()` 必须返回对象；顶层赋值会触发更新，嵌套对象 / 数组原地 mutation 不会触发父属性 watcher，列表更新建议整体替换：
  ```js
  this.items.push(x);          // ❌ 不触发
  this.items = [...this.items, x];   // ✅ 触发
  ```

---

## 5. `<template>` 模板语法

### 5.1 文本插值

```html
<label>{{ count }}</label>
<label>Hello, {{ name }}! count = {{ count }}</label>
<label>{{ count > 10 ? "big" : "small" }}</label>
```

`{{ ... }}` 里是 JS 表达式（不是语句）。`this.X` 自动加（**不要**手写 `this.`）。

### 5.2 属性绑定 `:attr="expr"`

```html
<div :class="active ? 'on' : 'off'"></div>
<button :enabled="canSubmit"></button>
<img :src="avatarUrl"/>
```

`:` 前缀表示动态求值。无 `:` 是字面量。

支持的目标属性（`ApplyBindingToWidget` 映射）：
`text` / `class` / `visible` / `opacity` / `bg-color` / `color` / `enabled` /
`width` / `height` / `selected` / `checked` / `on` / `value`

### 5.3 事件 `@event="..."`

```html
<button @click="inc">+1</button>
<button @click="addN(5)">+5</button>
<button @click="onDelete(item)">×</button>
<input  @change="updateName"/>
<div    @mousemove="trackXY"/>
```

事件值是 JS 表达式。Vue 3 语义：
- 裸标识符 (`@click="inc"`) → auto-call `inc.call(this, $event)`
- 调用形式 (`@click="addN(5)"`) → 直接 eval
- 赋值 (`@click="count = count + 1"`) → 直接 eval

`$event`：
| 事件 | $event |
|------|--------|
| `click` / `focus` / `blur` / `submit` / `mouseleave` | `undefined` |
| `mousedown` / `mousemove` / `mouseup` / `wheel` / `dblclick` / `mouseenter` / `contextmenu` | DOM 风格 `{offsetX, offsetY, clientX, clientY, button, buttons, deltaX, deltaY, ctrlKey, shiftKey, altKey}`（兼容旧 `x/y/delta`；`deltaY` 正值=向下，web 约定） |
| `change` / `input` | scalar (bool / string / float) |
| `dragstart` | 鼠标字段 + `data`（载荷，可改写 = setData） |
| `dragenter` / `dragover` | 鼠标字段 |
| `dragleave` | `undefined` |
| `drop` | 鼠标字段 + `data` |
| `dragend` | bool（是否成功投放） |

拖放属性：`draggable="true"`、`drag-data="payload"`（隐含 draggable）、
`drop-target="true"`（绑 `@drop` 亦隐含）。拖动悬停目标进入 `:drag-over` 伪类。

`<chips-input>`：单行模板编辑器（原子变量 chip + 自由文本）。属性
`placeholder`、`chip-labels="key=显示名;..."`；`:value`/`v-model`/`@change`
的值是模板串（chip 记作 `{key}`）。天然接受拖放（载荷=chip key），不要在其上
另绑 `@drop`。

### 5.4 控制流

```html
<!-- v-if: mount/unmount (不是 v-show) -->
<div v-if="loading" class="spinner"/>
<div v-if="error">err: {{ error }}</div>

<!-- v-show: 切 visible 属性 -->
<div v-show="visible">…</div>

<!-- v-for: 必须配 :key 才能 keyed-reuse widget -->
<div v-for="(item, i) in items" :key="item.id" class="row">
  <label>{{ i }}: {{ item.name }}</label>
  <button @click="remove(item)">×</button>
</div>

<!-- v-model: 双向绑定 (input/textarea/checkbox/toggle/slider) -->
<input v-model="name"/>
<input type="checkbox" v-model="agreed"/>
<input type="range" min="0" max="100" v-model="vol"/>
<toggle v-model="dark"/>
```

`v-for` 不带 `:key` 时退化为按位置复用；列表频繁重排时**强烈建议**给稳定 id。

### 5.5 i18n

```html
<label>{{ $t('greeting.hello') }}</label>
<label>locale = {{ $locale }}</label>
<label>@app.title</label>   <!-- 简写：等价于 {{ $t('app.title') }} -->
```

`$t()` 内部读 `$locale`，所以切换 locale 会自动重 fire 所有用了 `$t` 的 binding。

C 端注入：

```cpp
ui_page_load_language_string(page, "en",
    "greeting.hello=Hello\napp.title=My App\n");
ui_page_load_language_string(page, "zh",
    "greeting.hello=你好\napp.title=我的应用\n");
ui_page_set_locale(page, "zh");   // 整页 label 自动刷新
```

---

## 6. `<style>`

CSS 子集 + 选择器 + cascade。常用属性：

| 类别 | 属性 |
|------|------|
| 盒子 | `padding` / `margin` / `width` / `height` / `min-*` / `max-*` |
| flex | `flex-direction` / `gap` / `align-items` / `justify-content` / `flex` |
| 颜色 | `background` / `background-color` / `color` |
| 边框 | `border-radius` / `border` (还不全) |
| 阴影 | `box-shadow` (含 inset) |
| 背景模糊 | `backdrop-filter: blur(Npx)` / `backdrop-blur: Npx` |
| 渐变 | `background: linear-gradient(...)` / `radial-gradient(...)` |
| 文本 | `font-size` / `font-weight` / `text-align` |
| 变换 | `transform: rotate() scale() translate()` |
| 透明 | `opacity` |
| 过渡 | `transition: <prop> <duration>ms <easing>` |
| 鼠标 | `cursor: pointer` |

**选择器**：tag / `.class` / `#id` / 后代 ` ` / 子代 `>` / 状态 `:hover` `:active` `:focus` /
属性 `[type=...]` / 类组合 `.tab.active`。

**伪元素 / pseudo-class** 已支持的有限（`:hover` / `:active` / `:focus` / `:disabled`）。
**没有**：`:not()` / `:has()` / `::before` / `::after` / media query / container query。

不支持 grid，flex 也不完整 spec（无 `wrap` / `reverse`）。

---

## 7. 内置 widget 标签

模板里直接用这些标签（小写）：

| 标签 | 作用 |
|------|------|
| `div` / `section` / `nav` | flex 容器 (默认 column) |
| `Stack` | 只显示一个子页面的容器；`active` 指定当前索引 |
| `SplitView` | 侧栏 + 内容双槽布局；前两个元素子节点分别作为侧栏和内容 |
| `label` / `span` / `p` / `h1`-`h6` | 文本（不可编辑） |
| `button` | 按钮 |
| `NavItem` | 侧栏导航项；支持 `text` / `svg` / `glyph` / `selected` |
| `input` | 文本输入；`type="password"` / `checkbox` / `radio` / `range` / `number` 切换形态 |
| `textarea` | 多行文本 |
| `toggle` | 开关 |
| `select` | 下拉，映射到 `ComboBoxWidget` |
| `progressbar` | 进度条 |
| `expander` | 折叠面板 |
| `ScrollView` | 竖向滚动容器 |
| `TitleBar` | 无边框窗口标题栏 |
| `img` | 位图 / SVG 资源 |
| `svg` | SVG 子树 |
| `custom` | 自绘 widget |
| `gh-img-view` | 通用瓦块画布（数据由宿主经 C API 喂） |
| `ocr-img-view` | 图片划词画布（同上；`highlight-color` 可静态指定） |
| `tabs` | 标签页容器 |
| `menu` / `menuitem` / `separator` | 上下文菜单 |

每个 widget 详细属性见 `docs/controls.md`。

---

## 8. 常见模板（cookbook）

### 表单 + 校验

```vue
<script>
export default {
  data() { return { name: "", agreed: false }; },
  computed: {
    canSubmit() { return this.name.length > 0 && this.agreed; }
  },
  methods: {
    submit() { console.log("save", this.name); }
  }
}
</script>

<template>
  <div class="form">
    <input v-model="name" placeholder="name"/>
    <input type="checkbox" v-model="agreed"/> Agree
    <button :enabled="canSubmit" @click="submit">Save</button>
  </div>
</template>
```

### 列表 + 增删

```vue
<script>
export default {
  data() {
    return { items: [{id:1,label:"α"},{id:2,label:"β"}] };
  },
  methods: {
    add()        {
      var next = this.items.slice();
      next.push({ id: Date.now(), label: "new" });
      this.items = next;
    },
    remove(item) {
      this.items = this.items.filter(function(x){ return x.id !== item.id; });
    }
  }
}
</script>

<template>
  <div class="list">
    <div v-for="(item, i) in items" :key="item.id" class="row">
      <label>{{ i }}: {{ item.label }}</label>
      <button @click="remove(item)">×</button>
    </div>
    <button @click="add">add</button>
  </div>
</template>
```

### 标签栏 (tabs)

```vue
<script>
export default {
  data() { return { active: "a" }; },
  methods: { go(t) { this.active = t; } }
}
</script>

<template>
  <div>
    <div class="tabs">
      <button :class="active=='a' ? 'tab on' : 'tab'" @click="go('a')">A</button>
      <button :class="active=='b' ? 'tab on' : 'tab'" @click="go('b')">B</button>
    </div>
    <div v-if="active=='a'" class="panel">Panel A</div>
    <div v-if="active=='b'" class="panel">Panel B</div>
  </div>
</template>
```

---

## 9. C 端互通速查

```cpp
#include <ui_core.h>

UiPage page = ui_page_load_file(L"foo.uix");
// 或：UiPage page = ui_page_load_string(uix_text);

// 写状态（触发响应式更新）
ui_page_set_int   (page, "count",  5);
ui_page_set_bool  (page, "agreed", 1);
ui_page_set_float (page, "vol",    0.7);
ui_page_set_text  (page, "name",   L"Alice");
ui_page_set_text_list(page, "items", items_w, n);
ui_page_set_json  (page, "user",   "{\"id\":1,\"name\":\"A\"}");

// 读状态
char* s = ui_page_get_json(page, "user");
ui_page_free(s);

// 国际化
ui_page_load_language_string(page, "zh", "key=值\n...");
ui_page_set_locale(page, "zh");

// 错误检查
const char* err = ui_page_last_error(page);

// 打开窗口
UiWindow win = ui_page_open_window(page, NULL);
ui_run();
ui_page_destroy(page);
```

> **注意**：原 `ui_page_set_handler` / `ui_page_set_handler_ex` 已在 1.5.0 移除。
> C 端注册 native callback 需要在 Vue 3 `methods{}` 里声明 + 通过 `set_*` 写状态触发，
> 或者直接绕开 page 系统手挂 widget 的 `onClick`（参考 `docs/c-api.md`）。

---

## 10. 不要做的事

- ❌ `data: { ... }` shorthand —— 必须 `data() { return { ... }; }`
- ❌ `methods: { foo: stmt }` —— 必须 `methods: { foo() { stmt } }`
- ❌ `# comment` —— 用 JS 的 `//` 或 `/* */`
- ❌ `<import src="..." as="X"/>` —— component 系统已删
- ❌ `<script setup>` / `ref()` / `watch()` —— 无组合式
- ❌ `this.items.push(x)` —— 数组原地 mutation 不触发依赖 `items` 的 binding，请整体替换
- ❌ Vue / npm 生态库（不接 webview，不能 `import` 任何 npm 包）

---

## 11. AI 生成 `.uix` 的 prompt 建议

> 你正在为 Core UI 写 `.uix` 单文件组件。规则：
>
> 1. 一个文件 = `<window>` + `<script>` + `<style>` + `<template>` 四块
> 2. `<script>` 用 Vue 3 Options API：`export default { data() { return {...} }, computed: {...}, methods: {...} }`
> 3. 模板表达式不写 `this.` 前缀（编译期自动加）
> 4. 列表用 `v-for="(item, i) in items" :key="item.id"`
> 5. 列表更新用整体替换数组，例如 `items = [...items, x]`
> 6. CSS 用 flex（不用 grid），常用属性见 §6
> 7. 不要写 `<import>` / `<script setup>` / `ref()` / `watch()`
>
> 输出格式：单个 `.uix` 文件，前后用三引号包起来。

---

## 12. 真实 demo 索引

仓库里 6 个端到端可跑的 `.uix` demo，按场景找：

| Demo | 演示点 |
|------|--------|
| `demo/quickjs_smoke.uix` | 文本插值 + 反应式最小例 |
| `demo/quickjs_methods.uix` | `methods{}` + `@click` 全式 |
| `demo/quickjs_computed.uix` | `computed{}` 链式依赖 |
| `demo/quickjs_vif.uix` | `v-if` + `v-model` |
| `demo/quickjs_vfor.uix` | `v-for` + 列表增删 |
| `demo/quickjs_keyed.uix` | `:key` keyed reuse 重排 |
| `demo/quickjs_i18n.uix` | `$t` / `$locale` 切换 |
| `demo/quickjs_set_array.uix` | C 端 `set_text_list` 触发 |
