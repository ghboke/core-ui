cmake_minimum_required(VERSION 3.20)

set(UI_CORE_VERSION "1.0.0")
set(UI_CORE_INTERNAL_VERSION "1")
project(ui-core-project VERSION ${UI_CORE_VERSION} LANGUAGES CXX)

set(UI_CORE_VERSION_TAG "v${PROJECT_VERSION}")
set(UI_CORE_RELEASE_DIR_NAME "ui-core-${UI_CORE_VERSION_TAG}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(UI_CORE_STATIC "Build ui-core as static library (single exe, no DLL)" OFF)

# MinGW 默认会把 libstdc++/libgcc 作为动态依赖输出，导致目标机缺少 DLL。
# 这里改为静态链接运行时，便于仅分发 libui-core.dll + ui-demo.exe。
if(MINGW)
    add_link_options(-static-libstdc++ -static-libgcc -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic)
endif()

# ---- UI Core sources ----
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
    # ---- Static library: ui-core.lib (linked into exe) ----
    add_library(ui-core STATIC ${UI_CORE_SOURCES})
    target_compile_definitions(ui-core
        PUBLIC  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX UI_CORE_STATIC
        PRIVATE UI_CORE_BUILDING
    )
    target_link_libraries(ui-core PUBLIC ${UI_CORE_SYSTEM_LIBS})
else()
    # ---- Shared library: ui-core.dll ----
    add_library(ui-core SHARED ${UI_CORE_SOURCES})
    set_target_properties(ui-core PROPERTIES PREFIX "")  # MinGW: ui-core.dll not libui-core.dll
    target_compile_definitions(ui-core
        PUBLIC  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
        PRIVATE UI_CORE_BUILDING
    )
    target_link_libraries(ui-core PRIVATE ${UI_CORE_SYSTEM_LIBS})
endif()

target_include_directories(ui-core
    PUBLIC  include
    PRIVATE src/ui
)

# ---- Demo application: ui-demo.exe ----
add_executable(ui-demo WIN32
    demo/app.cpp
)
target_link_libraries(ui-demo PRIVATE ui-core)
target_include_directories(ui-demo PRIVATE src/ui include)

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
endif()

target_compile_definitions(ui-core
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
            ui-core
            "ui-core static library"
            "ui-core.lib"
            "ui-core.lib"
            "VFT_STATIC_LIB"
        )
    else()
        ui_core_add_version_resource(
            ui-core
            "ui-core dynamic library"
            "ui-core.dll"
            "ui-core.dll"
            "VFT_DLL"
        )
    endif()

    ui_core_add_version_resource(
        ui-demo
        "ui-core demo application"
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
        TARGETS ui-core
        ARCHIVE DESTINATION "${UI_CORE_RELEASE_DIR_NAME}/lib/static"
    )
else()
    install(
        TARGETS ui-core
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
