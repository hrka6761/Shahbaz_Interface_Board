# ESP-IDF-only adapters. Domain and interface components remain platform-free.
idf_component_register(
    SRCS
        "src/espidf_board_validator.cpp"
        "src/espidf_monotonic_clock.cpp"
        "src/espidf_i2c_bus.cpp"
    INCLUDE_DIRS "include"
    REQUIRES board_support interfaces esp_timer esp_psram spi_flash esp_driver_i2c esp_driver_gpio esp_rom
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wall -Wextra -Wshadow -Wconversion -Wpedantic -Werror
)

# These adapters include ESP-IDF C headers that intentionally use #include_next.
set_source_files_properties(
    src/espidf_board_validator.cpp
    src/espidf_monotonic_clock.cpp
    src/espidf_i2c_bus.cpp
    PROPERTIES COMPILE_OPTIONS "-Wno-pedantic"
)
