/**
 * @file app_main.cpp
 * @brief ESP32-S3 N16R8 composition root: validated I2C + native USB v2 + fail-safe actuators.
 */
#include "sdkconfig.h"

#include "shahbaz/actuator/null_actuator_controller.hpp"
#include "shahbaz/command/command_dispatcher.hpp"
#include "shahbaz/health/task_health_monitor.hpp"
#include "shahbaz/interfaces/board_validator.hpp"
#include "shahbaz/link/device_frame_sender.hpp"
#include "shahbaz/link/protocol_engine.hpp"
#include "shahbaz/link/sensor_telemetry_publisher.hpp"
#include "shahbaz/platform/espidf_board_validator.hpp"
#include "shahbaz/platform/espidf_i2c_bus.hpp"
#include "shahbaz/platform/espidf_monotonic_clock.hpp"
#include "shahbaz/safety/safety_supervisor.hpp"
#include "shahbaz/sensors/sensor_scheduler.hpp"
#include "shahbaz/usb/espidf_usb_cdc_transport.hpp"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>

namespace {
constexpr char kLogTag[] = "shahbaz";
// Always block for at least one scheduler tick. At ESP-IDF's 100 Hz default,
// pdMS_TO_TICKS(1) truncates to zero and lets the main task starve IDLE0,
// which correctly trips the task watchdog.
constexpr TickType_t kLoopDelayTicks = 1U;
constexpr std::uint64_t kStatusLogPeriodUs = 5'000'000U;
constexpr std::uint64_t kCriticalServiceMaxSilenceUs = 250'000U;

void log_issue(const shahbaz::interfaces::BoardValidationReport& report,
               const shahbaz::interfaces::BoardValidationIssue issue,
               const char* const message) noexcept {
    if (report.has(issue)) ESP_LOGW(kLogTag, "%s", message);
}

[[nodiscard]] auto new_session_token() noexcept -> std::uint64_t {
    std::uint64_t token = 0U;
    while (token == 0U) {
        token = (static_cast<std::uint64_t>(esp_random()) << 32U) |
                static_cast<std::uint64_t>(esp_random());
    }
    return token;
}

[[nodiscard]] constexpr auto alternate_i2c_reviewed() noexcept -> bool {
#if defined(CONFIG_SHAHBAZ_ALT_I2C_PINS_PHYSICALLY_REVIEWED) && \
    CONFIG_SHAHBAZ_ALT_I2C_PINS_PHYSICALLY_REVIEWED
    return CONFIG_SHAHBAZ_ALT_I2C_EVIDENCE_RECORD_ID[0] != '\0';
#else
    return false;
#endif
}

[[nodiscard]] constexpr auto health_config() noexcept
    -> std::array<shahbaz::health::TaskHealthConfig, shahbaz::health::kTaskCount> {
    using shahbaz::health::TaskHealthConfig;
    return {{
        TaskHealthConfig{true, true, kCriticalServiceMaxSilenceUs},   // SafetySupervisor
        TaskHealthConfig{true, true, kCriticalServiceMaxSilenceUs},   // UsbRx service
        TaskHealthConfig{true, true, kCriticalServiceMaxSilenceUs},   // UsbTx service
        TaskHealthConfig{true, true, kCriticalServiceMaxSilenceUs},   // Command/protocol service
        TaskHealthConfig{true, true, kCriticalServiceMaxSilenceUs},   // SensorScheduler
        TaskHealthConfig{true, false, kCriticalServiceMaxSilenceUs},  // Telemetry service
        TaskHealthConfig{true, false, 1'000'000U},                    // Maintenance/logging
    }};
}

}  // namespace

extern "C" void app_main(void) {
    using namespace shahbaz;

    static platform::EspIdfMonotonicClock clock{};
    static const platform::EspIdfBoardValidator validator{};
    const auto board_report = validator.validate();

    ESP_LOGI(kLogTag, "boot: flash=%" PRIu32 " bytes psram=%" PRIu32 " bytes issues=0x%08" PRIx32,
             board_report.detected_flash_bytes, board_report.detected_psram_bytes,
             board_report.issues);
    log_issue(board_report, interfaces::BoardValidationIssue::BoardRevisionUnverified,
              "exact board revision evidence is not recorded; revision-specific GPIOs stay unused");
    log_issue(board_report, interfaces::BoardValidationIssue::ModuleIdentityUnverified,
              "physical N16R8 module marking evidence is not recorded");
    log_issue(board_report, interfaces::BoardValidationIssue::NativeUsbRouteUnverified,
              "native USB connector routing evidence is not recorded; use the connector wired to GPIO19/GPIO20");
    log_issue(board_report, interfaces::BoardValidationIssue::BoardPowerPathUnverified,
              "exact board 5V/VIN power path is unverified");
    log_issue(board_report, interfaces::BoardValidationIssue::VbusIsolationUnverified,
              "USB VBUS isolation is unverified; do not combine USB host power with external power until verified");
    log_issue(board_report, interfaces::BoardValidationIssue::UsbReverseCurrentUnverified,
              "USB reverse-current/backfeed measurements are unverified");
    log_issue(board_report, interfaces::BoardValidationIssue::UsbIsolatedEnumerationUnverified,
              "USB enumeration with the reviewed VBUS isolation arrangement is unverified");
    log_issue(board_report, interfaces::BoardValidationIssue::UsbEvidenceRecordMissing,
              "consolidated USB/power evidence record is missing");
    log_issue(board_report, interfaces::BoardValidationIssue::SensorHardwareUnverified,
              "sensor wiring evidence is not recorded; runtime I2C diagnostics will determine availability");
    log_issue(board_report, interfaces::BoardValidationIssue::ActuatorEvidenceMissing,
              "actuator GPIO review evidence is missing; physical PWM initialization is blocked");

    static actuator::NullActuatorController active_actuator{};

    const auto heartbeat_timeout_us =
        static_cast<std::uint64_t>(CONFIG_SHAHBAZ_HEARTBEAT_TIMEOUT_MS) * 1'000U;
    static safety::SafetySupervisor safety{active_actuator, safety::SafetyConfig{heartbeat_timeout_us}};
    if (!safety.completeInitialization(clock.now_us())) {
        ESP_LOGE(kLogTag, "safety initialization rejected");
        return;
    }

    const bool invalid_i2c =
        board_report.has(interfaces::BoardValidationIssue::InvalidPinConfiguration);
    const bool fatal_profile_mismatch =
        !board_report.runtime_memory_matches() || invalid_i2c ||
        board_report.has(interfaces::BoardValidationIssue::InvalidActuatorPinConfiguration)
        ;
    if (fatal_profile_mismatch) {
        safety.reportHardwareMismatch();
        ESP_LOGE(kLogTag, "board capability/evidence mismatch latched as FAULT; unsafe peripherals remain blocked");
    }

    ESP_LOGI(kLogTag, "PWM actuators excluded from this production image; sensor/USB functions remain active");

    static platform::EspIdfI2cBus i2c{{CONFIG_SHAHBAZ_I2C_SDA_GPIO,
                                        CONFIG_SHAHBAZ_I2C_SCL_GPIO,
                                        400'000U,
                                        true,
                                        alternate_i2c_reviewed()}};
    interfaces::I2cStatus i2c_status = interfaces::I2cStatus::InvalidArgument;
    if (!invalid_i2c) {
        i2c_status = i2c.initialize();
    }
    if (i2c_status == interfaces::I2cStatus::Ok) {
        ESP_LOGI(kLogTag, "I2C ready: SDA=%d SCL=%d 400kHz",
                 CONFIG_SHAHBAZ_I2C_SDA_GPIO, CONFIG_SHAHBAZ_I2C_SCL_GPIO);
    } else if (invalid_i2c) {
        ESP_LOGE(kLogTag, "I2C initialization blocked by board GPIO policy");
    } else {
        ESP_LOGW(kLogTag, "I2C initial setup failed status=%u; bounded recovery remains authorized only for validated pins",
                 static_cast<unsigned>(i2c_status));
    }

    static usb::EspIdfUsbCdcTransport usb_transport{clock};
    bool usb_ready = false;
#if CONFIG_SHAHBAZ_ENABLE_NATIVE_USB_TRANSPORT
    usb_ready = usb_transport.initialize();
    ESP_LOGI(kLogTag, "native USB CDC initialization: %s", usb_ready ? "ready" : "FAILED");
#else
    ESP_LOGW(kLogTag, "native USB CDC disabled by CONFIG_SHAHBAZ_ENABLE_NATIVE_USB_TRANSPORT");
#endif

    static link::DeviceFrameSender frame_sender{usb_transport, clock};
    static link::SensorTelemetryPublisher telemetry{frame_sender};
    static sensors::scheduler::SharedSensorScheduler sensor_scheduler{i2c, clock, telemetry};
    static command::CommandDispatcher dispatcher{safety, command::DispatcherConfig{}};
    const link::DeviceRuntimeInfo runtime_info{
        board_report.detected_flash_bytes,
        board_report.detected_psram_bytes,
        board_report.issues,
        4U,
        2U,
        0U,
        0U,
        false,
        false,
    };
    static link::ProtocolEngine protocol{dispatcher, safety, active_actuator,
                                         sensor_scheduler, telemetry, frame_sender, clock,
                                         runtime_info};
    static health::TaskHealthMonitor task_health{health_config()};

    const auto watchdog_add_status = esp_task_wdt_add(nullptr);
    const bool watchdog_registered = watchdog_add_status == ESP_OK;
    if (!watchdog_registered) {
        safety.reportWatchdogFailure();
        ESP_LOGE(kLogTag, "failed to subscribe app_main to ESP task watchdog: %s",
                 esp_err_to_name(watchdog_add_status));
    } else {
        ESP_LOGI(kLogTag, "app_main subscribed to ESP task watchdog");
    }

    std::uint32_t handled_connection_epoch = 0U;
    bool protocol_session_admitted = false;
    protocol.setConnected(false);
    static std::array<std::uint8_t, 512U> rx_buffer{};
    std::uint64_t next_log_us = clock.now_us() + kStatusLogPeriodUs;

    for (;;) {
        const auto now_us = clock.now_us();
        const auto observed_connection =
            usb_ready ? usb_transport.connectionSnapshot() : usb::UsbConnectionSnapshot{};
        if (observed_connection.epoch != handled_connection_epoch ||
            observed_connection.connected() != protocol_session_admitted) {
            // Flush TinyUSB FIFOs and drain the epoch-tagged transport queue
            // before admitting a new logical CDC protocol session. The epoch
            // also catches a close/reopen pair between service iterations.
            usb_transport.resetSession();
            protocol.setConnected(false);
            protocol_session_admitted = false;

            // Reset can overlap a TinyUSB callback. Only admit a token if one
            // coherent epoch remains open through token creation and admission.
            const auto candidate = usb_transport.connectionSnapshot();
            if (usb_ready && candidate.connected() &&
                usb_transport.admitSession(candidate.epoch)) {
                protocol.setConnected(true, new_session_token());
                const auto confirmed = usb_transport.connectionSnapshot();
                if (confirmed.epoch == candidate.epoch && confirmed.connected() &&
                    usb_transport.sessionAdmitted(candidate.epoch)) {
                    handled_connection_epoch = confirmed.epoch;
                    protocol_session_admitted = true;
                } else {
                    usb_transport.resetSession();
                    protocol.setConnected(false);
                }
            } else {
                handled_connection_epoch = candidate.epoch;
            }
            ESP_LOGI(kLogTag,
                     "USB connection epoch=%" PRIu32 "; CDC logical session %s; protocol state reset",
                     candidate.epoch,
                     protocol_session_admitted ? "admitted" :
                         (candidate.connected() ? "changed during admission" : "closed"));
        }

        if (usb_ready && protocol_session_admitted) {
            for (;;) {
                const auto before_read = usb_transport.connectionSnapshot();
                if (before_read.epoch != handled_connection_epoch || !before_read.connected() ||
                    !usb_transport.sessionAdmitted(handled_connection_epoch)) {
                    usb_transport.resetSession();
                    protocol.setConnected(false);
                    protocol_session_admitted = false;
                    break;
                }
                const auto received = usb_transport.read({rx_buffer.data(), rx_buffer.size()});
                if (received == 0U) break;

                // A DTR close/open can occur while copying from the stream. Drop
                // that entire chunk unless it still belongs to the admitted epoch.
                const auto before_consume = usb_transport.connectionSnapshot();
                if (before_consume.epoch != handled_connection_epoch ||
                    !before_consume.connected() ||
                    !usb_transport.sessionAdmitted(handled_connection_epoch)) {
                    usb_transport.resetSession();
                    protocol.setConnected(false);
                    protocol_session_admitted = false;
                    break;
                }
                protocol.consume({rx_buffer.data(), received});
            }
        }
        task_health.mark_alive(health::TaskId::UsbRx, now_us);
        task_health.mark_alive(health::TaskId::CommandDispatcher, now_us);

        (void)sensor_scheduler.step();
        task_health.mark_alive(health::TaskId::SensorScheduler, clock.now_us());
        task_health.mark_alive(health::TaskId::TelemetryEncoder, clock.now_us());

        (void)safety.evaluate(clock.now_us());
        task_health.mark_alive(health::TaskId::SafetySupervisor, clock.now_us());

        const auto before_tx = usb_transport.connectionSnapshot();
        if (usb_ready && protocol_session_admitted &&
            before_tx.epoch == handled_connection_epoch && before_tx.connected() &&
            usb_transport.sessionAdmitted(handled_connection_epoch)) {
            usb_transport.serviceTx();
        } else if (protocol_session_admitted) {
            // Do not let replies/telemetry queued by an obsolete epoch cross a
            // rapid DTR reopen before the next service-loop iteration.
            usb_transport.resetSession();
            protocol.setConnected(false);
            protocol_session_admitted = false;
        }
        task_health.mark_alive(health::TaskId::UsbTx, clock.now_us());
        task_health.mark_alive(health::TaskId::Maintenance, clock.now_us());

        const auto health_snapshot = task_health.evaluate(clock.now_us());
        if (!health_snapshot.all_critical_healthy()) {
            safety.reportCriticalTaskFailure();
            ESP_LOGE(kLogTag, "critical runtime service liveness failed mask=0x%08" PRIx32,
                     health_snapshot.critical_unhealthy_mask);
        }

        if (watchdog_registered && esp_task_wdt_reset() != ESP_OK) {
            safety.reportWatchdogFailure();
            ESP_LOGE(kLogTag, "ESP task watchdog reset/feed failed");
        }

        if (now_us >= next_log_us) {
            const auto& sht = sensor_scheduler.sht3x().health();
            const auto& ms = sensor_scheduler.ms5611().health();
            const auto usb_stats = usb_transport.statistics();
            const auto protocol_stats = protocol.statistics();
            ESP_LOGI(kLogTag,
                     "state=%u usb=%u telemetry=%u "
                     "SHT30=%s(err=%u i2c=%u fail=%" PRIu32 " crc=%" PRIu32 ") "
                     "MS5611=%s(err=%u i2c=%u fail=%" PRIu32 " crc=%" PRIu32 ") rx=%" PRIu32
                     " rx_drop=%" PRIu32 " tx_drop=%" PRIu32 " session_reject=%" PRIu32
                     " stale_reject=%" PRIu32,
                     static_cast<unsigned>(safety.state()), protocol_session_admitted ? 1U : 0U,
                     telemetry.enabled() ? 1U : 0U,
                     sht.online ? "online" : "offline",
                     static_cast<unsigned>(sht.last_error),
                     static_cast<unsigned>(sht.last_i2c_status),
                     sht.total_failures, sht.crc_failures,
                     ms.online ? "online" : "offline",
                     static_cast<unsigned>(ms.last_error),
                     static_cast<unsigned>(ms.last_i2c_status),
                     ms.total_failures, ms.crc_failures,
                     usb_stats.rx_bytes, usb_stats.rx_overflow_bytes,
                     usb_stats.tx_frames_dropped + usb_stats.tx_expired,
                     protocol_stats.session_rejects, protocol_stats.freshness_rejects);
            next_log_us = now_us + kStatusLogPeriodUs;
        }
        vTaskDelay(kLoopDelayTicks);
    }
}
