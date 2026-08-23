cmake_minimum_required(VERSION 3.20)

set(UI_CORE_VERSION "1.7.0")
set(UI_CORE_INTERNAL_VERSION "253")
project(core-ui-project VERSION ${UI_CORE_VERSION} LANGUAGES C CXX)

set(UI_CORE_VERSION_TAG "v${PROJECT_VERSION}")
set(UI_CORE_RELEASE_DIR_NAME "core-ui-${UI_CORE_VERSION_TAG}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(UI_CORE_STATIC "Build core-ui as static library (single exe, no DLL)" OFF)

# 嵌入资源辅助：ui_core_embed_text(<target> FILE <p> OUT <h> VAR <name>)
# 把 .uix / .ui / .lang 等文本资源烤成 inline constexpr char 数组头文件，
# 这样 exe 可以做到完全自包含、不依赖外部资源文件。
include(cmake/UiCoreHelpers.cmake)

# 工具链：MSVC（cl.exe）/ clang-cl。MinGW 已不支持。
if(NOT MSVC)
    message(FATAL_ERROR
        "core-ui only supports MSVC / clang-cl on Windows. "
        "Use scripts/build-clang-cl.ps1 or a Visual Studio Developer prompt.")
endif()

# 默认 /MD（动态 CRT）。下游需要 vcredist；如果想要"纯单 DLL 零依赖"的发布，
# 把 UI_CORE_MSVC_STATIC_CRT 设为 ON 切到 /MT。
option(UI_CORE_MSVC_STATIC_CRT "Statically link MSVC C/C++ runtime (/MT)" OFF)
if(UI_CORE_MSVC_STATIC_CRT)
    # CMake 3.15+ 推荐用 MSVC_RUNTIME_LIBRARY 属性（cmp0091）。
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
# 源文件按 UTF-8 解析（不然带中文注释的文件会触发 C4819）
add_compile_options(/utf-8 /Zc:__cplusplus /permissive- /EHsc)
# 关掉一些 Windows 头里来源不可控的噪音
add_compile_definitions(_CRT_SECURE_NO_WARNINGS _SCL_SECURE_NO_WARNINGS)

# ---- QuickJS-NG (JS engine for .uix <script> blocks) ----
include(FetchContent)
FetchContent_Declare(
    quickjs
    GIT_REPOSITORY https://github.com/quickjs-ng/quickjs.git
    GIT_TAG        v0.14.0
    GIT_SHALLOW    TRUE
)
set(BUILD_SHARED_LIBS    OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_LIBC       OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_EXAMPLES   OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_CLI_STATIC OFF CACHE BOOL "" FORCE)
set(QJS_ENABLE_INSTALL   OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(quickjs)

# ---- LunaSVG (+ plutovg) — SVG CPU raster backend (L173 / core-ui Phase 4) ----
# vendored 源码 (含 gh-svg 自研 feDropShadow 滤镜层 + 提速), 静态编译折进 core-ui。
# core-ui 当前是 D2D + LunaSVG 混合路径:
#   - 主交互视图优先用 D2D ID2D1SvgDocument, 保持纯矢量缩放清晰。
#   - LunaSVG 负责 SVG 缩略图 BGRA、内嵌位图/复杂 SVG 的主视图栅格化,
#     以及放大/平移时按可见源矩形做视口重栅。
# 与 gh-img-decode 的 vendored LunaSVG 补丁保持同步, 避免主视图与缩略图行为分叉。
set(LUNASVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(PLUTOVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
# lunasvg/plutovg 静态折进 core-ui.dll, 不单独安装到 release/。用 EXCLUDE_FROM_ALL
# 让父级 install 脚本不递归进该子目录 —— 之前用 CMAKE_SKIP_INSTALL_RULES ON/OFF
# 包夹会抑制子目录 cmake_install.cmake 的生成, 但父级 cmake_install.cmake 仍
# 无条件 include 它, 导致 `cmake --install` 报 "include could not find requested
# file .../third_party/lunasvg/cmake_install.cmake" (L174 打 release 时暴露)。
# EXCLUDE_FROM_ALL 会传递给嵌套的 plutovg 子目录, 一并排除出 install 递归。
add_subdirectory(third_party/lunasvg EXCLUDE_FROM_ALL)
foreach(_lunasvg_t lunasvg plutovg)
    if(TARGET ${_lunasvg_t})
        target_compile_options(${_lunasvg_t} PRIVATE /W0)
    endif()
endforeach()

# ---- Core UI sources ----
set(UI_CORE_SOURCES
    src/ui/renderer.cpp
    src/ui/widget.cpp
    src/ui/controls.cpp
    src/ui/image_view_gdi.cpp
    src/ui/image_view_plus.cpp
    src/ui/gh_img_view.cpp
    src/ui/image_source.cpp
    src/ui/image_source_bitmap.cpp
    src/ui/image_source_svg.cpp
    src/ui/svg_style_inliner.cpp
    src/ui/image_source_gif.cpp
    src/ui/image_source_tiled.cpp
    src/ui/handle_table.cpp
    src/ui/asset.cpp
    src/ui/context_menu.cpp
    src/ui/overlay_service.cpp
    src/ui/easing.cpp
    src/ui/msgbox.cpp
    src/ui/display_list.cpp
    src/ui/frame_scheduler.cpp
    src/ui/render_resources.cpp
    src/ui/render_thread.cpp
    src/ui/render_window.cpp
    src/ui/resource_store.cpp
    src/ui/trace.cpp
    src/ui/ui_window.cpp
    src/ui/ui_context.cpp
    src/ui/ui_api.cpp
    src/ui/ui_debug.cpp
    src/ui/ui_debug_server.cpp
    src/ui/animation.cpp
    src/ui/animation_host.cpp
    src/ui/markup/markup_parser.cpp
    src/ui/markup/markup_builder.cpp
    src/ui/markup/markup.cpp
    src/ui/markup/markup_api.cpp
    src/ui/reactive/binding.cpp
    src/ui/expression/json.cpp
    src/ui/uix/template_parser.cpp
    src/ui/uix/sfc_parser.cpp
    src/ui/uix/script_runtime.cpp
    src/ui/uix/expr_rewriter.cpp
    src/ui/uix/value_convert.cpp
    src/ui/flex/flex.cpp
    src/ui/css/css_parser.cpp
    src/ui/css/selector.cpp
    src/ui/css/value.cpp
    src/ui/css/cascade.cpp
    src/ui/page/widget_factory.cpp
    src/ui/page/compiler.cpp
    src/ui/page/page_state.cpp
    src/ui/page/page_api.cpp
    src/ui/page/svg_widget.cpp
)

set(UI_CORE_SYSTEM_LIBS d2d1 d3d11 dxgi dxguid dwrite dwmapi uxtheme windowscodecs shlwapi gdiplus)

if(WIN32)
    function(ui_core_add_version_resource target file_desc internal_name original_name file_type)
        set(VERSION_FILE_DESCRIPTION "${file_desc}")
        set(VERSION_INTERNAL_NAME "${internal_name}")
        set(VERSION_ORIGINAL_FILENAME "${original_name}")
        set(VERSION_FILE_TYPE "${file_type}")
        set(VERSION_RESOURCE_OUT "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${target}-version.rc")
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/src/version.rc.in"
            "${VERSION_RESOURCE_OUT}"
            @ONLY
        )
        target_sources(${target} PRIVATE "${VERSION_RESOURCE_OUT}")
    endfunction()
endif()

# Win10 1709+ API（ID2D1DeviceContext5 / ID2D1SvgDocument / IDWriteTextFormat3 需要）
# 0x0A000003 = NTDDI_WIN10_RS2 (1703) —— D2D DeviceContext5 + SVG
# 0x0A000004 = NTDDI_WIN10_RS3 (1709) —— IDWriteTextFormat3（在 MSVC dwrite_3.h
#   的 RS3 守护块里，必须显式抬到这个版本才能编通）
set(UI_CORE_WIN_VERSION_DEFS
    _WIN32_WINNT=0x0A00
    WINVER=0x0A00
    NTDDI_VERSION=0x0A000004
)

if(UI_CORE_STATIC)
    # ---- Static-only build: core-ui.lib (linked into exe) ----
    add_library(core-ui STATIC ${UI_CORE_SOURCES})
    target_compile_definitions(core-ui
        PUBLIC  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX UI_CORE_STATIC
                ${UI_CORE_WIN_VERSION_DEFS}
        PRIVATE UI_CORE_BUILDING
    )
    target_link_libraries(core-ui PUBLIC ${UI_CORE_SYSTEM_LIBS} qjs lunasvg)
else()
    # ---- Shared library: core-ui.dll ----
    add_library(core-ui SHARED ${UI_CORE_SOURCES})
    set_target_properties(core-ui PROPERTIES PREFIX "")  # core-ui.dll not libcore-ui.dll
    target_compile_definitions(core-ui
        PUBLIC  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
                ${UI_CORE_WIN_VERSION_DEFS}
        PRIVATE UI_CORE_BUILDING
    )
    target_link_libraries(core-ui PRIVATE ${UI_CORE_SYSTEM_LIBS} qjs lunasvg)

    # 同时再编一份独立的"自包含静态归档"，发布到 lib/static/。
    # 这必须从源重新编译一次：UI_CORE_STATIC 宏改变了 ui_core.h 里
    # UI_API 的展开（dllexport→空），同一份 .obj 不能给两个版本共用。
    # 关掉 UI_CORE_BUILD_STATIC_ARCHIVE 可以跳过这一遍以加速 dev 迭代。
    option(UI_CORE_BUILD_STATIC_ARCHIVE
        "Also build a self-contained static archive (lib/static/core-ui.lib)" ON)
    if(UI_CORE_BUILD_STATIC_ARCHIVE)
        add_library(core-ui-static STATIC ${UI_CORE_SOURCES})
        target_compile_definitions(core-ui-static
            PUBLIC  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX UI_CORE_STATIC
                    ${UI_CORE_WIN_VERSION_DEFS}
            PRIVATE UI_CORE_BUILDING
        )
        target_link_libraries(core-ui-static PUBLIC ${UI_CORE_SYSTEM_LIBS} qjs lunasvg)
        target_include_directories(core-ui-static
            PUBLIC  include
            PRIVATE src/ui
        )
        # build 目录里输出名保留 core-ui-static.lib（防止与 DLL 的同名 import
        # lib 冲突）；install 时再 rename 成 core-ui.lib 放进 lib/static/。
    endif()
endif()

target_include_directories(core-ui
    PUBLIC  include
    PRIVATE src/ui
)

# ---- ui-demo-uix.exe — single-file .uix demo (embed app + lang + run) ----
add_executable(ui-demo-uix WIN32
    demo/ui_demo_uix.cpp
)
target_link_libraries(ui-demo-uix PRIVATE core-ui)
target_include_directories(ui-demo-uix PRIVATE include)
ui_core_embed_text(ui-demo-uix FILE demo/ui_demo.uix         OUT app_uix.embed.h VAR k_app_uix)
ui_core_embed_text(ui-demo-uix FILE demo/lang/ui_demo_zh.lang OUT lang_zh.embed.h VAR k_lang_zh)
ui_core_embed_text(ui-demo-uix FILE demo/lang/ui_demo_en.lang OUT lang_en.embed.h VAR k_lang_en)

set(UI_CORE_VERSION_DEFS
    UI_CORE_VERSION_TAG="${UI_CORE_VERSION_TAG}"
    UI_CORE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    UI_CORE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    UI_CORE_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    UI_CORE_VERSION_BUILD=${UI_CORE_INTERNAL_VERSION}
    UI_CORE_INTERNAL_VERSION=${UI_CORE_INTERNAL_VERSION}
)
target_compile_definitions(core-ui PUBLIC ${UI_CORE_VERSION_DEFS})
# core-ui-static 是并行的独立 target（fat lib 用），同样要注入版本宏 ——
# 否则 ui_core.h 里的 #ifndef 默认值 1.0.0.1 生效，下游静态链接拿到的
# ui_core_version_string() 永远报错版本。
if(TARGET core-ui-static)
    target_compile_definitions(core-ui-static PUBLIC ${UI_CORE_VERSION_DEFS})
endif()

option(UI_CORE_BUILD_INTERNAL_TESTS
    "Build internal regression test targets when test/ is available" ON)
if(UI_CORE_BUILD_INTERNAL_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/src/scroll_view_layout_test.cpp")
    add_executable(scroll_view_layout_test EXCLUDE_FROM_ALL
        test/src/scroll_view_layout_test.cpp
    )
    target_compile_definitions(scroll_view_layout_test PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(scroll_view_layout_test PRIVATE src/ui)
    target_link_libraries(scroll_view_layout_test PRIVATE core-ui)
endif()

if(UI_CORE_BUILD_INTERNAL_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/src/svg_normalize_test.cpp")
    add_executable(svg_normalize_test EXCLUDE_FROM_ALL
        test/src/svg_normalize_test.cpp
        src/ui/svg_style_inliner.cpp
        src/ui/css/css_parser.cpp
        src/ui/css/selector.cpp
    )
    target_compile_definitions(svg_normalize_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(svg_normalize_test PRIVATE src)

    add_executable(frame_scheduler_test EXCLUDE_FROM_ALL
        test/src/frame_scheduler_test.cpp
        src/ui/frame_scheduler.cpp
    )
    target_compile_definitions(frame_scheduler_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(frame_scheduler_test PRIVATE src)

    add_executable(image_cpu_backing_test EXCLUDE_FROM_ALL
        test/src/image_cpu_backing_test.cpp
    )
    target_compile_definitions(image_cpu_backing_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(image_cpu_backing_test PRIVATE src)
    if(TARGET core-ui-static)
        target_link_libraries(image_cpu_backing_test PRIVATE core-ui-static)
    else()
        target_link_libraries(image_cpu_backing_test PRIVATE core-ui)
    endif()

    add_executable(render_recovery_test EXCLUDE_FROM_ALL
        test/src/render_recovery_test.cpp
    )
    target_compile_definitions(render_recovery_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(render_recovery_test PRIVATE src)
    if(TARGET core-ui-static)
        target_link_libraries(render_recovery_test PRIVATE core-ui-static)
    else()
        target_link_libraries(render_recovery_test PRIVATE core-ui)
    endif()

    add_executable(resource_store_budget_test EXCLUDE_FROM_ALL
        test/src/resource_store_budget_test.cpp
    )
    target_compile_definitions(resource_store_budget_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(resource_store_budget_test PRIVATE src)
    if(TARGET core-ui-static)
        target_link_libraries(resource_store_budget_test PRIVATE core-ui-static)
    else()
        target_link_libraries(resource_store_budget_test PRIVATE core-ui)
    endif()

    add_executable(svg_text_path_test EXCLUDE_FROM_ALL
        test/src/svg_text_path_test.cpp
    )
    target_compile_definitions(svg_text_path_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(svg_text_path_test PRIVATE src)
    if(TARGET core-ui-static)
        target_link_libraries(svg_text_path_test PRIVATE core-ui-static)
    else()
        target_link_libraries(svg_text_path_test PRIVATE core-ui)
    endif()

    add_executable(svg_drop_shadow_layer_test EXCLUDE_FROM_ALL
        test/src/svg_drop_shadow_layer_test.cpp
    )
    target_compile_definitions(svg_drop_shadow_layer_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(svg_drop_shadow_layer_test PRIVATE src)
    if(TARGET core-ui-static)
        target_link_libraries(svg_drop_shadow_layer_test PRIVATE core-ui-static)
    else()
        target_link_libraries(svg_drop_shadow_layer_test PRIVATE core-ui)
    endif()
endif()

if(WIN32)
    if(UI_CORE_STATIC)
        ui_core_add_version_resource(
            core-ui
            "Core UI static library"
            "core-ui.lib"
            "core-ui.lib"
            "VFT_STATIC_LIB"
        )
    else()
        ui_core_add_version_resource(
            core-ui
            "Core UI dynamic library"
            "core-ui.dll"
            "core-ui.dll"
            "VFT_DLL"
        )
    endif()

endif()

install(
    DIRECTORY include/
    DESTINATION "${UI_CORE_RELEASE_DIR_NAME}/include"
    FILES_MATCHING
    PATTERN "*.h"
)

# 文档：UI_CORE_API.md 是目录页，里面的链接指向 docs/*.md，必须一起带上。
install(
    FILES UI_CORE_API.md README.md CHANGELOG.md
    DESTINATION "${UI_CORE_RELEASE_DIR_NAME}"
)
install(
    DIRECTORY docs/
    DESTINATION "${UI_CORE_RELEASE_DIR_NAME}/docs"
    FILES_MATCHING PATTERN "*.md"
)

if(UI_CORE_STATIC)
    install(
        TARGETS core-ui
        ARCHIVE DESTINATION "${UI_CORE_RELEASE_DIR_NAME}/lib/static"
    )
else()
    install(
        TARGETS core-ui
        RUNTIME DESTINATION "${UI_CORE_RELEASE_DIR_NAME}/lib/dynamic"
        ARCHIVE DESTINATION "${UI_CORE_RELEASE_DIR_NAME}/lib/dynamic"
    )
    if(UI_CORE_BUILD_STATIC_ARCHIVE AND TARGET core-ui-static)
        # 合并 core-ui-static.lib + qjs.lib 成一个胖归档 core-ui-fat.lib：
        # 下游静态链接 lib/static/core-ui.lib 时不会再缺 JS_* 符号。
        # （直接装 core-ui-static.lib 漏掉 qjs，下游会拿到 ~50 个
        # `unresolved external __imp_JS_xxx` —— 用户反馈的"不能静态编译"。）
        # Use cmake's archiver (clang-cl picks llvm-lib.exe; MSVC picks
        # lib.exe). Both accept the same /OUT: syntax.
        set(_fat_static "${CMAKE_BINARY_DIR}/core-ui-fat.lib")
        add_custom_command(
            OUTPUT  "${_fat_static}"
            COMMAND "${CMAKE_AR}" /NOLOGO
                    "/OUT:${_fat_static}"
                    "$<TARGET_FILE:core-ui-static>"
                    "$<TARGET_FILE:qjs>"
                    "$<TARGET_FILE:lunasvg>"
                    "$<TARGET_FILE:plutovg>"
            DEPENDS core-ui-static qjs lunasvg plutovg
            COMMENT "Bundling core-ui-static + qjs → core-ui-fat.lib"
            VERBATIM
        )
        add_custom_target(core-ui-fat-static ALL DEPENDS "${_fat_static}")
        install(
            FILES "${_fat_static}"
            DESTINATION "${UI_CORE_RELEASE_DIR_NAME}/lib/static"
            RENAME "core-ui.lib"
        )
    endif()

endif()

# 给下游用户的 CMake helper：ui_core_embed_text() 用来把任意文件烤成
# inline constexpr char[] 头文件，做单 exe 打包。
install(
    DIRECTORY cmake/
    DESTINATION "${UI_CORE_RELEASE_DIR_NAME}/cmake"
    FILES_MATCHING PATTERN "*.cmake"
)

add_custom_target(release-package
    COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}" --prefix "${CMAKE_SOURCE_DIR}/release"
    COMMENT "Create release package in release/${UI_CORE_RELEASE_DIR_NAME}"
)
# add_custom_target 默认不依赖任何 build target，cmake --install 只复制不编译。
# 之前踩过坑：dev 改了 src/ui/controls.cpp 重打 release-package，结果 dll 是
# 新的，core-ui.dll → 1.4.0.NEW；但 lib/static/core-ui.lib 是好几次构建之前的旧
# 版本（因为没人 build core-ui-static），下游静态链接的 ChromePlusGUI 重 link
# 之后还是旧行为。这里把所有 install 用到的 target 全声明依赖，避免再次踩坑。
set(_release_deps core-ui)
if(UI_CORE_BUILD_STATIC_ARCHIVE AND TARGET core-ui-static)
    list(APPEND _release_deps core-ui-static)
    # 同时把 fat lib 拉进来 —— install 装的 lib/static/core-ui.lib 是
    # core-ui-fat.lib（core-ui-static + qjs bundle）。仅依赖 core-ui-static
    # 时 release-package 不会触发 fat lib 重建，install 拷过去的是旧 fat。
    # 之前踩过：dynamic build 改了源 → release-package → release/lib/static/
    # 还是几次构建之前的版本，下游静态链接后行为没变。
    if(TARGET core-ui-fat-static)
        list(APPEND _release_deps core-ui-fat-static)
    endif()
endif()
add_dependencies(release-package ${_release_deps})
