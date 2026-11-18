cmake_minimum_required(VERSION 3.16)
project(XiaohongDeskCardRealLVGLTest LANGUAGES C)

if(NOT DEFINED DESK_LVGL_SOURCE_DIR OR NOT EXISTS "${DESK_LVGL_SOURCE_DIR}/lvgl.h")
    message(FATAL_ERROR
      "Provide -DDESK_LVGL_SOURCE_DIR=/path/to/lvgl-v8.3.11 source")
endif()

# Actual LVGL source is built as a proper static C library; C compiler and
# headers here are real, no fake lvgl.h or stub implementation.
set(LV_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h")
add_subdirectory("${DESK_LVGL_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/vendor_lvgl")

add_executable(test_lvgl_real
    test_lvgl_real.c
    ../../firmware/display/desk_view_lvgl.c
    ../../firmware/core/src/desk_core.c)
target_include_directories(test_lvgl_real PRIVATE
    ../../firmware/display
    ../../firmware/core/include)
target_link_libraries(test_lvgl_real PRIVATE lvgl)
set_target_properties(test_lvgl_real PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED YES)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_lvgl_real PRIVATE -Wall -Wextra -Werror -Wpedantic)
endif()

enable_testing()
add_test(NAME desk_lvgl_real_v8_tests COMMAND test_lvgl_real)
