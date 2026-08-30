idf_component_register(
    SRCS "src/espidf_pwm_actuator_controller.cpp"
    INCLUDE_DIRS "include"
    REQUIRES safety_supervisor board_support esp_driver_ledc
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(
    ${COMPONENT_LIB}
    PRIVATE -Wall -Wextra -Wshadow -Wconversion -Wpedantic -Werror
)

# ESP-IDF C headers use GCC's intentional #include_next extension.
set_source_files_properties(
    src/espidf_pwm_actuator_controller.cpp
    PROPERTIES COMPILE_OPTIONS "-Wno-pedantic"
)
