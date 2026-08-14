idf_component_register(
    SRCS "app_main.cpp"
    REQUIRES
        actuator_null
        board_support
        command_dispatcher
        device_link
        health_monitor
        interfaces
        platform_espidf
        safety_supervisor
        sensor_scheduler
        usb_transport_espidf
        esp_system
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wshadow -Wconversion -Wpedantic -Werror)
