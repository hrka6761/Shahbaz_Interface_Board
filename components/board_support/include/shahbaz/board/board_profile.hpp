/**
 * @file board_profile.hpp
 * @brief Compile-time ESP32-S3 N16R8 pin and evidence policy.
 *
 * The profile records project requirements without pretending that the current
 * physical board, connector routing, or PCB revision has been verified.
 */
#pragma once

#include <cstdint>

namespace shahbaz::board {

constexpr std::uint32_t kExpectedFlashBytes = 16U * 1024U * 1024U;
constexpr std::uint32_t kExpectedPsramBytes = 8U * 1024U * 1024U;

constexpr std::int32_t kDefaultI2cSdaGpio = 8;
constexpr std::int32_t kDefaultI2cSclGpio = 9;
constexpr std::int32_t kUsbDataMinusGpio = 19;
constexpr std::int32_t kUsbDataPlusGpio = 20;
constexpr std::int32_t kDiagnosticUartTxGpio = 43;
constexpr std::int32_t kDiagnosticUartRxGpio = 44;

enum class BoardRevision : std::uint8_t {
    Unverified = 0,
    YdEsp32S3V1_4,
};

enum class PinClassification : std::uint8_t {
    AvailableWithReview = 0,
    ProjectI2c,
    NativeUsbReserved,
    InternalMemoryForbidden,
    Nonexistent,
    RevisionReserved,
    StrappingSensitive,
    DiagnosticReserved,
    Invalid,
};

struct UsbActivationEvidence final {
    bool exact_power_path_verified{};
    bool connector_route_verified{};
    bool vbus_isolation_verified{};
    bool bidirectional_reverse_current_verified{};
    bool isolated_enumeration_verified{};
    bool evidence_record_present{};
};

[[nodiscard]] constexpr auto usb_activation_evidence_complete(
    const UsbActivationEvidence& evidence) noexcept -> bool {
    return evidence.exact_power_path_verified && evidence.connector_route_verified &&
           evidence.vbus_isolation_verified &&
           evidence.bidirectional_reverse_current_verified &&
           evidence.isolated_enumeration_verified && evidence.evidence_record_present;
}

[[nodiscard]] constexpr auto classify_gpio(const std::int32_t gpio) noexcept
    -> PinClassification {
    if (gpio < 0 || gpio > 48) {
        return PinClassification::Invalid;
    }
    if (gpio >= 22 && gpio <= 25) {
        return PinClassification::Nonexistent;
    }
    if (gpio == 8 || gpio == 9) {
        return PinClassification::ProjectI2c;
    }
    if (gpio == 19 || gpio == 20) {
        return PinClassification::NativeUsbReserved;
    }
    if (gpio >= 26 && gpio <= 37) {
        return PinClassification::InternalMemoryForbidden;
    }
    if (gpio == 48) {
        return PinClassification::RevisionReserved;
    }
    if (gpio == 0 || gpio == 3 || gpio == 45 || gpio == 46) {
        return PinClassification::StrappingSensitive;
    }
    if (gpio == 43 || gpio == 44) {
        return PinClassification::DiagnosticReserved;
    }
    return PinClassification::AvailableWithReview;
}

[[nodiscard]] constexpr auto is_actuator_gpio_candidate(const std::int32_t gpio) noexcept -> bool {
    return classify_gpio(gpio) == PinClassification::AvailableWithReview;
}

[[nodiscard]] constexpr auto is_default_i2c_pair(const std::int32_t sda,
                                                  const std::int32_t scl) noexcept -> bool {
    return sda == kDefaultI2cSdaGpio && scl == kDefaultI2cSclGpio;
}

[[nodiscard]] constexpr auto is_valid_i2c_pair(
    const std::int32_t sda,
    const std::int32_t scl,
    const bool alternate_pins_physically_reviewed = false) noexcept -> bool {
    if (sda == scl) {
        return false;
    }
    const auto acceptable = [](const std::int32_t gpio) constexpr {
        const auto classification = classify_gpio(gpio);
        return classification == PinClassification::AvailableWithReview ||
               classification == PinClassification::ProjectI2c;
    };
    if (!acceptable(sda) || !acceptable(scl)) {
        return false;
    }
    return is_default_i2c_pair(sda, scl) || alternate_pins_physically_reviewed;
}

[[nodiscard]] constexpr auto onboard_rgb_gpio(const BoardRevision revision) noexcept
    -> std::int32_t {
    switch (revision) {
    case BoardRevision::YdEsp32S3V1_4:
        return 48;
    case BoardRevision::Unverified:
    default:
        return -1;
    }
}

static_assert(is_valid_i2c_pair(kDefaultI2cSdaGpio, kDefaultI2cSclGpio));
static_assert(is_actuator_gpio_candidate(4));
static_assert(!is_actuator_gpio_candidate(19));
static_assert(!is_actuator_gpio_candidate(35));
static_assert(classify_gpio(19) == PinClassification::NativeUsbReserved);
static_assert(classify_gpio(20) == PinClassification::NativeUsbReserved);
static_assert(classify_gpio(35) == PinClassification::InternalMemoryForbidden);
static_assert(classify_gpio(36) == PinClassification::InternalMemoryForbidden);
static_assert(classify_gpio(37) == PinClassification::InternalMemoryForbidden);
static_assert(classify_gpio(22) == PinClassification::Nonexistent);
static_assert(classify_gpio(25) == PinClassification::Nonexistent);
static_assert(classify_gpio(26) == PinClassification::InternalMemoryForbidden);
static_assert(classify_gpio(34) == PinClassification::InternalMemoryForbidden);
static_assert(onboard_rgb_gpio(BoardRevision::Unverified) == -1);
static_assert(!usb_activation_evidence_complete(UsbActivationEvidence{}));

} // namespace shahbaz::board
