/**
 * @file vl53l0x_domain_test.cpp
 * @brief Exhaustive host tests for pure VL53L0X parsing and SPAD policy.
 */
#include "sensor_vl53l0x/vl53l0x_domain.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {

auto expect(const bool condition, const char* const message) -> int {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        return 1;
    }
    return 0;
}

}  // namespace

int main() {
    namespace sensor = shahbaz::sensors::vl53l0x;
    int failures = 0;

    failures += expect(sensor::kDefaultAddress == 0x29U,
                       "seven-bit default address is 0x29");
    for (std::size_t index = 0U; index < sensor::kSensorCount; ++index) {
        failures += expect(
            sensor::role_for_instance(index) == static_cast<sensor::Role>(index),
            "instance-to-role mapping is stable");
        failures += expect(sensor::is_usable_seven_bit_address(
                               sensor::kDefaultAssignedAddresses[index]),
                           "assigned address is a usable seven-bit address");
        for (std::size_t other = index + 1U; other < sensor::kSensorCount; ++other) {
            failures += expect(
                sensor::kDefaultAssignedAddresses[index] !=
                    sensor::kDefaultAssignedAddresses[other],
                "assigned addresses are unique");
        }
    }
    failures += expect(!sensor::is_usable_seven_bit_address(0x07U),
                       "reserved low address is rejected");
    failures += expect(!sensor::is_usable_seven_bit_address(0x78U),
                       "reserved high address is rejected");

    const std::array<std::uint8_t, 2U> model{{0xEEU, 0xAAU}};
    const auto model_ok = sensor::parse_model_id(model.data(), model.size());
    failures += expect(model_ok.ok() && model_ok.value == sensor::kExpectedModelId,
                       "expected model ID is accepted");
    const std::array<std::uint8_t, 2U> wrong_model{{0xEEU, 0xABU}};
    failures += expect(
        sensor::parse_model_id(wrong_model.data(), wrong_model.size()).error ==
            sensor::Error::UnexpectedModelId,
        "unexpected model ID is explicit");
    failures += expect(sensor::parse_model_id(nullptr, 2U).error == sensor::Error::NullData,
                       "null model ID input is rejected");
    failures += expect(sensor::parse_model_id(model.data(), 1U).error ==
                           sensor::Error::InvalidLength,
                       "truncated model ID is rejected");

    for (std::uint8_t raw_status = 0U; raw_status < 16U; ++raw_status) {
        std::array<std::uint8_t, 12U> bytes{};
        bytes[0] = static_cast<std::uint8_t>(raw_status << 3U);
        bytes[10] = 0x03U;
        bytes[11] = 0xE8U;
        const auto result = sensor::parse_range_result(bytes.data(), bytes.size());
        failures += expect(result.ok(), "complete range block parses");
        failures += expect(result.value.raw_status == raw_status,
                           "raw range status is preserved");
        const bool expected_valid = raw_status == 0U || raw_status == 11U;
        failures += expect(result.value.status_valid == expected_valid,
                           "all raw range status validity states are covered");
        failures += expect(result.value.control_eligible() == expected_valid,
                           "status gates control eligibility");
        failures += expect(result.value.signal_quality_percent ==
                               static_cast<std::uint8_t>(expected_valid ? 100U : 0U),
                           "quality is deterministic from validated evidence");
    }

    for (const auto distance :
         {std::uint16_t{0U}, std::uint16_t{29U}, std::uint16_t{30U},
          std::uint16_t{2'000U}, std::uint16_t{2'001U}, std::uint16_t{65'535U}}) {
        std::array<std::uint8_t, 12U> bytes{};
        bytes[10] = static_cast<std::uint8_t>(distance >> 8U);
        bytes[11] = static_cast<std::uint8_t>(distance & 0xFFU);
        const auto result = sensor::parse_range_result(bytes.data(), bytes.size());
        const bool expected = distance >= sensor::kMinimumControlDistanceMm &&
                              distance <= sensor::kMaximumControlDistanceMm;
        failures += expect(result.value.distance_mm == distance,
                           "distance preserves full uint16 value");
        failures += expect(result.value.distance_within_control_range == expected,
                           "distance boundary policy is inclusive and exhaustive");
        failures += expect(result.value.control_eligible() == expected,
                           "valid status still requires a usable distance");
    }
    std::array<std::uint8_t, 12U> range_bytes{};
    failures += expect(sensor::parse_range_result(nullptr, range_bytes.size()).error ==
                           sensor::Error::NullData,
                       "null range block is rejected");
    failures += expect(sensor::parse_range_result(range_bytes.data(), 11U).error ==
                           sensor::Error::InvalidLength,
                       "truncated range block is rejected");
    failures += expect(sensor::parse_range_result(range_bytes.data(), 13U).error ==
                           sensor::Error::InvalidLength,
                       "oversized range block is rejected");

    std::array<std::uint8_t, 6U> all_spads{{0xFFU, 0xFFU, 0xFFU,
                                            0xFFU, 0xFFU, 0xFFU}};
    auto non_aperture = all_spads;
    failures += expect(sensor::apply_reference_spad_selection(non_aperture, 5U, false),
                       "non-aperture SPAD selection succeeds");
    failures += expect(non_aperture[0] == 0x1FU && non_aperture[1] == 0U,
                       "non-aperture selection enables the first requested SPADs only");
    auto aperture = all_spads;
    failures += expect(sensor::apply_reference_spad_selection(aperture, 5U, true),
                       "aperture SPAD selection succeeds");
    failures += expect(aperture[0] == 0U && aperture[1] == 0xF0U &&
                           aperture[2] == 0x01U,
                       "aperture selection skips the first twelve SPADs");
    const auto unchanged = all_spads;
    failures += expect(!sensor::apply_reference_spad_selection(all_spads, 0U, false) &&
                           all_spads == unchanged,
                       "zero SPAD request fails without mutation");
    std::array<std::uint8_t, 6U> sparse{};
    sparse[0] = 0x01U;
    const auto sparse_before = sparse;
    failures += expect(!sensor::apply_reference_spad_selection(sparse, 2U, false) &&
                           sparse == sparse_before,
                       "insufficient factory SPAD map fails without mutation");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
