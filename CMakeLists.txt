cmake_minimum_required(VERSION 3.20)

set(UI_CORE_VERSION "1.9.0")
set(UI_CORE_INTERNAL_VERSION "345")
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
    src/ui/ocr_text_model.cpp
    src/ui/ocr_img_view.cpp
    src/ui/image_source.cpp
    src/ui/image_source_bitmap.cpp
    src/ui/image_source_svg.cpp
    src/ui/svg_style_inliner.cpp
    src/ui/svg_dom.cpp
    src/ui/svg_d2d_segments.cpp
    src/ui/svg_font.cpp
    src/ui/image_shadow_geom.cpp
    src/ui/zoom_motion.cpp
    src/ui/menu_open_motion.cpp
    src/ui/toggle_visual.cpp
    src/ui/anim_clock.cpp
    src/ui/frame_ticker.cpp
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
    src/ui/layered_frame.cpp
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
    src/ui/page/svg_symbols.cpp
)

set(UI_CORE_SYSTEM_LIBS d2d1 d3d11 dxgi dxguid dwrite dwmapi uxtheme windowscodecs shlwapi gdiplus imm32)

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

# ---- ocr-demo.exe — ui_ocr_img_view 图片划词集成 demo ----
add_executable(ocr-demo WIN32 EXCLUDE_FROM_ALL
    demo/ocr_demo.cpp
)
target_link_libraries(ocr-demo PRIVATE core-ui)
target_include_directories(ocr-demo PRIVATE include)

# ---- ocr-uix-demo.exe — <ocr-img-view> 的 .uix 声明式集成 demo ----
# 画布在 .uix 里声明, 宿主 find_by_id 后喂数据; 附带"行列表 ↔ 画布"双向
# 联动 (select_range / selection_range / item_meta) 的可自动化验证。
add_executable(ocr-uix-demo WIN32 EXCLUDE_FROM_ALL
    demo/ocr_uix_demo.cpp
)
target_link_libraries(ocr-uix-demo PRIVATE core-ui)
target_include_directories(ocr-uix-demo PRIVATE include)
ui_core_embed_text(ocr-uix-demo FILE demo/ocr_uix_demo.uix
    OUT ocr_uix_demo_uix.embed.h VAR k_ocr_uix_demo_uix)

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
if(UI_CORE_BUILD_INTERNAL_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test/src/svg_normalize_test.cpp")
    # 测试全部用 assert() 断言。Release 构建带 /DNDEBUG 会把 assert 编成空操作 ——
    # 那样测试只会打印 PASS 而什么都不检查 (2026-07 发现: 全部 9 个测试目标已空跑
    # 了很久)。/UNDEBUG 撤销该宏, 让断言在任何 CMAKE_BUILD_TYPE 下都真正生效。
    # 新增测试目标务必调用它。
    function(ui_core_mark_test target)
        target_compile_options(${target} PRIVATE /UNDEBUG)
        # 测试隔离: 测试窗口不抢用户前台 / 焦点, 不理真实鼠标 (src/ui/test_isolation.h)
        target_sources(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/test/src/test_isolation_env.cpp)
    endfunction()

    add_executable(svg_normalize_test EXCLUDE_FROM_ALL
        test/src/svg_normalize_test.cpp
        src/ui/svg_style_inliner.cpp
        src/ui/css/css_parser.cpp
        src/ui/css/selector.cpp
        src/ui/css/value.cpp
    )
    target_compile_definitions(svg_normalize_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(svg_normalize_test PRIVATE src)
    ui_core_mark_test(svg_normalize_test)

    # ---- 从 test/CMakeLists.txt 迁回来的纯逻辑测试 ----------------------------
    # 那个文件从来没被 add_subdirectory 过, 里面的目标一个都构建不到, 这 5 个测试
    # 就此静静躺了很久 (2026-07-30 发现)。测试目标只能定义在本文件里。
    add_executable(flex_test EXCLUDE_FROM_ALL
        test/src/flex_test.cpp
        src/ui/flex/flex.cpp
    )
    target_compile_definitions(flex_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(flex_test PRIVATE src)
    ui_core_mark_test(flex_test)

    # 全局 COM 对象退出不 Release (exit_safe_com.h)。纯 header, 不链 core-ui。
    add_executable(exit_leak_com_test EXCLUDE_FROM_ALL
        test/src/exit_leak_com_test.cpp
    )
    target_compile_definitions(exit_leak_com_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(exit_leak_com_test PRIVATE src)
    ui_core_mark_test(exit_leak_com_test)

    # 右键菜单磨砂背景 box blur (ui/box_blur.h)。纯 header, 不链 core-ui。
    add_executable(box_blur_test EXCLUDE_FROM_ALL
        test/src/box_blur_test.cpp
    )
    target_compile_definitions(box_blur_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(box_blur_test PRIVATE src)
    ui_core_mark_test(box_blur_test)

    add_executable(css_parser_test EXCLUDE_FROM_ALL
        test/src/css_parser_test.cpp
        src/ui/css/css_parser.cpp
    )
    target_compile_definitions(css_parser_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(css_parser_test PRIVATE src)
    ui_core_mark_test(css_parser_test)

    add_executable(css_match_test EXCLUDE_FROM_ALL
        test/src/css_match_test.cpp
        src/ui/css/css_parser.cpp
        src/ui/css/selector.cpp
        src/ui/css/value.cpp
        src/ui/css/cascade.cpp
    )
    target_compile_definitions(css_match_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(css_match_test PRIVATE src)
    ui_core_mark_test(css_match_test)

    add_executable(expr_rewriter_test EXCLUDE_FROM_ALL
        test/src/expr_rewriter_test.cpp
        src/ui/uix/expr_rewriter.cpp
    )
    target_compile_definitions(expr_rewriter_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(expr_rewriter_test PRIVATE src)
    ui_core_mark_test(expr_rewriter_test)

    add_executable(reactive_test EXCLUDE_FROM_ALL
        test/src/reactive_test.cpp
        src/ui/reactive/binding.cpp
    )
    target_compile_definitions(reactive_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(reactive_test PRIVATE src)
    ui_core_mark_test(reactive_test)

    # 属性动画"按属性取消"语义。animation.cpp 会经 GetContext() 回窗口层,
    # 单独编两个 .cpp 链不上, 所以跟 image_cpu_backing_test 一样链整个 lib。
    add_executable(property_anim_cancel_test EXCLUDE_FROM_ALL
        test/src/property_anim_cancel_test.cpp
    )
    target_compile_definitions(property_anim_cancel_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(property_anim_cancel_test PRIVATE src)
    ui_core_mark_test(property_anim_cancel_test)
    if(TARGET core-ui-static)
        target_link_libraries(property_anim_cancel_test PRIVATE core-ui-static)
    else()
        target_link_libraries(property_anim_cancel_test PRIVATE core-ui)
    endif()

    add_executable(frame_scheduler_test EXCLUDE_FROM_ALL
        test/src/frame_scheduler_test.cpp
        src/ui/frame_scheduler.cpp
    )
    target_compile_definitions(frame_scheduler_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(frame_scheduler_test PRIVATE src)
    ui_core_mark_test(frame_scheduler_test)

    add_executable(image_cpu_backing_test EXCLUDE_FROM_ALL
        test/src/image_cpu_backing_test.cpp
    )
    target_compile_definitions(image_cpu_backing_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(image_cpu_backing_test PRIVATE src)
    ui_core_mark_test(image_cpu_backing_test)
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
    ui_core_mark_test(render_recovery_test)
    if(TARGET core-ui-static)
        target_link_libraries(render_recovery_test PRIVATE core-ui-static)
    else()
        target_link_libraries(render_recovery_test PRIVATE core-ui)
    endif()

    # OverlayService::InvokeAsync: 不等待返回 + 与 InvokeSync 同队列保序。
    add_executable(overlay_invoke_async_test EXCLUDE_FROM_ALL
        test/src/overlay_invoke_async_test.cpp
    )
    target_compile_definitions(overlay_invoke_async_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(overlay_invoke_async_test PRIVATE src)
    ui_core_mark_test(overlay_invoke_async_test)
    if(TARGET core-ui-static)
        target_link_libraries(overlay_invoke_async_test PRIVATE core-ui-static)
    else()
        target_link_libraries(overlay_invoke_async_test PRIVATE core-ui)
    endif()

    # OverlayService toast 调试查询 (build 330): info 屏障 / 截图 / text_clipped。
    add_executable(toast_debug_test EXCLUDE_FROM_ALL
        test/src/toast_debug_test.cpp
    )
    target_compile_definitions(toast_debug_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(toast_debug_test PRIVATE src)
    ui_core_mark_test(toast_debug_test)
    if(TARGET core-ui-static)
        target_link_libraries(toast_debug_test PRIVATE core-ui-static windowscodecs ole32)
    else()
        target_link_libraries(toast_debug_test PRIVATE core-ui windowscodecs ole32)
    endif()
    add_executable(resource_store_budget_test EXCLUDE_FROM_ALL
        test/src/resource_store_budget_test.cpp
    )
    target_compile_definitions(resource_store_budget_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(resource_store_budget_test PRIVATE src)
    ui_core_mark_test(resource_store_budget_test)
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
    ui_core_mark_test(svg_text_path_test)
    if(TARGET core-ui-static)
        target_link_libraries(svg_text_path_test PRIVATE core-ui-static)
    else()
        target_link_libraries(svg_text_path_test PRIVATE core-ui)
    endif()

    # 图片阴影几何: 纯算术, 不碰 D2D。
    add_executable(image_shadow_geom_test EXCLUDE_FROM_ALL
        test/src/image_shadow_geom_test.cpp
        src/ui/image_shadow_geom.cpp
    )
    target_compile_definitions(image_shadow_geom_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(image_shadow_geom_test PRIVATE src)
    ui_core_mark_test(image_shadow_geom_test)

    # Smooth zoom motion (Qt-style linear segments): pure arithmetic, no D2D.
    add_executable(zoom_motion_test EXCLUDE_FROM_ALL
        test/src/zoom_motion_test.cpp
        src/ui/zoom_motion.cpp
    )
    target_compile_definitions(zoom_motion_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(zoom_motion_test PRIVATE src)
    ui_core_mark_test(zoom_motion_test)

    # 弹出菜单打开动画 (WinUI MenuPopupThemeTransition 曲线 / 几何): 纯算术, 不碰 DComp。
    add_executable(menu_open_motion_test EXCLUDE_FROM_ALL
        test/src/menu_open_motion_test.cpp
        src/ui/menu_open_motion.cpp
    )
    target_compile_definitions(menu_open_motion_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(menu_open_motion_test PRIVATE src)
    ui_core_mark_test(menu_open_motion_test)

    add_executable(easing_test EXCLUDE_FROM_ALL
        test/src/easing_test.cpp
        src/ui/easing.cpp
        src/ui/menu_open_motion.cpp
        src/ui/animation.cpp
        src/ui/anim_clock.cpp
    )
    target_compile_definitions(easing_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX UI_CORE_STATIC)
    target_include_directories(easing_test PRIVATE src)
    ui_core_mark_test(easing_test)

    # 动画时钟: QPC 毫秒, 非 0、单调、1ms 步长。
    add_executable(anim_clock_test EXCLUDE_FROM_ALL
        test/src/anim_clock_test.cpp
        src/ui/anim_clock.cpp
    )
    target_compile_definitions(anim_clock_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(anim_clock_test PRIVATE src)
    ui_core_mark_test(anim_clock_test)

    # 动画心跳: vblank 对齐的唤醒时刻 + 合并 / 退订 (真线程)。
    add_executable(frame_ticker_test EXCLUDE_FROM_ALL
        test/src/frame_ticker_test.cpp
        src/ui/frame_ticker.cpp
    )
    target_compile_definitions(frame_ticker_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(frame_ticker_test PRIVATE src)
    target_link_libraries(frame_ticker_test PRIVATE dwmapi)
    ui_core_mark_test(frame_ticker_test)

    # 开关圆点 / 轨道的状态几何与配色 (WinUI ToggleSwitch): 纯算术。
    add_executable(toggle_visual_test EXCLUDE_FROM_ALL
        test/src/toggle_visual_test.cpp
        src/ui/toggle_visual.cpp
    )
    target_compile_definitions(toggle_visual_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(toggle_visual_test PRIVATE src)
    ui_core_mark_test(toggle_visual_test)

    # GhImgView smooth zoom behaviour: links the whole lib (widget + context).
    add_executable(gh_img_view_zoom_anim_test EXCLUDE_FROM_ALL
        test/src/gh_img_view_zoom_anim_test.cpp
    )
    target_compile_definitions(gh_img_view_zoom_anim_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(gh_img_view_zoom_anim_test PRIVATE src)
    ui_core_mark_test(gh_img_view_zoom_anim_test)
    if(TARGET core-ui-static)
        target_link_libraries(gh_img_view_zoom_anim_test PRIVATE core-ui-static)
    else()
        target_link_libraries(gh_img_view_zoom_anim_test PRIVATE core-ui)
    endif()

    # 最小 SVG DOM 是无依赖的纯逻辑模块, 直接编源文件, 不必链整个 core-ui。
    add_executable(svg_dom_test EXCLUDE_FROM_ALL
        test/src/svg_dom_test.cpp
        src/ui/svg_dom.cpp
    )
    target_compile_definitions(svg_dom_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(svg_dom_test PRIVATE src)
    ui_core_mark_test(svg_dom_test)

    # SVG 1.1 SVG Fonts (<font>/<glyph>) 也是无依赖的纯逻辑模块, 同样直接编源文件。
    add_executable(svg_font_test EXCLUDE_FROM_ALL
        test/src/svg_font_test.cpp
        src/ui/svg_font.cpp
    )
    target_compile_definitions(svg_font_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(svg_font_test PRIVATE src)
    ui_core_mark_test(svg_font_test)

    add_executable(svg_d2d_segments_test EXCLUDE_FROM_ALL
        test/src/svg_d2d_segments_test.cpp
        src/ui/svg_d2d_segments.cpp
        src/ui/svg_dom.cpp
    )
    target_compile_definitions(svg_d2d_segments_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(svg_d2d_segments_test PRIVATE src)
    ui_core_mark_test(svg_d2d_segments_test)

    add_executable(svg_drop_shadow_layer_test EXCLUDE_FROM_ALL
        test/src/svg_drop_shadow_layer_test.cpp
    )
    target_compile_definitions(svg_drop_shadow_layer_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(svg_drop_shadow_layer_test PRIVATE src)
    ui_core_mark_test(svg_drop_shadow_layer_test)
    if(TARGET core-ui-static)
        target_link_libraries(svg_drop_shadow_layer_test PRIVATE core-ui-static)
    else()
        target_link_libraries(svg_drop_shadow_layer_test PRIVATE core-ui)
    endif()

    add_executable(ocr_text_model_test EXCLUDE_FROM_ALL
        test/src/ocr_text_model_test.cpp
        src/ui/ocr_text_model.cpp
    )
    target_compile_definitions(ocr_text_model_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ocr_text_model_test PRIVATE src)
    ui_core_mark_test(ocr_text_model_test)

    # 宿主联动 C API (select_range / selection_range / item_meta) 走真实
    # C ABI, 所以链 lib 而不是只编 model 源文件。
    add_executable(ocr_c_api_test EXCLUDE_FROM_ALL
        test/src/ocr_c_api_test.cpp
    )
    target_compile_definitions(ocr_c_api_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(ocr_c_api_test PRIVATE include)
    ui_core_mark_test(ocr_c_api_test)
    if(TARGET core-ui-static)
        target_link_libraries(ocr_c_api_test PRIVATE core-ui-static)
    else()
        target_link_libraries(ocr_c_api_test PRIVATE core-ui)
    endif()

    add_executable(combo_box_dropdown_test EXCLUDE_FROM_ALL
        test/src/combo_box_dropdown_test.cpp
    )
    target_compile_definitions(combo_box_dropdown_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(combo_box_dropdown_test PRIVATE src)
    ui_core_mark_test(combo_box_dropdown_test)
    if(TARGET core-ui-static)
        target_link_libraries(combo_box_dropdown_test PRIVATE core-ui-static)
    else()
        target_link_libraries(combo_box_dropdown_test PRIVATE core-ui)
    endif()

    # build 285: Widget 的批量子节点原语 (ReplaceChildRange / RemoveChildren)。
    # 纯下标算术, 错了不崩只错位, 必须钉住边界与顺序。
    add_executable(widget_children_test EXCLUDE_FROM_ALL
        test/src/widget_children_test.cpp
    )
    target_compile_definitions(widget_children_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(widget_children_test PRIVATE src)
    ui_core_mark_test(widget_children_test)
    if(TARGET core-ui-static)
        target_link_libraries(widget_children_test PRIVATE core-ui-static)
    else()
        target_link_libraries(widget_children_test PRIVATE core-ui)
    endif()

    # build 343: 静态子菜单悬停展开 (非 UI 线程跑 beforeShowHook) 内容不能变回模板原样。
    add_executable(menu_submenu_thread_test EXCLUDE_FROM_ALL
        test/src/menu_submenu_thread_test.cpp
    )
    target_compile_definitions(menu_submenu_thread_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(menu_submenu_thread_test PRIVATE src)
    ui_core_mark_test(menu_submenu_thread_test)
    if(TARGET core-ui-static)
        target_link_libraries(menu_submenu_thread_test PRIVATE core-ui-static)
    else()
        target_link_libraries(menu_submenu_thread_test PRIVATE core-ui)
    endif()

    # build 341: TitleBar 扩展控制按钮布局 (紧贴最小化 / 隐藏不占位 / custom 在左)。
    add_executable(titlebar_caption_test EXCLUDE_FROM_ALL
        test/src/titlebar_caption_test.cpp
    )
    target_compile_definitions(titlebar_caption_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(titlebar_caption_test PRIVATE src)
    ui_core_mark_test(titlebar_caption_test)
    if(TARGET core-ui-static)
        target_link_libraries(titlebar_caption_test PRIVATE core-ui-static)
    else()
        target_link_libraries(titlebar_caption_test PRIVATE core-ui)
    endif()

    # build 323: 折行内容按分到的宽度定高度 (Widget::HeightForWidth)。
    add_executable(wrap_height_layout_test EXCLUDE_FROM_ALL
        test/src/wrap_height_layout_test.cpp
    )
    target_compile_definitions(wrap_height_layout_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(wrap_height_layout_test PRIVATE src)
    ui_core_mark_test(wrap_height_layout_test)
    if(TARGET core-ui-static)
        target_link_libraries(wrap_height_layout_test PRIVATE core-ui-static)
    else()
        target_link_libraries(wrap_height_layout_test PRIVATE core-ui)
    endif()

    # build 328: <window height="auto"> 窗口高度跟随内容 (AutoHeightTargetDip)。
    add_executable(auto_height_test EXCLUDE_FROM_ALL
        test/src/auto_height_test.cpp
    )
    target_compile_definitions(auto_height_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(auto_height_test PRIVATE src)
    ui_core_mark_test(auto_height_test)
    if(TARGET core-ui-static)
        target_link_libraries(auto_height_test PRIVATE core-ui-static)
    else()
        target_link_libraries(auto_height_test PRIVATE core-ui)
    endif()

    # build 299: 命令式写过的字段 (:background / :left / ui_widget_set_bg_color)
    # 能不能扛住一次 CSS 重算。漏补一个字段不会崩, 只会"绑了等于没绑", 必须钉住。
    add_executable(dynamic_overrides_test EXCLUDE_FROM_ALL
        test/src/dynamic_overrides_test.cpp
    )
    target_compile_definitions(dynamic_overrides_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(dynamic_overrides_test PRIVATE src)
    ui_core_mark_test(dynamic_overrides_test)
    if(TARGET core-ui-static)
        target_link_libraries(dynamic_overrides_test PRIVATE core-ui-static)
    else()
        target_link_libraries(dynamic_overrides_test PRIVATE core-ui)
    endif()

    # build 303: v-for 行内的 @event 要跟普通元素同一套分派 (拖放 / 右键 /
    # 鼠标 / 焦点)。行内漏接不报错只是静默失效, 必须钉住。
    add_executable(loop_scope_events_test EXCLUDE_FROM_ALL
        test/src/loop_scope_events_test.cpp
    )
    target_compile_definitions(loop_scope_events_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(loop_scope_events_test PRIVATE src include)
    ui_core_mark_test(loop_scope_events_test)
    if(TARGET core-ui-static)
        target_link_libraries(loop_scope_events_test PRIVATE core-ui-static)
    else()
        target_link_libraries(loop_scope_events_test PRIVATE core-ui)
    endif()

    # build 303: 声明式菜单按数据循环生成时, 每一项的 id / 属性 / 点击动作 /
    # 子菜单都要各是各的 (对齐 HTML/Vue)。
    add_executable(menu_loop_test EXCLUDE_FROM_ALL
        test/src/menu_loop_test.cpp
    )
    target_compile_definitions(menu_loop_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(menu_loop_test PRIVATE src include)
    ui_core_mark_test(menu_loop_test)
    if(TARGET core-ui-static)
        target_link_libraries(menu_loop_test PRIVATE core-ui-static)
    else()
        target_link_libraries(menu_loop_test PRIVATE core-ui)
    endif()

    # build 303: 拖放贴边自动滚动的步长 (纯算术, header-only)。
    add_executable(drag_autoscroll_test EXCLUDE_FROM_ALL
        test/src/drag_autoscroll_test.cpp
    )
    target_compile_definitions(drag_autoscroll_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(drag_autoscroll_test PRIVATE src)
    ui_core_mark_test(drag_autoscroll_test)

    # build 303: 真窗口里的拖放贴边自动滚动 (定时器 + ScrollView + dragover)。
    add_executable(drag_autoscroll_window_test EXCLUDE_FROM_ALL
        test/src/drag_autoscroll_window_test.cpp
    )
    target_compile_definitions(drag_autoscroll_window_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(drag_autoscroll_window_test PRIVATE src include)
    ui_core_mark_test(drag_autoscroll_window_test)
    if(TARGET core-ui-static)
        target_link_libraries(drag_autoscroll_window_test PRIVATE core-ui-static)
    else()
        target_link_libraries(drag_autoscroll_window_test PRIVATE core-ui)
    endif()

    # build 309: gh_img_view 视图变换回调 (拖动每次移动 / 动画每帧 + interacting 标志)。
    add_executable(gh_img_view_transform_test EXCLUDE_FROM_ALL
        test/src/gh_img_view_transform_test.cpp
    )
    target_compile_definitions(gh_img_view_transform_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(gh_img_view_transform_test PRIVATE src)
    ui_core_mark_test(gh_img_view_transform_test)
    if(TARGET core-ui-static)
        target_link_libraries(gh_img_view_transform_test PRIVATE core-ui-static)
    else()
        target_link_libraries(gh_img_view_transform_test PRIVATE core-ui)
    endif()

    # build 308: 松开丢在窗口外 (没抓 capture 的控件按下后移出窗口松开) 的兜底。
    add_executable(lost_mouse_up_test EXCLUDE_FROM_ALL
        test/src/lost_mouse_up_test.cpp
    )
    target_compile_definitions(lost_mouse_up_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(lost_mouse_up_test PRIVATE src include)
    ui_core_mark_test(lost_mouse_up_test)
    if(TARGET core-ui-static)
        target_link_libraries(lost_mouse_up_test PRIVATE core-ui-static)
    else()
        target_link_libraries(lost_mouse_up_test PRIVATE core-ui)
    endif()

    # build 323: .uix 静态 visible 属性建控件时生效。
    add_executable(static_visible_attr_test EXCLUDE_FROM_ALL
        test/src/static_visible_attr_test.cpp
    )
    target_compile_definitions(static_visible_attr_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(static_visible_attr_test PRIVATE src include)
    ui_core_mark_test(static_visible_attr_test)
    if(TARGET core-ui-static)
        target_link_libraries(static_visible_attr_test PRIVATE core-ui-static)
    else()
        target_link_libraries(static_visible_attr_test PRIVATE core-ui)
    endif()

    # build 323: ui_window_show_immediate 返回时首帧已 Present (不再露系统底色)。
    add_executable(show_first_present_test EXCLUDE_FROM_ALL
        test/src/show_first_present_test.cpp
    )
    target_compile_definitions(show_first_present_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(show_first_present_test PRIVATE src include)
    ui_core_mark_test(show_first_present_test)
    if(TARGET core-ui-static)
        target_link_libraries(show_first_present_test PRIVATE core-ui-static)
    else()
        target_link_libraries(show_first_present_test PRIVATE core-ui)
    endif()

    # build 323: 画布模式窗口双击 (WM_NCLBUTTONDBLCLK HTCAPTION) 不最大化。
    add_executable(canvas_dblclick_test EXCLUDE_FROM_ALL
        test/src/canvas_dblclick_test.cpp
    )
    target_compile_definitions(canvas_dblclick_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(canvas_dblclick_test PRIVATE src include)
    ui_core_mark_test(canvas_dblclick_test)
    if(TARGET core-ui-static)
        target_link_libraries(canvas_dblclick_test PRIVATE core-ui-static)
    else()
        target_link_libraries(canvas_dblclick_test PRIVATE core-ui)
    endif()

    # build 304: SVG sprite —— <symbol> 定义一次, <use href> 按代号引用 (含外部文件)。
    add_executable(svg_use_test EXCLUDE_FROM_ALL
        test/src/svg_use_test.cpp
    )
    target_compile_definitions(svg_use_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(svg_use_test PRIVATE src include)
    ui_core_mark_test(svg_use_test)
    if(TARGET core-ui-static)
        target_link_libraries(svg_use_test PRIVATE core-ui-static)
    else()
        target_link_libraries(svg_use_test PRIVATE core-ui)
    endif()

    # build 304: ui_page_watch —— 页面状态变化通知宿主 (对齐 Vue watch)。
    add_executable(page_watch_test EXCLUDE_FROM_ALL
        test/src/page_watch_test.cpp
    )
    target_compile_definitions(page_watch_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(page_watch_test PRIVATE src include)
    ui_core_mark_test(page_watch_test)
    if(TARGET core-ui-static)
        target_link_libraries(page_watch_test PRIVATE core-ui-static)
    else()
        target_link_libraries(page_watch_test PRIVATE core-ui)
    endif()

    # build 332: 宿主漏了 ui_page_destroy, ui_shutdown / 进程退出都不能崩。
    add_executable(page_shutdown_test EXCLUDE_FROM_ALL
        test/src/page_shutdown_test.cpp
    )
    target_compile_definitions(page_shutdown_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(page_shutdown_test PRIVATE src include)
    ui_core_mark_test(page_shutdown_test)
    if(TARGET core-ui-static)
        target_link_libraries(page_shutdown_test PRIVATE core-ui-static)
    else()
        target_link_libraries(page_shutdown_test PRIVATE core-ui)
    endif()

    # build 306: 一次排版请求要让每个窗口都重排 (两个真窗口)。
    add_executable(multi_window_layout_test EXCLUDE_FROM_ALL
        test/src/multi_window_layout_test.cpp
    )
    target_compile_definitions(multi_window_layout_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(multi_window_layout_test PRIVATE src include)
    ui_core_mark_test(multi_window_layout_test)
    if(TARGET core-ui-static)
        target_link_libraries(multi_window_layout_test PRIVATE core-ui-static)
    else()
        target_link_libraries(multi_window_layout_test PRIVATE core-ui)
    endif()

    # build 306: @dblclick 沿父链找最近的监听者 (真窗口)。
    add_executable(dblclick_bubble_test EXCLUDE_FROM_ALL
        test/src/dblclick_bubble_test.cpp
    )
    target_compile_definitions(dblclick_bubble_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(dblclick_bubble_test PRIVATE src include)
    ui_core_mark_test(dblclick_bubble_test)
    if(TARGET core-ui-static)
        target_link_libraries(dblclick_bubble_test PRIVATE core-ui-static)
    else()
        target_link_libraries(dblclick_bubble_test PRIVATE core-ui)
    endif()

    # 开关快速双击只切换一次, 对标 WinUI ToggleSwitch (真窗口)。
    add_executable(test_isolation_test EXCLUDE_FROM_ALL
        test/src/test_isolation_test.cpp
    )
    target_compile_definitions(test_isolation_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(test_isolation_test PRIVATE src include)
    ui_core_mark_test(test_isolation_test)
    if(TARGET core-ui-static)
        target_link_libraries(test_isolation_test PRIVATE core-ui-static)
    else()
        target_link_libraries(test_isolation_test PRIVATE core-ui)
    endif()

    add_executable(sim_press_capture_test EXCLUDE_FROM_ALL
        test/src/sim_press_capture_test.cpp
    )
    target_compile_definitions(sim_press_capture_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(sim_press_capture_test PRIVATE src include)
    ui_core_mark_test(sim_press_capture_test)
    if(TARGET core-ui-static)
        target_link_libraries(sim_press_capture_test PRIVATE core-ui-static)
    else()
        target_link_libraries(sim_press_capture_test PRIVATE core-ui)
    endif()

    add_executable(toast_frames_test EXCLUDE_FROM_ALL
        test/src/toast_frames_test.cpp
    )
    target_compile_definitions(toast_frames_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(toast_frames_test PRIVATE src include)
    ui_core_mark_test(toast_frames_test)
    if(TARGET core-ui-static)
        target_link_libraries(toast_frames_test PRIVATE core-ui-static)
    else()
        target_link_libraries(toast_frames_test PRIVATE core-ui)
    endif()

    add_executable(window_open_anim_test EXCLUDE_FROM_ALL
        test/src/window_open_anim_test.cpp
    )
    target_compile_definitions(window_open_anim_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(window_open_anim_test PRIVATE src include)
    ui_core_mark_test(window_open_anim_test)
    if(TARGET core-ui-static)
        target_link_libraries(window_open_anim_test PRIVATE core-ui-static)
    else()
        target_link_libraries(window_open_anim_test PRIVATE core-ui)
    endif()

    add_executable(mouse_move_frames_test EXCLUDE_FROM_ALL
        test/src/mouse_move_frames_test.cpp
    )
    target_compile_definitions(mouse_move_frames_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(mouse_move_frames_test PRIVATE src include)
    ui_core_mark_test(mouse_move_frames_test)
    if(TARGET core-ui-static)
        target_link_libraries(mouse_move_frames_test PRIVATE core-ui-static)
    else()
        target_link_libraries(mouse_move_frames_test PRIVATE core-ui)
    endif()

    add_executable(anim_tick_test EXCLUDE_FROM_ALL
        test/src/anim_tick_test.cpp
    )
    target_compile_definitions(anim_tick_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(anim_tick_test PRIVATE src include)
    ui_core_mark_test(anim_tick_test)
    if(TARGET core-ui-static)
        target_link_libraries(anim_tick_test PRIVATE core-ui-static)
    else()
        target_link_libraries(anim_tick_test PRIVATE core-ui)
    endif()

    add_executable(present_pacing_test EXCLUDE_FROM_ALL
        test/src/present_pacing_test.cpp
    )
    target_compile_definitions(present_pacing_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(present_pacing_test PRIVATE src include)
    ui_core_mark_test(present_pacing_test)
    if(TARGET core-ui-static)
        target_link_libraries(present_pacing_test PRIVATE core-ui-static)
    else()
        target_link_libraries(present_pacing_test PRIVATE core-ui)
    endif()

    add_executable(bitmap_handle_test EXCLUDE_FROM_ALL
        test/src/bitmap_handle_test.cpp
    )
    target_compile_definitions(bitmap_handle_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(bitmap_handle_test PRIVATE src include)
    ui_core_mark_test(bitmap_handle_test)
    if(TARGET core-ui-static)
        target_link_libraries(bitmap_handle_test PRIVATE core-ui-static)
    else()
        target_link_libraries(bitmap_handle_test PRIVATE core-ui)
    endif()

    add_executable(composite_layer_test EXCLUDE_FROM_ALL
        test/src/composite_layer_test.cpp
    )
    target_compile_definitions(composite_layer_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(composite_layer_test PRIVATE src include)
    ui_core_mark_test(composite_layer_test)
    if(TARGET core-ui-static)
        target_link_libraries(composite_layer_test PRIVATE core-ui-static)
    else()
        target_link_libraries(composite_layer_test PRIVATE core-ui)
    endif()

    add_executable(msgbox_cancel_test EXCLUDE_FROM_ALL
        test/src/msgbox_cancel_test.cpp
    )
    target_compile_definitions(msgbox_cancel_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(msgbox_cancel_test PRIVATE src include)
    ui_core_mark_test(msgbox_cancel_test)
    if(TARGET core-ui-static)
        target_link_libraries(msgbox_cancel_test PRIVATE core-ui-static)
    else()
        target_link_libraries(msgbox_cancel_test PRIVATE core-ui)
    endif()

    add_executable(image_loading_frames_test EXCLUDE_FROM_ALL
        test/src/image_loading_frames_test.cpp
    )
    target_compile_definitions(image_loading_frames_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(image_loading_frames_test PRIVATE src include)
    ui_core_mark_test(image_loading_frames_test)
    if(TARGET core-ui-static)
        target_link_libraries(image_loading_frames_test PRIVATE core-ui-static)
    else()
        target_link_libraries(image_loading_frames_test PRIVATE core-ui)
    endif()

    add_executable(toggle_dblclick_test EXCLUDE_FROM_ALL
        test/src/toggle_dblclick_test.cpp
    )
    target_compile_definitions(toggle_dblclick_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(toggle_dblclick_test PRIVATE src include)
    ui_core_mark_test(toggle_dblclick_test)
    if(TARGET core-ui-static)
        target_link_libraries(toggle_dblclick_test PRIVATE core-ui-static)
    else()
        target_link_libraries(toggle_dblclick_test PRIVATE core-ui)
    endif()

    # build 310: @wheel 沿父链找最近的监听者, 正在滚动的 ScrollView 截停 (真窗口)。
    add_executable(wheel_bubble_test EXCLUDE_FROM_ALL
        test/src/wheel_bubble_test.cpp
    )
    target_compile_definitions(wheel_bubble_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(wheel_bubble_test PRIVATE src include)
    ui_core_mark_test(wheel_bubble_test)
    if(TARGET core-ui-static)
        target_link_libraries(wheel_bubble_test PRIVATE core-ui-static)
    else()
        target_link_libraries(wheel_bubble_test PRIVATE core-ui)
    endif()

    # build 312: 无边框不可缩放窗口 (弹窗) 在 Win11 显式请求圆角。
    add_executable(window_corner_test EXCLUDE_FROM_ALL
        test/src/window_corner_test.cpp
    )
    target_compile_definitions(window_corner_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(window_corner_test PRIVATE src)
    ui_core_mark_test(window_corner_test)

    # build 319: 分层窗口 (ULW) 帧双缓冲 —— 无边框随图缩放原子提交位置 + 尺寸 + 画面。
    add_executable(layered_frame_test EXCLUDE_FROM_ALL
        test/src/layered_frame_test.cpp
    )
    target_compile_definitions(layered_frame_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(layered_frame_test PRIVATE src include)
    ui_core_mark_test(layered_frame_test)
    if(TARGET core-ui-static)
        target_link_libraries(layered_frame_test PRIVATE core-ui-static)
    else()
        target_link_libraries(layered_frame_test PRIVATE core-ui)
    endif()

    # build 319: 分层画布窗口 (UiWindowConfig.layered_canvas) 端到端。
    add_executable(layered_window_test EXCLUDE_FROM_ALL
        test/src/layered_window_test.cpp
    )
    target_compile_definitions(layered_window_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(layered_window_test PRIVATE src include)
    ui_core_mark_test(layered_window_test)
    if(TARGET core-ui-static)
        target_link_libraries(layered_window_test PRIVATE core-ui-static)
    else()
        target_link_libraries(layered_window_test PRIVATE core-ui)
    endif()

    # build 316: 宿主 C++ 设的文字按 key 绑定, 换 locale 自动重译。
    add_executable(page_text_binding_test EXCLUDE_FROM_ALL
        test/src/page_text_binding_test.cpp
    )
    target_compile_definitions(page_text_binding_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(page_text_binding_test PRIVATE src include)
    ui_core_mark_test(page_text_binding_test)
    if(TARGET core-ui-static)
        target_link_libraries(page_text_binding_test PRIVATE core-ui-static)
    else()
        target_link_libraries(page_text_binding_test PRIVATE core-ui)
    endif()

    # build 314: 运行时换字体 / 排版区域对已打开窗口立即生效。
    add_executable(font_runtime_test EXCLUDE_FROM_ALL
        test/src/font_runtime_test.cpp
    )
    target_compile_definitions(font_runtime_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(font_runtime_test PRIVATE src include)
    ui_core_mark_test(font_runtime_test)
    if(TARGET core-ui-static)
        target_link_libraries(font_runtime_test PRIVATE core-ui-static dwrite)
    else()
        target_link_libraries(font_runtime_test PRIVATE core-ui dwrite)
    endif()

    # build 313: <select> 的 <option> 支持 v-for / {{ }} 插值。
    add_executable(select_options_test EXCLUDE_FROM_ALL
        test/src/select_options_test.cpp
    )
    target_compile_definitions(select_options_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(select_options_test PRIVATE src include)
    ui_core_mark_test(select_options_test)
    if(TARGET core-ui-static)
        target_link_libraries(select_options_test PRIVATE core-ui-static)
    else()
        target_link_libraries(select_options_test PRIVATE core-ui)
    endif()

    # build 311: ui_text_input_select_all (对标 input.select())。
    add_executable(text_input_select_all_test EXCLUDE_FROM_ALL
        test/src/text_input_select_all_test.cpp
    )
    target_compile_definitions(text_input_select_all_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(text_input_select_all_test PRIVATE src include)
    ui_core_mark_test(text_input_select_all_test)
    if(TARGET core-ui-static)
        target_link_libraries(text_input_select_all_test PRIVATE core-ui-static)
    else()
        target_link_libraries(text_input_select_all_test PRIVATE core-ui)
    endif()

    # build 340: TextInput 密码模式 (ui_text_input_set_password / password="true")。
    add_executable(text_input_password_test EXCLUDE_FROM_ALL
        test/src/text_input_password_test.cpp
    )
    target_compile_definitions(text_input_password_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(text_input_password_test PRIVATE src include)
    ui_core_mark_test(text_input_password_test)
    if(TARGET core-ui-static)
        target_link_libraries(text_input_password_test PRIVATE core-ui-static)
    else()
        target_link_libraries(text_input_password_test PRIVATE core-ui)
    endif()

    # build 333: 文本框撤销 / 重做 (TextUndoHistory + 三个文本控件)。
    add_executable(text_undo_test EXCLUDE_FROM_ALL
        test/src/text_undo_test.cpp
    )
    target_compile_definitions(text_undo_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(text_undo_test PRIVATE src include)
    ui_core_mark_test(text_undo_test)
    if(TARGET core-ui-static)
        target_link_libraries(text_undo_test PRIVATE core-ui-static)
    else()
        target_link_libraries(text_undo_test PRIVATE core-ui)
    endif()

    # build 306: 拖影显示被拖元素的可见文字 (不是 drag-data 载荷)。
    add_executable(drag_ghost_text_test EXCLUDE_FROM_ALL
        test/src/drag_ghost_text_test.cpp
    )
    target_compile_definitions(drag_ghost_text_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_include_directories(drag_ghost_text_test PRIVATE src)
    ui_core_mark_test(drag_ghost_text_test)
    if(TARGET core-ui-static)
        target_link_libraries(drag_ghost_text_test PRIVATE core-ui-static)
    else()
        target_link_libraries(drag_ghost_text_test PRIVATE core-ui)
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
