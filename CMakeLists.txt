cmake_minimum_required(VERSION 3.16)
project(XiaohongDeskCardCore LANGUAGES C)

add_library(desk_core STATIC
    firmware/core/src/desk_core.c
    firmware/core/src/desk_wire.c)
target_include_directories(desk_core PUBLIC firmware/core/include)
set_target_properties(desk_core PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(desk_core PRIVATE -Wall -Wextra -Werror -Wpedantic)
endif()

enable_testing()
add_executable(test_core tests/test_core.c)
target_link_libraries(test_core PRIVATE desk_core)
add_test(NAME desk_core_tests COMMAND test_core)
