/**
 * @file espidf_board_validator.hpp
 * @brief Runtime N16R8 memory and physical-evidence gate validation.
 *
 * This adapter reads ESP-IDF memory capabilities and compile-time evidence
 * attestations. It does not probe or infer connector routing, PCB revision, or
 * power topology.
 */
#pragma once

#include "shahbaz/interfaces/board_validator.hpp"

namespace shahbaz::platform {

class EspIdfBoardValidator final : public interfaces::IBoardValidator {
  public:
    /** Performs bounded local capability checks; no external bus I/O occurs. */
    [[nodiscard]] auto validate() const noexcept
        -> interfaces::BoardValidationReport override;
};

} // namespace shahbaz::platform

