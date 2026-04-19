cmake_minimum_required(VERSION 3.20)

set(UI_CORE_VERSION "1.3.0")
set(UI_CORE_INTERNAL_VERSION "4")
project(core-ui-project VERSION ${UI_CORE_VERSION} LANGUAGES CXX)

set(UI_CORE_VERSION_TAG "v${PROJECT_VERSION}")
set(UI_CORE_RELEASE_DIR_NAME "core-ui-${UI_CORE_VERSION_TAG}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(UI_CORE_STATIC "Build core-ui as static library (single exe, no DLL)" OFF)

# MinGW 默认会把 libstdc++/libgcc 作为动态依赖输出，导致目标机缺少 DLL。
# 这里改为静态链接运行时，便于仅分发 libcore-ui.dll + ui-demo.exe。
if(MINGW)
    add_link_options(-static-libstdc++ -static-libgcc -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic)
endif()

# ---- Core UI sources ----
set(UI_CORE_SOURCES
    src/ui/renderer.cpp
    src/ui/widget.cpp
    src/ui/controls.cpp
    src/ui/handle_table.cpp
    src/ui/context_menu.cpp
    src/ui/ui_window.cpp
    src/ui/ui_context.cpp
    src/ui/ui_api.cpp
    src/ui/ui_debug.cpp
    src/ui/animation.cpp
    src/ui/markup/markup_parser.cpp
    src/ui/markup/markup_builder.cpp
    src/ui/markup/markup.cpp
)

set(UI_CORE_SYSTEM_LIBS d2d1 d3d11 dxgi dxguid dwrite dwmapi uxtheme windowscodecs shlwapi)

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

if(UI_CORE_STATIC)
    # ---- Static library: core-ui.lib (linked into exe) ----
    add_library(core-ui STATIC ${UI_CORE_SOURCES})
    target_compile_definitions(core-ui
        PUBLIC  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX UI_CORE_STATIC
        PRIVATE UI_CORE_BUILDING
    )
    target_link_libraries(core-ui PUBLIC ${UI_CORE_SYSTEM_LIBS})
else()
    # ---- Shared library: core-ui.dll ----
    add_library(core-ui SHARED ${UI_CORE_SOURCES})
    set_target_properties(core-ui PROPERTIES PREFIX "")  # MinGW: core-ui.dll not libcore-ui.dll
    target_compile_definitions(core-ui
        PUBLIC  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
        PRIVATE UI_CORE_BUILDING
    )
    target_link_libraries(core-ui PRIVATE ${UI_CORE_SYSTEM_LIBS})
endif()

target_include_directories(core-ui
    PUBLIC  include
    PRIVATE src/ui
)

# ---- Demo application: ui-demo.exe ----
add_executable(ui-demo WIN32
    demo/app.cpp
)
target_link_libraries(ui-demo PRIVATE core-ui)
target_include_directories(ui-demo PRIVATE src/ui include)

# ---- Font demo: font-demo.exe ----
# 文字渲染模式 A/B/C/D 对比（内部低层 Renderer，用于开发对比）
add_executable(font-demo WIN32
    demo/font_demo.cpp
)
target_link_libraries(font-demo PRIVATE core-ui)
target_include_directories(font-demo PRIVATE include src/ui)

# ---- Font API demo: font-api-demo.exe ----
# 1.3.0 公开字体 / 渲染模式 C API 演示
add_executable(font-api-demo WIN32
    demo/font_api_demo.cpp
)
target_link_libraries(font-api-demo PRIVATE core-ui)
target_include_directories(font-api-demo PRIVATE include)

# POST_BUILD: 把 MinGW 运行时 DLL 拷到 exe 同目录。虽然顶上已经用
# -static-libstdc++/-static-libgcc + -Bstatic lwinpthread 试图静态化，
# 但 MinGW 的 C++ 线程头实际会通过其它路径引入 libwinpthread-1.dll 动态依赖，
# 稳妥的办法就是直接带上。
if(MINGW AND NOT UI_CORE_STATIC)
    get_filename_component(MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    add_custom_command(TARGET ui-demo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MINGW_BIN}/libwinpthread-1.dll"
            "$<TARGET_FILE_DIR:ui-demo>/libwinpthread-1.dll"
    )
    add_custom_command(TARGET font-demo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MINGW_BIN}/libwinpthread-1.dll"
            "$<TARGET_FILE_DIR:font-demo>/libwinpthread-1.dll"
    )
    add_custom_command(TARGET font-api-demo POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MINGW_BIN}/libwinpthread-1.dll"
            "$<TARGET_FILE_DIR:font-api-demo>/libwinpthread-1.dll"
    )
endif()

target_compile_definitions(core-ui
    PUBLIC
        UI_CORE_VERSION_TAG="${UI_CORE_VERSION_TAG}"
        UI_CORE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        UI_CORE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        UI_CORE_VERSION_PATCH=${PROJECT_VERSION_PATCH}
        UI_CORE_VERSION_BUILD=${UI_CORE_INTERNAL_VERSION}
        UI_CORE_INTERNAL_VERSION=${UI_CORE_INTERNAL_VERSION}
)

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

    ui_core_add_version_resource(
        ui-demo
        "Core UI demo application"
        "ui-demo.exe"
        "ui-demo.exe"
        "VFT_APP"
    )
endif()

install(
    DIRECTORY include/
    DESTINATION "${UI_CORE_RELEASE_DIR_NAME}/include"
    FILES_MATCHING
    PATTERN "*.h"
)

install(
    FILES UI_CORE_API.md
    DESTINATION "${UI_CORE_RELEASE_DIR_NAME}"
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
endif()

install(
    TARGETS ui-demo
    RUNTIME DESTINATION "${UI_CORE_RELEASE_DIR_NAME}/demo"
)

add_custom_target(release-package
    COMMAND ${CMAKE_COMMAND} --install "${CMAKE_BINARY_DIR}" --prefix "${CMAKE_SOURCE_DIR}/release"
    COMMENT "Create release package in release/${UI_CORE_RELEASE_DIR_NAME}"
)
