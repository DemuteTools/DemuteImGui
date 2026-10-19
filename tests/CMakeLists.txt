# Host tests: no window, no backend (a failing test never shows anything on screen).
set(SRC "${CMAKE_CURRENT_SOURCE_DIR}/../src")

add_executable(ui_drag_number_test ui_drag_number_test.cpp)
target_include_directories(ui_drag_number_test PRIVATE ${SRC})
add_test(NAME ui_drag_number COMMAND ui_drag_number_test)

add_executable(ui_flow_test ui_flow_test.cpp ../src/ui_flow_math.cpp)
target_include_directories(ui_flow_test PRIVATE ${SRC})
add_test(NAME ui_flow COMMAND ui_flow_test)

add_executable(ui_glyphs_test ui_glyphs_test.cpp)
target_include_directories(ui_glyphs_test PRIVATE ${SRC})
target_compile_definitions(ui_glyphs_test PRIVATE RAV_SRC_DIR="${SRC}")
add_test(NAME ui_glyphs COMMAND ui_glyphs_test)

# Headless frame: ImGui context with no backend, theme applied, every widget and icon drawn.
add_executable(theme_headless_test theme_headless_test.cpp)
target_link_libraries(theme_headless_test PRIVATE theme)
add_test(NAME theme_headless COMMAND theme_headless_test)
