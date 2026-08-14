#include "shahbaz/actuator/pwm_math.hpp"

#include <cstdlib>
#include <iostream>

#define CHECK(x) do { if (!(x)) { std::cerr << "CHECK failed: " #x << "\n"; return EXIT_FAILURE; } } while (0)
int main() {
    using shahbaz::actuator::pulse_us_to_duty;
    CHECK(pulse_us_to_duty(0U, 50U, 13U) == 0U);
    CHECK(pulse_us_to_duty(1000U, 0U, 13U) == 0U);
    CHECK(pulse_us_to_duty(1000U, 50U, 0U) == 0U);
    CHECK(pulse_us_to_duty(1000U, 50U, 31U) == 0U);
    CHECK(pulse_us_to_duty(1000U, 50U, 13U) == 410U);
    CHECK(pulse_us_to_duty(1500U, 50U, 13U) == 614U);
    CHECK(pulse_us_to_duty(1000U, 400U, 13U) == 3277U);
    CHECK(pulse_us_to_duty(2100U, 400U, 13U) == 6881U);
    CHECK(pulse_us_to_duty(2500U, 400U, 13U) == 8191U); // full-scale clamped safely
    return EXIT_SUCCESS;
}
