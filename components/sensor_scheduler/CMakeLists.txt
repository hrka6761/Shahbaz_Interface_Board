idf_component_register(
    SRCS "src/sensor_scheduler.cpp"
    INCLUDE_DIRS "include"
    REQUIRES interfaces domain sensor_sht30 sensor_ms5611
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wshadow -Wconversion -Wpedantic -Werror
    )
elseif(MSVC)
    target_compile_options(${COMPONENT_LIB} PRIVATE /W4 /WX)
endif()
