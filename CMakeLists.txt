cmake_minimum_required(VERSION 3.20)
project(demute_imgui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

if(DEMUTE_IMGUI_BUILD_TESTS)
    add_subdirectory(tests)
endif()
