cmake_minimum_required(VERSION 3.16)
project(XiaohongDeskCardRealLVGLTest LANGUAGES C)

if(NOT DEFINED DESK_LVGL_SOURCE_DIR OR NOT EXISTS "${DESK_LVGL_SOURCE_DIR}/lvgl.h")
    message(FATAL_ERROR
      "Provide -DDESK_LVGL_SOURCE_DIR=/path/to/lvgl-v8-or-v9 source")
endif()

# Actual LVGL source is built as a proper static C library; C compiler and
# headers here are real, no fake lvgl.h or stub implementation.
set(LV_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h")
set(LV_BUILD_CONF_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${DESK_LVGL_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/vendor_lvgl")
# The LVGL v8 custom.cmake normally locates lv_conf.h relative to its tree.
# The explicit include also supports the nested GitHub Actions checkout path.
target_include_directories(lvgl PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
# LVGL 9.3 upstream glob includes ARM Helium *.S files even for x86_64.
# They are unavailable on the GitHub Linux host and are disabled by
# LVGL's software draw config anyway. Exclude machine-specific assembly
# from this *host-only* compatibility test, not from WS63 firmware.
get_target_property(_desk_lvgl_sources lvgl SOURCES)
list(FILTER _desk_lvgl_sources EXCLUDE REGEX "\\.S$")
set_property(TARGET lvgl PROPERTY SOURCES "${_desk_lvgl_sources}")

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
