/**
 * @file espidf_board_validator.cpp
 * @brief ESP-IDF implementation of the fail-closed board capability gate.
 */
#include "shahbaz/platform/espidf_board_validator.hpp"

#include "shahbaz/board/board_profile.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#include "esp_flash.h"  // ESP-IDF 5.4 public header contains a GNU anonymous struct.
#pragma GCC diagnostic pop
#include "esp_psram.h"
#include "sdkconfig.h"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace shahbaz::platform {
namespace {

constexpr auto issue_bit(const interfaces::BoardValidationIssue issue) noexcept -> std::uint32_t {
    return static_cast<std::uint32_t>(issue);
}

constexpr auto configured_revision_verified() noexcept -> bool {
#if defined(CONFIG_SHAHBAZ_BOARD_REVISION_YD_ESP32_S3_V1_4)
    return CONFIG_SHAHBAZ_BOARD_REVISION_EVIDENCE_RECORD_ID[0] != '\0';
#else
    return false;
#endif
}

constexpr auto module_identity_verified() noexcept -> bool {
#if defined(CONFIG_SHAHBAZ_N16R8_MODULE_IDENTITY_VERIFIED)
    return CONFIG_SHAHBAZ_MODULE_IDENTITY_EVIDENCE_RECORD_ID[0] != '\0';
#else
    return false;
#endif
}

constexpr auto native_usb_route_verified() noexcept -> bool {
#if defined(CONFIG_SHAHBAZ_NATIVE_USB_ROUTE_VERIFIED)
    return true;
#else
    return false;
#endif
}

constexpr auto board_power_path_verified() noexcept -> bool {
#if defined(CONFIG_SHAHBAZ_BOARD_POWER_PATH_VERIFIED)
    return true;
#else
    return false;
#endif
}

constexpr auto vbus_isolation_verified() noexcept -> bool {
#if defined(CONFIG_SHAHBAZ_USB_VBUS_ISOLATION_VERIFIED)
    return true;
#else
    return false;
#endif
}

constexpr auto usb_reverse_current_verified() noexcept -> bool {
#if defined(CONFIG_SHAHBAZ_USB_BIDIRECTIONAL_REVERSE_CURRENT_VERIFIED)
    return true;
#else
    return false;
#endif
}

constexpr auto usb_isolated_enumeration_verified() noexcept -> bool {
#if defined(CONFIG_SHAHBAZ_USB_ISOLATED_ENUMERATION_VERIFIED)
    return true;
#else
    return false;
#endif
}

constexpr auto usb_evidence_record_present() noexcept -> bool {
    return CONFIG_SHAHBAZ_USB_EVIDENCE_RECORD_ID[0] != '\0';
}

constexpr auto alternate_i2c_evidence_present() noexcept -> bool {
#if defined(CONFIG_SHAHBAZ_ALT_I2C_PINS_PHYSICALLY_REVIEWED)
    return CONFIG_SHAHBAZ_ALT_I2C_EVIDENCE_RECORD_ID[0] != '\0';
#else
    return false;
#endif
}

constexpr auto sensor_hardware_verified() noexcept -> bool {
#if defined(CONFIG_SHAHBAZ_SENSOR_HARDWARE_VERIFIED)
    return CONFIG_SHAHBAZ_SENSOR_EVIDENCE_RECORD_ID[0] != '\0';
#else
    return false;
#endif
}

constexpr auto actuator_evidence_present() noexcept -> bool {
#if defined(CONFIG_SHAHBAZ_ACTUATORS_ENABLE) && CONFIG_SHAHBAZ_ACTUATORS_ENABLE && \
    defined(CONFIG_SHAHBAZ_ACTUATOR_PINS_PHYSICALLY_REVIEWED) && \
    CONFIG_SHAHBAZ_ACTUATOR_PINS_PHYSICALLY_REVIEWED
    return CONFIG_SHAHBAZ_ACTUATOR_EVIDENCE_RECORD_ID[0] != '\0';
#else
    return false;
#endif
}

constexpr auto actuator_pin_configuration_valid() noexcept -> bool {
#if defined(CONFIG_SHAHBAZ_ACTUATORS_ENABLE) && CONFIG_SHAHBAZ_ACTUATORS_ENABLE
    constexpr int pins[] = {
        CONFIG_SHAHBAZ_MOTOR0_GPIO, CONFIG_SHAHBAZ_MOTOR1_GPIO,
        CONFIG_SHAHBAZ_MOTOR2_GPIO, CONFIG_SHAHBAZ_MOTOR3_GPIO,
        CONFIG_SHAHBAZ_SERVO0_GPIO, CONFIG_SHAHBAZ_SERVO1_GPIO,
    };
    for (std::size_t i = 0U; i < (sizeof(pins) / sizeof(pins[0])); ++i) {
        if (!board::is_actuator_gpio_candidate(pins[i])) return false;
        for (std::size_t j = i + 1U; j < (sizeof(pins) / sizeof(pins[0])); ++j) {
            if (pins[i] == pins[j]) return false;
        }
    }
#endif
    return true;
}

} // namespace

auto EspIdfBoardValidator::validate() const noexcept -> interfaces::BoardValidationReport {
    interfaces::BoardValidationReport report{};

    std::uint32_t flash_bytes = 0U;
    const auto flash_status = esp_flash_get_size(nullptr, &flash_bytes);
    report.detected_flash_bytes = flash_bytes;
    if (flash_status != ESP_OK || flash_bytes != board::kExpectedFlashBytes) {
        report.issues |= issue_bit(interfaces::BoardValidationIssue::WrongFlashSize);
    }

    if (!esp_psram_is_initialized()) {
        report.issues |= issue_bit(interfaces::BoardValidationIssue::PsramUnavailable);
    } else {
        const std::size_t psram_bytes = esp_psram_get_size();
        if (psram_bytes <= std::numeric_limits<std::uint32_t>::max()) {
            report.detected_psram_bytes = static_cast<std::uint32_t>(psram_bytes);
        }
        if (psram_bytes != board::kExpectedPsramBytes) {
            report.issues |= issue_bit(interfaces::BoardValidationIssue::WrongPsramSize);
        }
    }

    constexpr auto configured_sda = CONFIG_SHAHBAZ_I2C_SDA_GPIO;
    constexpr auto configured_scl = CONFIG_SHAHBAZ_I2C_SCL_GPIO;
    constexpr bool alternate_i2c_attested = alternate_i2c_evidence_present();
    if (!board::is_valid_i2c_pair(configured_sda, configured_scl, alternate_i2c_attested)) {
        report.issues |= issue_bit(interfaces::BoardValidationIssue::InvalidPinConfiguration);
    }
    if (!board::is_default_i2c_pair(configured_sda, configured_scl) &&
        board::is_valid_i2c_pair(configured_sda, configured_scl, true) &&
        !alternate_i2c_attested) {
        report.issues |= issue_bit(interfaces::BoardValidationIssue::AlternateI2cEvidenceMissing);
    }
    if (!configured_revision_verified()) {
        report.issues |= issue_bit(interfaces::BoardValidationIssue::BoardRevisionUnverified);
    }
    if (!module_identity_verified()) {
        report.issues |= issue_bit(interfaces::BoardValidationIssue::ModuleIdentityUnverified);
    }
    constexpr board::UsbActivationEvidence usb_evidence{
        board_power_path_verified(),
        native_usb_route_verified(),
        vbus_isolation_verified(),
        usb_reverse_current_verified(),
        usb_isolated_enumeration_verified(),
        usb_evidence_record_present(),
    };
    if (!usb_evidence.connector_route_verified) {
        report.issues |= issue_bit(interfaces::BoardValidationIssue::NativeUsbRouteUnverified);
    }
    if (!usb_evidence.exact_power_path_verified) {
        report.issues |= issue_bit(interfaces::BoardValidationIssue::BoardPowerPathUnverified);
    }
    if (!usb_evidence.vbus_isolation_verified) {
        report.issues |= issue_bit(interfaces::BoardValidationIssue::VbusIsolationUnverified);
    }
    if (!usb_evidence.bidirectional_reverse_current_verified) {
        report.issues |= issue_bit(interfaces::BoardValidationIssue::UsbReverseCurrentUnverified);
    }
    if (!usb_evidence.isolated_enumeration_verified) {
        report.issues |=
            issue_bit(interfaces::BoardValidationIssue::UsbIsolatedEnumerationUnverified);
    }
    if (!usb_evidence.evidence_record_present) {
        report.issues |= issue_bit(interfaces::BoardValidationIssue::UsbEvidenceRecordMissing);
    }
    if (!sensor_hardware_verified()) {
        report.issues |= issue_bit(interfaces::BoardValidationIssue::SensorHardwareUnverified);
    }
#if defined(CONFIG_SHAHBAZ_ACTUATORS_ENABLE) && CONFIG_SHAHBAZ_ACTUATORS_ENABLE
    if (!actuator_pin_configuration_valid()) {
        report.issues |=
            issue_bit(interfaces::BoardValidationIssue::InvalidActuatorPinConfiguration);
    }
    if (!actuator_evidence_present()) {
        report.issues |= issue_bit(interfaces::BoardValidationIssue::ActuatorEvidenceMissing);
    }
#endif

    return report;
}

} // namespace shahbaz::platform
