#pragma once

using esp_err_t = int;

inline constexpr esp_err_t ESP_OK = 0;
inline constexpr esp_err_t ESP_ERR_INVALID_ARG = 0x102;
inline constexpr esp_err_t ESP_ERR_INVALID_STATE = 0x103;
inline constexpr esp_err_t ESP_ERR_NOT_FOUND = 0x105;
inline constexpr esp_err_t ESP_ERR_TIMEOUT = 0x107;
