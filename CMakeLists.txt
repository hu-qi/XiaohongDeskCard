cmake_minimum_required(VERSION 3.16)
project(XiaohongDeskCardCore LANGUAGES C)

add_library(desk_core STATIC
    firmware/core/src/desk_core.c
    firmware/core/src/desk_wire.c
    firmware/core/src/desk_packet.c
    firmware/core/src/desk_dispatcher.c)
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

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME desk_sim_http_tests
        COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests -p test_simulator.py
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(desk_sim_http_tests PROPERTIES
        ENVIRONMENT "DESK_SIM_LIB=$<TARGET_FILE:desk_simulator>")
endif()

if(Python3_Interpreter_FOUND)
    add_test(NAME harmony_stage_structure
        COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests -p test_harmony_structure.py
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endif()
