cmake_minimum_required(VERSION 3.16)
project(XiaohongDeskCardCore LANGUAGES C)

add_library(desk_core STATIC
    firmware/core/src/desk_core.c
    firmware/core/src/desk_wire.c)
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

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME desk_sim_http_tests
        COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests -p test_simulator.py
        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    set_tests_properties(desk_sim_http_tests PROPERTIES
        ENVIRONMENT "DESK_SIM_LIB=$<TARGET_FILE:desk_simulator>")
endif()
