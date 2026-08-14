cmake_minimum_required(VERSION 3.16)
project(command_dispatcher_tests LANGUAGES CXX)

add_executable(command_dispatcher_tests
    test_command_dispatcher.cpp
    ../src/command_dispatcher.cpp
    ../../safety_supervisor/src/safety_supervisor.cpp
    ../../telemetry_protocol/src/cobs.cpp
    ../../telemetry_protocol/src/crc32c.cpp
    ../../telemetry_protocol/src/wire_protocol.cpp
)

target_include_directories(command_dispatcher_tests PRIVATE
    ../include
    ../../safety_supervisor/include
    ../../telemetry_protocol/include
)
target_compile_features(command_dispatcher_tests PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(command_dispatcher_tests PRIVATE /W4 /WX)
else()
    target_compile_options(command_dispatcher_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_test(NAME command_dispatcher_tests COMMAND command_dispatcher_tests)

