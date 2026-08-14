cmake_minimum_required(VERSION 3.16)
project(sensor_ms5611_host_tests LANGUAGES CXX)

enable_testing()

add_library(sensor_ms5611_domain STATIC
    ../src/ms5611_domain.cpp
)
target_include_directories(sensor_ms5611_domain PUBLIC ../include)
target_compile_features(sensor_ms5611_domain PUBLIC cxx_std_17)

add_executable(sensor_ms5611_domain_test ms5611_domain_test.cpp)
target_link_libraries(sensor_ms5611_domain_test PRIVATE sensor_ms5611_domain)
target_compile_features(sensor_ms5611_domain_test PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(sensor_ms5611_domain PRIVATE /W4 /WX)
    target_compile_options(sensor_ms5611_domain_test PRIVATE /W4 /WX)
else()
    target_compile_options(sensor_ms5611_domain PRIVATE
        -Wall -Wextra -Wshadow -Wconversion -Wpedantic -Werror
    )
    target_compile_options(sensor_ms5611_domain_test PRIVATE
        -Wall -Wextra -Wshadow -Wconversion -Wpedantic -Werror
    )
endif()

add_test(NAME sensor_ms5611_domain_test COMMAND sensor_ms5611_domain_test)
