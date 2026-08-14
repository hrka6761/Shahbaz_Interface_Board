cmake_minimum_required(VERSION 3.16)
project(safety_supervisor_tests LANGUAGES CXX)

add_executable(safety_supervisor_tests
    test_safety_supervisor.cpp
    ../src/safety_supervisor.cpp
)

target_include_directories(safety_supervisor_tests PRIVATE ../include)
target_compile_features(safety_supervisor_tests PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(safety_supervisor_tests PRIVATE /W4 /WX)
else()
    target_compile_options(safety_supervisor_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_test(NAME safety_supervisor_tests COMMAND safety_supervisor_tests)

