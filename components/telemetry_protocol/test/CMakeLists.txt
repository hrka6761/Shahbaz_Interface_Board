cmake_minimum_required(VERSION 3.16)
project(telemetry_protocol_tests LANGUAGES CXX)

add_executable(telemetry_protocol_tests
    test_telemetry_protocol.cpp
    ../src/cobs.cpp
    ../src/crc32c.cpp
    ../src/frame_accumulator.cpp
    ../src/wire_protocol.cpp
)

target_include_directories(telemetry_protocol_tests PRIVATE ../include)
target_compile_features(telemetry_protocol_tests PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(telemetry_protocol_tests PRIVATE /W4 /WX)
else()
    target_compile_options(telemetry_protocol_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

enable_testing()
add_test(NAME telemetry_protocol_tests COMMAND telemetry_protocol_tests)

