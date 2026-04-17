# Changelog

版本号规则：`MAJOR.MINOR.PATCH.BUILD`

- `MAJOR/MINOR/PATCH`：语义化版本，对应 `CMakeLists.txt` 的 `UI_CORE_VERSION`
- `BUILD`：构建编号，对应 `CMakeLists.txt` 的 `UI_CORE_INTERNAL_VERSION`
  - 每次发布构建 +1；同一 MAJOR.MINOR.PATCH 下可累加
- DLL 属性里看到的 `FileVersion` 即该四段串，由 `src/version.rc.in` 注入

## 1.0.0 — build 1

- 首个带版本号记录的版本
- `ui-core.dll` / `ui-demo.exe` 嵌入 Windows VERSIONINFO 资源
- 新增 `ui_core_version()` / `ui_core_version_build()` / `ui_core_version_string()` 运行时 API
- 头文件暴露 `UI_CORE_VERSION_MAJOR/MINOR/PATCH/BUILD` 宏与 `UI_CORE_VERSION_STRING`
