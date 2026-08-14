cmake_minimum_required(VERSION 3.16)
project(sensor_sht30_host_tests LANGUAGES CXX)

enable_testing()

add_library(sensor_sht30_domain STATIC
    ../src/sht3x_domain.cpp
)
target_include_directories(sensor_sht30_domain PUBLIC ../include)
target_compile_features(sensor_sht30_domain PUBLIC cxx_std_17)

add_executable(sensor_sht30_domain_test sht3x_domain_test.cpp)
target_link_libraries(sensor_sht30_domain_test PRIVATE sensor_sht30_domain)
target_compile_features(sensor_sht30_domain_test PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(sensor_sht30_domain PRIVATE /W4 /WX)
    target_compile_options(sensor_sht30_domain_test PRIVATE /W4 /WX)
else()
    target_compile_options(sensor_sht30_domain PRIVATE
        -Wall -Wextra -Wshadow -Wconversion -Wpedantic -Werror
    )
    target_compile_options(sensor_sht30_domain_test PRIVATE
        -Wall -Wextra -Wshadow -Wconversion -Wpedantic -Werror
    )
endif()

add_test(NAME sensor_sht30_domain_test COMMAND sensor_sht30_domain_test)
