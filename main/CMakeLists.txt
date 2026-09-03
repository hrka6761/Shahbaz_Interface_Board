set(SHAHBAZ_ACTUATOR_COMPONENTS actuator_null)
if(SHAHBAZ_ACTUATOR_BACKEND STREQUAL "espidf")
    list(APPEND SHAHBAZ_ACTUATOR_COMPONENTS actuator_espidf)
endif()

idf_component_register(
    SRCS "app_main.cpp"
    REQUIRES
        ${SHAHBAZ_ACTUATOR_COMPONENTS}
        board_support
        command_dispatcher
        device_link
        health_monitor
        interfaces
        platform_espidf
        safety_supervisor
        sensor_scheduler
        sensor_vl53l0x
        usb_transport_espidf
        esp_system
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wshadow -Wconversion -Wpedantic -Werror)
if(SHAHBAZ_ACTUATOR_BACKEND STREQUAL "espidf")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE SHAHBAZ_BUILD_PHYSICAL_ACTUATORS=1)
endif()

# The composition root includes ESP-IDF C headers that intentionally use #include_next.
set_source_files_properties(
    app_main.cpp
    PROPERTIES COMPILE_OPTIONS "-Wno-pedantic"
)
