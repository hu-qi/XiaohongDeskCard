cmake_minimum_required(VERSION 3.16)
project(XiaohongDeskCardCore LANGUAGES C)

add_library(desk_core STATIC
    firmware/core/src/desk_core.c
    firmware/core/src/desk_wire.c
    firmware/core/src/desk_packet.c
    firmware/core/src/desk_dispatcher.c
    firmware/core/src/desk_radio_ingress.c
    firmware/core/src/desk_nv_store.c
    firmware/core/src/desk_http_json.c
    firmware/core/src/desk_wifi_ingress.c)
target_include_directories(desk_core PUBLIC firmware/core/include)
set_target_properties(desk_core PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED YES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(desk_core PRIVATE -Wall -Wextra -Werror -Wpedantic)
endif()

# Host desktop simulator: shares the same C99 Desk Core as the eventual WS63 port.
add_library(desk_simulator SHARED tools/simulator/desk_sim_bridge.c)
target_link_libraries(desk_simulator PRIVATE desk_core)
set_target_properties(desk_simulator PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(desk_simulator PRIVATE -Wall -Wextra -Werror -Wpedantic)
endif()

enable_testing()
add_executable(test_core tests/test_core.c)
target_link_libraries(test_core PRIVATE desk_core)
add_test(NAME desk_core_tests COMMAND test_core)

add_executable(test_packet tests/test_packet.c)
target_link_libraries(test_packet PRIVATE desk_core)
add_test(NAME desk_packet_tests COMMAND test_packet)

add_executable(test_dispatcher tests/test_dispatcher.c)
target_link_libraries(test_dispatcher PRIVATE desk_core)
add_test(NAME desk_dispatcher_tests COMMAND test_dispatcher)

add_executable(test_radio_ingress tests/test_radio_ingress.c)
target_link_libraries(test_radio_ingress PRIVATE desk_core)
add_test(NAME desk_radio_ingress_tests COMMAND test_radio_ingress)

add_executable(test_http_json tests/test_http_json.c)
target_link_libraries(test_http_json PRIVATE desk_core)
add_test(NAME desk_http_json_tests COMMAND test_http_json)

add_executable(test_http_json_fuzz tests/test_http_json_fuzz.c)
target_link_libraries(test_http_json_fuzz PRIVATE desk_core)
add_test(NAME desk_http_json_fuzz_tests COMMAND test_http_json_fuzz)

add_executable(test_wifi_ingress tests/test_wifi_ingress.c)
target_link_libraries(test_wifi_ingress PRIVATE desk_core)
add_test(NAME desk_wifi_ingress_tests COMMAND test_wifi_ingress)

add_executable(test_nv_store tests/test_nv_store.c)
target_link_libraries(test_nv_store PRIVATE desk_core)
add_test(NAME desk_nv_store_tests COMMAND test_nv_store)

add_executable(test_packet_fuzz tests/test_packet_fuzz.c)
target_link_libraries(test_packet_fuzz PRIVATE desk_core)
add_test(NAME desk_packet_fuzz_tests COMMAND test_packet_fuzz)

# LVGL 8 API surface is compiled against a deliberately small HOST stub.
# This tests view logic/ownership but NOT the upstream LVGL or actual ST7789.
add_library(desk_view_host STATIC
    firmware/display/desk_view_lvgl.c
    tests/lvgl_stub/lvgl_stub.c)
target_include_directories(desk_view_host PUBLIC
    firmware/display
    tests/lvgl_stub)
target_link_libraries(desk_view_host PUBLIC desk_core)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(desk_view_host PRIVATE -Wall -Wextra -Werror -Wpedantic)
endif()
add_executable(test_lvgl_view tests/test_lvgl_view.c)
target_link_libraries(test_lvgl_view PRIVATE desk_view_host)
add_test(NAME desk_lvgl_view_host_tests COMMAND test_lvgl_view)

# SDK-independent adapter for the upstream Xiaohong LvglTask + g_disp_event_qid.
# Host fake NV/radio callback test only; not a WS63 SDK compiler build.
add_library(desk_ws63_glue_host STATIC firmware/platform/ws63/desk_ws63_glue.c)
target_include_directories(desk_ws63_glue_host PUBLIC firmware/platform/ws63)
target_link_libraries(desk_ws63_glue_host PUBLIC desk_view_host)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(desk_ws63_glue_host PRIVATE -Wall -Wextra -Werror -Wpedantic)
endif()
add_executable(test_ws63_glue tests/test_ws63_glue.c)
target_link_libraries(test_ws63_glue PRIVATE desk_ws63_glue_host)
add_test(NAME desk_ws63_glue_host_tests COMMAND test_ws63_glue)

# Full host integration: Flash two-slot journal -> WS63 glue -> LVGL + business ACK.
add_executable(test_nv_glue tests/test_nv_glue.c)
target_link_libraries(test_nv_glue PRIVATE desk_ws63_glue_host)
add_test(NAME desk_nv_glue_host_tests COMMAND test_nv_glue)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME desk_sim_http_tests
        COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests -p test_simulator.py
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(desk_sim_http_tests PROPERTIES
        ENVIRONMENT "DESK_SIM_LIB=$<TARGET_FILE:desk_simulator>")
endif()

if(Python3_Interpreter_FOUND)
    add_test(NAME ws63_source_preflight_tests
        COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests -p test_ws63_preflight.py
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endif()

if(Python3_Interpreter_FOUND)
    add_test(NAME ws63_overlay_stage_tests
        COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests -p test_ws63_overlay.py
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endif()

if(Python3_Interpreter_FOUND)
    add_test(NAME harmony_stage_structure
        COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests -p test_harmony_structure.py
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endif()
