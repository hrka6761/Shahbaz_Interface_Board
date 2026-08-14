#include "shahbaz/actuator/null_actuator_controller.hpp"

#include <cstdio>

namespace {

#define CHECK(condition)                                                               \
    do {                                                                               \
        if (!(condition)) {                                                            \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__,   \
                         #condition);                                                  \
            return false;                                                              \
        }                                                                              \
    } while (false)

bool testNullControllerHasNoActiveState() {
    shahbaz::actuator::NullActuatorController controller{};
    CHECK(!controller.outputsEnabled());
    CHECK(!controller.armed());
    CHECK(controller.safeRequestCount() == 0U);
    CHECK(!controller.available());
    CHECK(controller.arm() == shahbaz::safety::ActuatorStatus::Unavailable);
    CHECK(controller.writePulseUs(shahbaz::safety::ActuatorKind::Motor, 0U, 1000U) ==
          shahbaz::safety::ActuatorStatus::Unavailable);

    controller.forceSafe(shahbaz::safety::SafeStopReason::Startup);
    controller.forceSafe(shahbaz::safety::SafeStopReason::ArmRejected);
    controller.forceSafe(shahbaz::safety::SafeStopReason::EmergencyStop);

    CHECK(controller.safeRequestCount() == 3U);
    CHECK(controller.lastReason() == shahbaz::safety::SafeStopReason::EmergencyStop);
    CHECK(!controller.outputsEnabled());
    CHECK(!controller.armed());
    return true;
}

}  // namespace

int main() {
    return testNullControllerHasNoActiveState() ? 0 : 1;
}

