# ESP-IDF project entry point. Sensor/USB hardware is active by default; physical
# actuator outputs are implemented but remain gated by Kconfig and safety checks.
cmake_minimum_required(VERSION 3.16)

# Seed the ESP-IDF graph from the production composition root. Dependencies
# declared through REQUIRES/PRIV_REQUIRES are added recursively; unrelated IDF
# components (including target test frameworks) must not be processed.
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(shahbaz_sensor_usb_node)

# Fail the target configuration if firmware constants drift from the hardware
# contract or if a Kconfig verification claim names no eligible evidence record.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
if(DEFINED SDKCONFIG)
    set(SHAHBAZ_SDKCONFIG_PATH "${SDKCONFIG}")
elseif(DEFINED ENV{SDKCONFIG})
    set(SHAHBAZ_SDKCONFIG_PATH "$ENV{SDKCONFIG}")
else()
    set(SHAHBAZ_SDKCONFIG_PATH "${CMAKE_SOURCE_DIR}/sdkconfig")
endif()
set(SHAHBAZ_CONTRACT_ARGS "${CMAKE_SOURCE_DIR}/tools/validate_firmware_contract.py")
if(EXISTS "${SHAHBAZ_SDKCONFIG_PATH}")
    list(APPEND SHAHBAZ_CONTRACT_ARGS --sdkconfig "${SHAHBAZ_SDKCONFIG_PATH}")
endif()
execute_process(
    COMMAND "${Python3_EXECUTABLE}" ${SHAHBAZ_CONTRACT_ARGS}
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    RESULT_VARIABLE SHAHBAZ_CONTRACT_RESULT
)
if(NOT SHAHBAZ_CONTRACT_RESULT EQUAL 0)
    message(FATAL_ERROR "Shahbaz firmware/hardware contract validation failed")
endif()
