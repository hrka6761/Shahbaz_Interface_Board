cmake_minimum_required(VERSION 3.16)
project(sensor_scheduler_host_tests LANGUAGES CXX)

enable_testing()

add_library(sensor_scheduler_host STATIC
    ../src/sensor_scheduler.cpp
    ../../sensor_sht30/src/sht3x_domain.cpp
    ../../sensor_ms5611/src/ms5611_domain.cpp
    ../../sensor_vl53l0x/src/vl53l0x_array.cpp
    ../../sensor_vl53l0x/src/vl53l0x_domain.cpp
)
target_include_directories(sensor_scheduler_host PUBLIC
    ../include
    ../../interfaces/include
    ../../domain/include
    ../../sensor_sht30/include
    ../../sensor_ms5611/include
    ../../sensor_vl53l0x/include
)
target_compile_features(sensor_scheduler_host PUBLIC cxx_std_17)

add_executable(sensor_scheduler_test sensor_scheduler_test.cpp)
target_link_libraries(sensor_scheduler_test PRIVATE sensor_scheduler_host)
target_compile_features(sensor_scheduler_test PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(sensor_scheduler_host PRIVATE /W4 /WX)
    target_compile_options(sensor_scheduler_test PRIVATE /W4 /WX)
else()
    target_compile_options(sensor_scheduler_host PRIVATE
        -Wall -Wextra -Wshadow -Wconversion -Wpedantic -Werror
    )
    target_compile_options(sensor_scheduler_test PRIVATE
        -Wall -Wextra -Wshadow -Wconversion -Wpedantic -Werror
    )
endif()

add_test(NAME sensor_scheduler_test COMMAND sensor_scheduler_test)
