idf_component_register(
    SRCS "src/espidf_usb_cdc_transport.cpp"
    INCLUDE_DIRS "include"
    REQUIRES interfaces telemetry_protocol esp_tinyusb freertos
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wshadow -Wconversion -Wpedantic -Werror)

# This component is the sole C++ boundary around ESP-IDF/FreeRTOS/TinyUSB C
# headers. GCC emits an unclassified pedantic diagnostic for ESP-IDF's
# intentional #include_next even inside a diagnostic pragma, so disable only
# ISO-pedantic diagnostics for this one adapter translation unit. All semantic
# warning classes above remain errors, and every other project component keeps
# -Wpedantic -Werror.
set_source_files_properties(
    src/espidf_usb_cdc_transport.cpp
    PROPERTIES COMPILE_OPTIONS "-Wno-pedantic"
)
