cmake_minimum_required(VERSION 3.20)
project(demute_imgui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The tools are REAPER DLLs built with the static MSVC runtime (/MT): a top-level build of the library does
# the same unless told otherwise. A tool that consumes it sets its own (RAW, RAV: before FetchContent).
if(MSVC AND NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Tests run with Dear ImGui's assertions on (IM_ASSERT is assert(): off under NDEBUG), so a misuse of
# the API fails the headless test. A top-level build with no build type is therefore a Debug build.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES AND CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

option(DEMUTE_IMGUI_BUILD_TESTS "Build the demute-imgui tests" ${PROJECT_IS_TOP_LEVEL})
if(DEMUTE_IMGUI_BUILD_TESTS)
    include(CTest)
endif()
include(FetchContent)

# Dear ImGui, pinned: the tools no longer fetch it themselves. Dear ImGui has no CMakeLists of its
# own, so the core is compiled here. No backend (Win32, OpenGL...) belongs to the `theme` target.
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.9b
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(imgui)

add_library(demute_imgui_core STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp")
target_include_directories(demute_imgui_core SYSTEM PUBLIC "${imgui_SOURCE_DIR}")

# theme: colours, fonts roles, widgets, adaptive layout, icons. No REAPER, no windows.h.
add_library(theme STATIC
    src/ui_theme.cpp
    src/ui_flow.cpp
    src/ui_flow_math.cpp
    src/ui_fold_all.cpp
    src/library_icons.cpp)
target_include_directories(theme PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(theme PUBLIC demute_imgui_core)
target_compile_options(theme PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
add_library(demute_imgui::theme ALIAS theme)

# host (Windows only): the docked panel, the floating tool windows, the GL and ImGui contexts of a window.
# REAPER is reached only through the services the tool injects (src/host/host_services.h). Not configured
# on other systems (its pure parts are tested there by tests/host_pure_test.cpp).
if(WIN32)
    # Dear ImGui's Win32 and OpenGL3 backends, compiled as they come (none of the host's warning options).
    add_library(demute_imgui_backends STATIC
        "${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp"
        "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp")
    target_include_directories(demute_imgui_backends SYSTEM PUBLIC "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends")
    target_link_libraries(demute_imgui_backends PUBLIC demute_imgui_core opengl32 gdi32 user32 dwmapi imm32)

    add_library(host STATIC
        src/host/host_services.cpp
        src/host/gl_context.cpp
        src/host/ui_fonts.cpp
        src/host/docked_panel.cpp
        src/host/float_ui_window.cpp)
    target_include_directories(host PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/host")
    target_link_libraries(host PUBLIC theme demute_imgui_backends opengl32 gdi32 user32 dwmapi imm32)
    target_compile_definitions(host PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    target_compile_options(host PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wextra>)
    add_library(demute_imgui::host ALIAS host)
endif()

if(DEMUTE_IMGUI_BUILD_TESTS)
    add_subdirectory(tests)
endif()
