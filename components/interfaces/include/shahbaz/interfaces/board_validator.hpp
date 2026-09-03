/**
 * @file board_validator.hpp
 * @brief Port for validating runtime memory capabilities and evidence gates.
 */
#pragma once

#include <cstdint>

namespace shahbaz::interfaces {

enum class BoardValidationIssue : std::uint32_t {
    None = 0,
    WrongFlashSize = 1U << 0U,
    PsramUnavailable = 1U << 1U,
    WrongPsramSize = 1U << 2U,
    InvalidPinConfiguration = 1U << 3U,
    BoardRevisionUnverified = 1U << 4U,
    NativeUsbRouteUnverified = 1U << 5U,
    VbusIsolationUnverified = 1U << 6U,
    SensorHardwareUnverified = 1U << 7U,
    BoardPowerPathUnverified = 1U << 8U,
    UsbReverseCurrentUnverified = 1U << 9U,
    UsbIsolatedEnumerationUnverified = 1U << 10U,
    UsbEvidenceRecordMissing = 1U << 11U,
    AlternateI2cEvidenceMissing = 1U << 12U,
    ModuleIdentityUnverified = 1U << 13U,
    InvalidActuatorPinConfiguration = 1U << 14U,
    ActuatorEvidenceMissing = 1U << 15U,
    RangefinderEvidenceMissing = 1U << 16U,
    InvalidRangefinderPinConfiguration = 1U << 17U,
    RangefinderActuatorPinConflict = 1U << 18U,
};

struct BoardValidationReport final {
    std::uint32_t issues{};
    std::uint32_t detected_flash_bytes{};
    std::uint32_t detected_psram_bytes{};

    [[nodiscard]] constexpr auto has(const BoardValidationIssue issue) const noexcept -> bool {
        return (issues & static_cast<std::uint32_t>(issue)) != 0U;
    }

    [[nodiscard]] constexpr auto runtime_memory_matches() const noexcept -> bool {
        constexpr auto memory_mask = static_cast<std::uint32_t>(BoardValidationIssue::WrongFlashSize) |
                                     static_cast<std::uint32_t>(BoardValidationIssue::PsramUnavailable) |
                                     static_cast<std::uint32_t>(BoardValidationIssue::WrongPsramSize);
        return (issues & memory_mask) == 0U;
    }
};

class IBoardValidator {
  public:
    virtual ~IBoardValidator() = default;
    [[nodiscard]] virtual auto validate() const noexcept -> BoardValidationReport = 0;
};

} // namespace shahbaz::interfaces
