cmake_minimum_required(VERSION 3.16)
project(null_actuator_tests LANGUAGES CXX)

add_executable(null_actuator_tests
    test_null_actuator.cpp
    ../src/null_actuator_controller.cpp
)

target_include_directories(null_actuator_tests PRIVATE
    ../include
    ../../safety_supervisor/include
)
target_compile_features(null_actuator_tests PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(null_actuator_tests PRIVATE /W4 /WX)
else()
    target_compile_options(null_actuator_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_test(NAME null_actuator_tests COMMAND null_actuator_tests)

