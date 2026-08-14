#include "espidf_i2c_error_mapping.hpp"

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
    using shahbaz::interfaces::I2cStatus;
    using shahbaz::platform::detail::map_i2c_error;

    int failures = 0;
    failures += expect(map_i2c_error(ESP_OK) == I2cStatus::Ok, "ESP_OK maps to Ok");
    failures += expect(map_i2c_error(ESP_ERR_INVALID_ARG) == I2cStatus::InvalidArgument,
                       "invalid argument remains distinct");
    failures += expect(map_i2c_error(ESP_ERR_INVALID_STATE) == I2cStatus::BusError,
                       "transaction non-DONE state is not mislabeled uninitialized");
    failures += expect(map_i2c_error(ESP_ERR_NOT_FOUND) == I2cStatus::NotFound,
                       "not found remains distinct");
    failures += expect(map_i2c_error(ESP_ERR_TIMEOUT) == I2cStatus::Timeout,
                       "timeout remains distinct");
    failures += expect(map_i2c_error(0x7fff) == I2cStatus::BusError,
                       "unknown driver error fails closed as bus error");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
