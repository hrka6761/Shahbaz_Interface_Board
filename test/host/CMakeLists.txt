# Aggregate hardware-independent host test build. No ESP-IDF, FreeRTOS, USB
# device, or physical sensor is required.
cmake_minimum_required(VERSION 3.16)
project(shahbaz_host_tests LANGUAGES CXX)

enable_testing()

set(SHAHBAZ_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

function(shahbaz_test target)
    add_executable(${target} ${ARGN})
    target_compile_features(${target} PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wshadow -Wconversion -Wpedantic -Werror
        )
    endif()
    add_test(NAME ${target} COMMAND ${target})
endfunction()

shahbaz_test(usb_transport_test
    "${SHAHBAZ_ROOT}/components/usb_transport_espidf/test/usb_transport_test.cpp"
    "${SHAHBAZ_ROOT}/components/usb_transport_espidf/src/espidf_usb_cdc_transport.cpp"
    "${SHAHBAZ_ROOT}/components/telemetry_protocol/src/cobs.cpp"
    "${SHAHBAZ_ROOT}/components/telemetry_protocol/src/crc32c.cpp"
    "${SHAHBAZ_ROOT}/components/telemetry_protocol/src/frame_accumulator.cpp"
    "${SHAHBAZ_ROOT}/components/telemetry_protocol/src/wire_protocol.cpp"
)
target_include_directories(usb_transport_test PRIVATE
    "${SHAHBAZ_ROOT}/components/usb_transport_espidf/test/fake_idf"
    "${SHAHBAZ_ROOT}/components/usb_transport_espidf/include"
    "${SHAHBAZ_ROOT}/components/interfaces/include"
    "${SHAHBAZ_ROOT}/components/telemetry_protocol/include"
)

shahbaz_test(domain_measurement_test
    "${SHAHBAZ_ROOT}/components/domain/test/measurement_test.cpp"
)
target_include_directories(domain_measurement_test PRIVATE
    "${SHAHBAZ_ROOT}/components/domain/include"
)

shahbaz_test(board_profile_test
    "${SHAHBAZ_ROOT}/components/board_support/test/board_profile_test.cpp"
)
target_include_directories(board_profile_test PRIVATE
    "${SHAHBAZ_ROOT}/components/board_support/include"
)

shahbaz_test(espidf_i2c_error_mapping_test
    "${SHAHBAZ_ROOT}/components/platform_espidf/test/espidf_i2c_error_mapping_test.cpp"
)
target_include_directories(espidf_i2c_error_mapping_test PRIVATE
    "${SHAHBAZ_ROOT}/components/platform_espidf/test/fake_idf"
    "${SHAHBAZ_ROOT}/components/platform_espidf/src"
    "${SHAHBAZ_ROOT}/components/interfaces/include"
)

shahbaz_test(fixed_priority_queue_test
    "${SHAHBAZ_ROOT}/components/application/test/fixed_priority_queue_test.cpp"
)
target_include_directories(fixed_priority_queue_test PRIVATE
    "${SHAHBAZ_ROOT}/components/application/include"
)

shahbaz_test(task_health_monitor_test
    "${SHAHBAZ_ROOT}/components/health_monitor/test/task_health_monitor_test.cpp"
    "${SHAHBAZ_ROOT}/components/health_monitor/src/task_health_monitor.cpp"
)
target_include_directories(task_health_monitor_test PRIVATE
    "${SHAHBAZ_ROOT}/components/health_monitor/include"
)

shahbaz_test(sensor_sht30_domain_test
    "${SHAHBAZ_ROOT}/components/sensor_sht30/test/sht3x_domain_test.cpp"
    "${SHAHBAZ_ROOT}/components/sensor_sht30/src/sht3x_domain.cpp"
)
target_include_directories(sensor_sht30_domain_test PRIVATE
    "${SHAHBAZ_ROOT}/components/sensor_sht30/include"
)

shahbaz_test(sensor_ms5611_domain_test
    "${SHAHBAZ_ROOT}/components/sensor_ms5611/test/ms5611_domain_test.cpp"
    "${SHAHBAZ_ROOT}/components/sensor_ms5611/src/ms5611_domain.cpp"
)
target_include_directories(sensor_ms5611_domain_test PRIVATE
    "${SHAHBAZ_ROOT}/components/sensor_ms5611/include"
)

shahbaz_test(sensor_vl53l0x_domain_test
    "${SHAHBAZ_ROOT}/components/sensor_vl53l0x/test/vl53l0x_domain_test.cpp"
    "${SHAHBAZ_ROOT}/components/sensor_vl53l0x/src/vl53l0x_domain.cpp"
)
target_include_directories(sensor_vl53l0x_domain_test PRIVATE
    "${SHAHBAZ_ROOT}/components/sensor_vl53l0x/include"
)

shahbaz_test(sensor_vl53l0x_array_test
    "${SHAHBAZ_ROOT}/components/sensor_vl53l0x/test/vl53l0x_array_test.cpp"
    "${SHAHBAZ_ROOT}/components/sensor_vl53l0x/src/vl53l0x_array.cpp"
    "${SHAHBAZ_ROOT}/components/sensor_vl53l0x/src/vl53l0x_domain.cpp"
)
target_include_directories(sensor_vl53l0x_array_test PRIVATE
    "${SHAHBAZ_ROOT}/components/sensor_vl53l0x/include"
    "${SHAHBAZ_ROOT}/components/interfaces/include"
    "${SHAHBAZ_ROOT}/components/domain/include"
)

shahbaz_test(sensor_scheduler_test
    "${SHAHBAZ_ROOT}/components/sensor_scheduler/test/sensor_scheduler_test.cpp"
    "${SHAHBAZ_ROOT}/components/sensor_scheduler/src/sensor_scheduler.cpp"
    "${SHAHBAZ_ROOT}/components/sensor_sht30/src/sht3x_domain.cpp"
    "${SHAHBAZ_ROOT}/components/sensor_ms5611/src/ms5611_domain.cpp"
    "${SHAHBAZ_ROOT}/components/sensor_vl53l0x/src/vl53l0x_array.cpp"
    "${SHAHBAZ_ROOT}/components/sensor_vl53l0x/src/vl53l0x_domain.cpp"
)
target_include_directories(sensor_scheduler_test PRIVATE
    "${SHAHBAZ_ROOT}/components/sensor_scheduler/include"
    "${SHAHBAZ_ROOT}/components/interfaces/include"
    "${SHAHBAZ_ROOT}/components/domain/include"
    "${SHAHBAZ_ROOT}/components/sensor_sht30/include"
    "${SHAHBAZ_ROOT}/components/sensor_ms5611/include"
    "${SHAHBAZ_ROOT}/components/sensor_vl53l0x/include"
)

shahbaz_test(telemetry_protocol_test
    "${SHAHBAZ_ROOT}/components/telemetry_protocol/test/test_telemetry_protocol.cpp"
    "${SHAHBAZ_ROOT}/components/telemetry_protocol/src/cobs.cpp"
    "${SHAHBAZ_ROOT}/components/telemetry_protocol/src/crc32c.cpp"
    "${SHAHBAZ_ROOT}/components/telemetry_protocol/src/frame_accumulator.cpp"
    "${SHAHBAZ_ROOT}/components/telemetry_protocol/src/wire_protocol.cpp"
)
target_include_directories(telemetry_protocol_test PRIVATE
    "${SHAHBAZ_ROOT}/components/telemetry_protocol/include"
)

shahbaz_test(safety_supervisor_test
    "${SHAHBAZ_ROOT}/components/safety_supervisor/test/test_safety_supervisor.cpp"
    "${SHAHBAZ_ROOT}/components/safety_supervisor/src/safety_supervisor.cpp"
)
target_include_directories(safety_supervisor_test PRIVATE
    "${SHAHBAZ_ROOT}/components/safety_supervisor/include"
)

shahbaz_test(null_actuator_test
    "${SHAHBAZ_ROOT}/components/actuator_null/test/test_null_actuator.cpp"
    "${SHAHBAZ_ROOT}/components/actuator_null/src/null_actuator_controller.cpp"
)
target_include_directories(null_actuator_test PRIVATE
    "${SHAHBAZ_ROOT}/components/actuator_null/include"
    "${SHAHBAZ_ROOT}/components/safety_supervisor/include"
)

shahbaz_test(command_dispatcher_test
    "${SHAHBAZ_ROOT}/components/command_dispatcher/test/test_command_dispatcher.cpp"
    "${SHAHBAZ_ROOT}/components/command_dispatcher/src/command_dispatcher.cpp"
    "${SHAHBAZ_ROOT}/components/safety_supervisor/src/safety_supervisor.cpp"
    "${SHAHBAZ_ROOT}/components/telemetry_protocol/src/cobs.cpp"
    "${SHAHBAZ_ROOT}/components/telemetry_protocol/src/crc32c.cpp"
    "${SHAHBAZ_ROOT}/components/telemetry_protocol/src/frame_accumulator.cpp"
    "${SHAHBAZ_ROOT}/components/telemetry_protocol/src/wire_protocol.cpp"
)
target_include_directories(command_dispatcher_test PRIVATE
    "${SHAHBAZ_ROOT}/components/command_dispatcher/include"
    "${SHAHBAZ_ROOT}/components/safety_supervisor/include"
    "${SHAHBAZ_ROOT}/components/telemetry_protocol/include"
)

shahbaz_test(pwm_math_test
    "${SHAHBAZ_ROOT}/components/actuator_espidf/test/pwm_math_test.cpp"
)
target_include_directories(pwm_math_test PRIVATE
    "${SHAHBAZ_ROOT}/components/actuator_espidf/include"
)

shahbaz_test(espidf_motor_frame_test
    "${SHAHBAZ_ROOT}/components/actuator_espidf/test/motor_frame_test.cpp"
    "${SHAHBAZ_ROOT}/components/actuator_espidf/src/espidf_pwm_actuator_controller.cpp"
)
target_include_directories(espidf_motor_frame_test PRIVATE
    "${SHAHBAZ_ROOT}/components/actuator_espidf/test/fake_idf"
    "${SHAHBAZ_ROOT}/components/actuator_espidf/include"
    "${SHAHBAZ_ROOT}/components/safety_supervisor/include"
    "${SHAHBAZ_ROOT}/components/board_support/include"
)

shahbaz_test(device_link_test
    "${SHAHBAZ_ROOT}/components/device_link/test/device_link_test.cpp"
    "${SHAHBAZ_ROOT}/components/device_link/src/device_frame_sender.cpp"
    "${SHAHBAZ_ROOT}/components/device_link/src/sensor_telemetry_publisher.cpp"
    "${SHAHBAZ_ROOT}/components/device_link/src/protocol_engine.cpp"
    "${SHAHBAZ_ROOT}/components/command_dispatcher/src/command_dispatcher.cpp"
    "${SHAHBAZ_ROOT}/components/safety_supervisor/src/safety_supervisor.cpp"
    "${SHAHBAZ_ROOT}/components/sensor_scheduler/src/sensor_scheduler.cpp"
    "${SHAHBAZ_ROOT}/components/sensor_sht30/src/sht3x_domain.cpp"
    "${SHAHBAZ_ROOT}/components/sensor_ms5611/src/ms5611_domain.cpp"
    "${SHAHBAZ_ROOT}/components/sensor_vl53l0x/src/vl53l0x_array.cpp"
    "${SHAHBAZ_ROOT}/components/sensor_vl53l0x/src/vl53l0x_domain.cpp"
    "${SHAHBAZ_ROOT}/components/telemetry_protocol/src/cobs.cpp"
    "${SHAHBAZ_ROOT}/components/telemetry_protocol/src/crc32c.cpp"
    "${SHAHBAZ_ROOT}/components/telemetry_protocol/src/frame_accumulator.cpp"
    "${SHAHBAZ_ROOT}/components/telemetry_protocol/src/wire_protocol.cpp"
)
target_include_directories(device_link_test PRIVATE
    "${SHAHBAZ_ROOT}/components/device_link/include"
    "${SHAHBAZ_ROOT}/components/command_dispatcher/include"
    "${SHAHBAZ_ROOT}/components/safety_supervisor/include"
    "${SHAHBAZ_ROOT}/components/sensor_scheduler/include"
    "${SHAHBAZ_ROOT}/components/sensor_sht30/include"
    "${SHAHBAZ_ROOT}/components/sensor_ms5611/include"
    "${SHAHBAZ_ROOT}/components/sensor_vl53l0x/include"
    "${SHAHBAZ_ROOT}/components/telemetry_protocol/include"
    "${SHAHBAZ_ROOT}/components/interfaces/include"
    "${SHAHBAZ_ROOT}/components/domain/include"
)
