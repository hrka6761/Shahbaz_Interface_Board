#include "shahbaz/actuator/espidf_pwm_actuator_controller.hpp"

#include "driver/ledc.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

#define CHECK(condition)                                                               \
    do {                                                                               \
        if (!(condition)) {                                                            \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__,   \
                         #condition);                                                  \
            return false;                                                              \
        }                                                                              \
    } while (false)

enum class EventKind : std::uint8_t { Timer, Configure, Stop, Set, Update };
struct Event final {
    EventKind kind{};
    int channel{-1};
    std::uint32_t duty{};
};

struct FakeLedc final {
    std::vector<Event> events{};
    int fail_set_call{-1};
    int fail_update_call{-1};
    int set_calls{};
    int update_calls{};

    void clearRuntime() {
        events.clear();
        fail_set_call = -1;
        fail_update_call = -1;
        set_calls = 0;
        update_calls = 0;
    }
};

FakeLedc g_ledc{};

shahbaz::actuator::EspIdfPwmActuatorConfig validConfig() {
    return {{{4, 5, 6, 7}}, {{10, 11}}, 400U, 50U};
}

std::size_t countEvents(const EventKind kind) {
    std::size_t count = 0U;
    for (const auto& event : g_ledc.events) {
        if (event.kind == kind) ++count;
    }
    return count;
}

bool testFrameStagesThenUpdatesEveryChannel() {
    shahbaz::actuator::EspIdfPwmActuatorController controller{validConfig()};
    CHECK(controller.initialize());
    CHECK(controller.arm() == shahbaz::safety::ActuatorStatus::Ok);
    g_ledc.clearRuntime();

    const shahbaz::safety::QuadMotorPulseFrame frame{{900U, 1200U, 1800U, 2100U}};
    CHECK(controller.writeMotorFrame(frame) == shahbaz::safety::ActuatorStatus::Ok);
    CHECK(controller.armed());
    CHECK(controller.outputsEnabled());
    CHECK(g_ledc.events.size() == 8U);
    for (std::size_t index = 0U; index < 4U; ++index) {
        CHECK(g_ledc.events[index].kind == EventKind::Set);
        CHECK(g_ledc.events[index].channel == static_cast<int>(index));
    }
    for (std::size_t index = 0U; index < 4U; ++index) {
        CHECK(g_ledc.events[4U + index].kind == EventKind::Update);
        CHECK(g_ledc.events[4U + index].channel == static_cast<int>(index));
    }
    return true;
}

bool testEveryInvalidValueForcesAllOutputsSafeBeforeAnyWrite() {
    shahbaz::actuator::EspIdfPwmActuatorController controller{validConfig()};
    CHECK(controller.initialize());

    constexpr std::array<std::uint16_t, 2U> invalid_pulses{{899U, 2101U}};
    for (std::size_t channel = 0U; channel < 4U; ++channel) {
        for (const std::uint16_t invalid : invalid_pulses) {
            CHECK(controller.arm() == shahbaz::safety::ActuatorStatus::Ok);
            g_ledc.clearRuntime();
            shahbaz::safety::QuadMotorPulseFrame frame{{1000U, 1000U, 1000U, 1000U}};
            frame[channel] = invalid;
            CHECK(controller.writeMotorFrame(frame) ==
                  shahbaz::safety::ActuatorStatus::InvalidValue);
            CHECK(!controller.armed());
            CHECK(!controller.outputsEnabled());
            CHECK(countEvents(EventKind::Set) == 0U);
            CHECK(countEvents(EventKind::Update) == 0U);
            CHECK(countEvents(EventKind::Stop) == 6U);
        }
    }
    return true;
}

bool testEveryStagingFailureForcesAllOutputsSafe() {
    const shahbaz::safety::QuadMotorPulseFrame frame{{1000U, 1200U, 1400U, 1600U}};
    for (int failure = 1; failure <= 4; ++failure) {
        shahbaz::actuator::EspIdfPwmActuatorController controller{validConfig()};
        CHECK(controller.initialize());
        CHECK(controller.arm() == shahbaz::safety::ActuatorStatus::Ok);
        g_ledc.clearRuntime();
        g_ledc.fail_set_call = failure;
        CHECK(controller.writeMotorFrame(frame) ==
              shahbaz::safety::ActuatorStatus::HardwareError);
        CHECK(!controller.armed());
        CHECK(!controller.outputsEnabled());
        CHECK(g_ledc.set_calls == failure);
        CHECK(g_ledc.update_calls == 0);
        CHECK(countEvents(EventKind::Stop) == 6U);
    }
    return true;
}

bool testEveryUpdateFailureForcesAllOutputsSafe() {
    const shahbaz::safety::QuadMotorPulseFrame frame{{1000U, 1200U, 1400U, 1600U}};
    for (int failure = 1; failure <= 4; ++failure) {
        shahbaz::actuator::EspIdfPwmActuatorController controller{validConfig()};
        CHECK(controller.initialize());
        CHECK(controller.arm() == shahbaz::safety::ActuatorStatus::Ok);
        g_ledc.clearRuntime();
        g_ledc.fail_update_call = failure;
        CHECK(controller.writeMotorFrame(frame) ==
              shahbaz::safety::ActuatorStatus::HardwareError);
        CHECK(!controller.armed());
        CHECK(!controller.outputsEnabled());
        CHECK(g_ledc.set_calls == 4);
        CHECK(g_ledc.update_calls == failure);
        CHECK(countEvents(EventKind::Stop) == 6U);
    }
    return true;
}

bool testPreconditionsRemainSafe() {
    const shahbaz::safety::QuadMotorPulseFrame frame{{1000U, 1000U, 1000U, 1000U}};
    shahbaz::actuator::EspIdfPwmActuatorController controller{validConfig()};
    g_ledc.clearRuntime();
    CHECK(controller.writeMotorFrame(frame) ==
          shahbaz::safety::ActuatorStatus::NotInitialized);
    CHECK(g_ledc.events.empty());
    CHECK(!controller.armed());
    CHECK(!controller.outputsEnabled());

    CHECK(controller.initialize());
    g_ledc.clearRuntime();
    CHECK(controller.writeMotorFrame(frame) == shahbaz::safety::ActuatorStatus::NotArmed);
    CHECK(countEvents(EventKind::Stop) == 6U);
    CHECK(countEvents(EventKind::Set) == 0U);
    CHECK(countEvents(EventKind::Update) == 0U);
    return true;
}

bool testConfiguredPeriodsAccommodateEveryAcceptedPulse() {
    for (const bool motor : {false, true}) {
        auto invalid = validConfig();
        if (motor) invalid.motor_frequency_hz = 500U; // 2100 us cannot fit into 2000 us.
        else invalid.servo_frequency_hz = 400U; // 2500 us leaves no falling edge.
        shahbaz::actuator::EspIdfPwmActuatorController controller{invalid};
        g_ledc.clearRuntime();
        CHECK(!controller.initialize());
        CHECK(!controller.available());
        CHECK(g_ledc.events.empty());
    }
    auto boundary = validConfig();
    boundary.motor_frequency_hz = 476U;
    boundary.servo_frequency_hz = 399U;
    shahbaz::actuator::EspIdfPwmActuatorController controller{boundary};
    CHECK(controller.initialize());
    CHECK(controller.arm() == shahbaz::safety::ActuatorStatus::Ok);
    CHECK(controller.writePulseUs(shahbaz::safety::ActuatorKind::Servo, 0U, 2500U) ==
          shahbaz::safety::ActuatorStatus::Ok);
    CHECK(controller.writeMotorFrame({{2100U, 2100U, 2100U, 2100U}}) ==
          shahbaz::safety::ActuatorStatus::Ok);
    return true;
}

}  // namespace

extern "C" esp_err_t ledc_timer_config(const ledc_timer_config_t*) {
    g_ledc.events.push_back({EventKind::Timer, -1, 0U});
    return ESP_OK;
}

extern "C" esp_err_t ledc_channel_config(const ledc_channel_config_t* config) {
    g_ledc.events.push_back(
        {EventKind::Configure, static_cast<int>(config->channel), config->duty});
    return ESP_OK;
}

extern "C" esp_err_t ledc_stop(const ledc_mode_t,
                                const ledc_channel_t channel,
                                const std::uint32_t idle_level) {
    g_ledc.events.push_back({EventKind::Stop, static_cast<int>(channel), idle_level});
    return ESP_OK;
}

extern "C" esp_err_t ledc_set_duty(const ledc_mode_t,
                                    const ledc_channel_t channel,
                                    const std::uint32_t duty) {
    ++g_ledc.set_calls;
    g_ledc.events.push_back({EventKind::Set, static_cast<int>(channel), duty});
    return g_ledc.set_calls == g_ledc.fail_set_call ? ESP_FAIL : ESP_OK;
}

extern "C" esp_err_t ledc_update_duty(const ledc_mode_t,
                                       const ledc_channel_t channel) {
    ++g_ledc.update_calls;
    g_ledc.events.push_back({EventKind::Update, static_cast<int>(channel), 0U});
    return g_ledc.update_calls == g_ledc.fail_update_call ? ESP_FAIL : ESP_OK;
}

int main() {
    const bool passed = testFrameStagesThenUpdatesEveryChannel() &&
                        testEveryInvalidValueForcesAllOutputsSafeBeforeAnyWrite() &&
                        testEveryStagingFailureForcesAllOutputsSafe() &&
                        testEveryUpdateFailureForcesAllOutputsSafe() &&
                        testPreconditionsRemainSafe() &&
                        testConfiguredPeriodsAccommodateEveryAcceptedPulse();
    return passed ? 0 : 1;
}
