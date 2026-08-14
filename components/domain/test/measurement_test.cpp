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
