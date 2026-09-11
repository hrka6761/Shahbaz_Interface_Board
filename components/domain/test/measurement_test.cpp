/**
 * @file measurement_test.cpp
 * @brief Host checks for fixed-size typed measurement records.
 */
#include "shahbaz/domain/measurement.hpp"

#include <cstdlib>
#include <iostream>
#include <type_traits>

int main() {
    using namespace shahbaz::domain;

    constexpr auto largest = std::numeric_limits<std::uint64_t>::max();
    static_assert(AcquisitionWindow{100U, 105U}.midpoint_us() == 102U);
    static_assert(AcquisitionWindow{100U, 105U}.maximum_error_us() == 3U);
    static_assert(AcquisitionWindow{largest - 10U, largest}.midpoint_us() ==
                  largest - 5U);
    static_assert(AcquisitionWindow{0U, largest}.maximum_error_us() ==
                  largest / 2U + 1U);
    static_assert(AcquisitionWindow{42U, 42U}.maximum_error_us() == 0U);
    static_assert(!AcquisitionWindow{43U, 42U}.ordered());
    static_assert(AcquisitionWindow{43U, 42U}.maximum_error_us() == largest);
    static_assert(AcquisitionWindow{100U, 105U}.wire_uncertainty_us() == 3U);
    static_assert(AcquisitionWindow{43U, 42U}.wire_uncertainty_us() == UINT32_MAX);
    static_assert(AcquisitionWindow{0U, largest}.wire_uncertainty_us() == UINT32_MAX);

    SensorSample sample{};
    sample.sensor_id = SensorId::Sht3x;
    sample.instance_id = 0U;
    sample.sequence = 7U;
    sample.monotonic_timestamp_us = 42U;
    sample.validity = ValidityFlag::TransportValid | ValidityFlag::CrcValid;
    sample.quality = QualityFlag::Fresh;
    sample.field_count = 2U;
    sample.fields[0] =
        make_signed_field(FieldId::AmbientTemperatureMilliCelsius, 20'070);
    sample.fields[1] =
        make_unsigned_field(FieldId::RelativeHumidityMilliPercent, 50'000U);

    if (!std::is_trivially_copyable_v<SensorSample> || sample.field_count != 2U ||
        sample.fields[0].value != 20'070 || sample.fields[1].value != 50'000) {
        std::cerr << "FAIL: typed fixed-capacity sample\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
