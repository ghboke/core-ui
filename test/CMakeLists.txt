cmake_minimum_required(VERSION 3.20)
project(bitmap-memory-test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MINGW)
    add_link_options(-static-libstdc++ -static-libgcc -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic)
endif()

add_executable(bitmap_mem_test WIN32
    src/bitmap_memory_test.cpp
)

target_compile_definitions(bitmap_mem_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(bitmap_mem_test PRIVATE d2d1 d3d11 dxgi dwrite windowscodecs ole32 psapi)

add_executable(wic_load_test WIN32
    src/wic_load_test.cpp
)
target_compile_definitions(wic_load_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(wic_load_test PRIVATE d2d1 d3d11 dxgi windowscodecs ole32 psapi)

add_executable(d2d_wic_direct_test WIN32
    src/d2d_wic_direct_test.cpp
)
target_compile_definitions(d2d_wic_direct_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(d2d_wic_direct_test PRIVATE d2d1 d3d11 dxgi windowscodecs ole32 psapi)

add_executable(heap_optimize_test WIN32
    src/heap_optimize_test.cpp
)
target_compile_definitions(heap_optimize_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(heap_optimize_test PRIVATE d2d1 d3d11 dxgi windowscodecs ole32 psapi)

# Layout engine test (console app, no D2D needed)
add_executable(layout_test
    src/layout_test.cpp
    ../src/ui/widget.cpp
)
target_compile_definitions(layout_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(layout_test PRIVATE ../src)

# Markup engine test — links against the full static library built from parent CMakeLists
# Build from the root project with -DUI_CORE_STATIC=ON first, then run this test.
add_executable(markup_test
    src/markup_test.cpp
)
target_compile_definitions(markup_test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX UI_CORE_STATIC)
target_include_directories(markup_test PRIVATE ../src ../include)
# Link the pre-built static lib
target_link_libraries(markup_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../build-test/libcore-ui.a
    d2d1 d3d11 dxgi dwrite dwmapi uxtheme windowscodecs shlwapi ole32
)
