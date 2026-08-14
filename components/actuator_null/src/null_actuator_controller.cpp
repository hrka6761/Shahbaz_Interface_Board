#include "shahbaz/actuator/null_actuator_controller.hpp"

#include <limits>

namespace shahbaz::actuator {

void NullActuatorController::forceSafe(safety::SafeStopReason reason) noexcept {
    last_reason_ = reason;
    if (safe_request_count_ != std::numeric_limits<std::uint32_t>::max()) {
        ++safe_request_count_;
    }
}

}  // namespace shahbaz::actuator

