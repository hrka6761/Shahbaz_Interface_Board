idf_component_register(
    SRCS
        "src/device_frame_sender.cpp"
        "src/sensor_telemetry_publisher.cpp"
        "src/protocol_engine.cpp"
    INCLUDE_DIRS "include"
    REQUIRES interfaces domain telemetry_protocol command_dispatcher safety_supervisor sensor_scheduler
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wshadow -Wconversion -Wpedantic -Werror)
