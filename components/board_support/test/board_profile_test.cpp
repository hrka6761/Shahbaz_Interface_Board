/**
 * @file board_profile_test.cpp
 * @brief Host tests for YD-ESP32-S3 N16R8 pin exclusions and board-safe LED policy.
 */
#include "shahbaz/board/board_profile.hpp"

#include <cstdlib>
#include <initializer_list>
#include <iostream>

namespace {

auto expect(const bool condition, const char* const message) -> int {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        return 1;
    }
    return 0;
}

} // namespace

int main() {
    using shahbaz::board::BoardRevision;
    using shahbaz::board::PinClassification;
    using shahbaz::board::UsbActivationEvidence;
    using shahbaz::board::classify_gpio;

    int failures = 0;
    for (const int gpio : {26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37}) {
        failures += expect(classify_gpio(gpio) == PinClassification::InternalMemoryForbidden,
                           "WROOM memory-path GPIO is forbidden");
    }
    for (const int gpio : {22, 23, 24, 25}) {
        failures += expect(classify_gpio(gpio) == PinClassification::Nonexistent,
                           "nonexistent ESP32-S3 GPIO is rejected");
    }
    failures += expect(classify_gpio(19) == PinClassification::NativeUsbReserved,
                       "USB D- is reserved");
    failures += expect(classify_gpio(20) == PinClassification::NativeUsbReserved,
                       "USB D+ is reserved");
    failures += expect(!shahbaz::board::is_valid_i2c_pair(8, 19),
                       "native USB cannot be reassigned to I2C");
    failures += expect(!shahbaz::board::is_valid_i2c_pair(35, 9),
                       "PSRAM pin cannot be assigned to I2C");
    failures += expect(!shahbaz::board::is_valid_i2c_pair(26, 9),
                       "flash bus pin cannot be assigned to I2C");
    failures += expect(!shahbaz::board::is_valid_i2c_pair(22, 9),
                       "nonexistent GPIO cannot be assigned to I2C");
    failures += expect(!shahbaz::board::is_valid_i2c_pair(8, 8),
                       "SDA and SCL cannot share one GPIO");
    failures += expect(shahbaz::board::is_valid_i2c_pair(8, 9),
                       "configured I2C defaults are valid");
    failures += expect(!shahbaz::board::is_valid_i2c_pair(1, 2),
                       "alternate I2C pins fail closed without physical review");
    failures += expect(shahbaz::board::is_valid_i2c_pair(1, 2, true),
                       "reviewed alternate I2C pins may be selected");
    failures += expect(!shahbaz::board::is_valid_i2c_pair(19, 20, true),
                       "physical review cannot override native USB reservations");
    failures += expect(!shahbaz::board::is_valid_i2c_pair(26, 27, true),
                       "physical review cannot override memory-path restrictions");
    constexpr UsbActivationEvidence complete_usb_evidence{
        true, true, true, true, true, true};
    failures += expect(shahbaz::board::usb_activation_evidence_complete(complete_usb_evidence),
                       "USB activation accepts every independent evidence gate");
    constexpr UsbActivationEvidence missing_record{
        true, true, true, true, true, false};
    failures += expect(!shahbaz::board::usb_activation_evidence_complete(missing_record),
                       "USB activation rejects a missing evidence record ID");
    constexpr UsbActivationEvidence missing_enumeration{
        true, true, true, true, false, true};
    failures += expect(!shahbaz::board::usb_activation_evidence_complete(missing_enumeration),
                       "USB activation rejects unverified isolated enumeration");
    constexpr UsbActivationEvidence missing_reverse_current{
        true, true, true, false, true, true};
    failures += expect(!shahbaz::board::usb_activation_evidence_complete(missing_reverse_current),
                       "USB activation rejects incomplete reverse-current evidence");
    failures += expect(shahbaz::board::onboard_rgb_gpio(BoardRevision::Unverified) == -1,
                       "unverified revision has no enabled LED GPIO");
    failures += expect(classify_gpio(38) == PinClassification::AvailableWithReview,
                       "YD header GPIO38 remains available with review");
    failures += expect(classify_gpio(48) == PinClassification::RevisionReserved,
                       "YD onboard RGB GPIO48 remains reserved");
    failures += expect(shahbaz::board::is_actuator_gpio_candidate(4),
                       "default motor GPIO4 is an actuator candidate after review");
    failures += expect(shahbaz::board::is_actuator_gpio_candidate(11),
                       "default servo GPIO11 is an actuator candidate after review");
    failures += expect(!shahbaz::board::is_actuator_gpio_candidate(19),
                       "native USB GPIO cannot become an actuator output");
    failures += expect(!shahbaz::board::is_actuator_gpio_candidate(0),
                       "strapping GPIO cannot become an actuator output");
    failures += expect(
        shahbaz::board::is_valid_vl53l0x_xshut_pins(
            shahbaz::board::kDefaultVl53l0xXshutGpio),
        "default GPIO12..15 XSHUT proposal is electrically eligible");
    failures += expect(
        !shahbaz::board::is_valid_vl53l0x_xshut_pins({{12, 12, 14, 15}}),
        "XSHUT pins must be unique");
    failures += expect(
        !shahbaz::board::is_valid_vl53l0x_xshut_pins({{8, 13, 14, 15}}),
        "XSHUT cannot overlap SDA");
    failures += expect(
        !shahbaz::board::is_valid_vl53l0x_xshut_pins({{19, 13, 14, 15}}),
        "XSHUT cannot use native USB");
    failures += expect(
        !shahbaz::board::is_valid_vl53l0x_xshut_pins({{0, 13, 14, 15}}),
        "XSHUT cannot use a strapping-sensitive GPIO");
    constexpr std::array<std::int32_t, 6U> default_actuators{{4, 5, 6, 7, 10, 11}};
    failures += expect(
        !shahbaz::board::vl53l0x_xshut_conflicts(
            shahbaz::board::kDefaultVl53l0xXshutGpio, default_actuators),
        "default XSHUT proposal does not overlap default actuators");
    constexpr std::array<std::int32_t, 2U> conflicting_outputs{{15, 18}};
    failures += expect(
        shahbaz::board::vl53l0x_xshut_conflicts(
            shahbaz::board::kDefaultVl53l0xXshutGpio, conflicting_outputs),
        "cross-peripheral XSHUT conflicts are detected");
    failures += expect(shahbaz::board::onboard_rgb_gpio(BoardRevision::YdEsp32S3V1_4) == 48,
                       "verified YD-ESP32-S3 V1.4 profile maps onboard RGB to GPIO48");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
