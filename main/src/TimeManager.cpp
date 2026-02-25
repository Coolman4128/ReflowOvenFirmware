#include "TimeManager.hpp"

#include "SettingsManager.hpp"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdlib>
#include <sys/time.h>

namespace {
constexpr uint32_t INITIAL_BACKOFF_MS = 1000;
constexpr uint32_t MAX_BACKOFF_MS = 10 * 60 * 1000;
constexpr uint32_t RESYNC_INTERVAL_MS = 60 * 60 * 1000;
constexpr time_t MIN_VALID_UNIX_EPOCH = 1700000000;
}

TimeManager* TimeManager::instance = nullptr;

TimeManager& TimeManager::getInstance() {
    if (instance == nullptr) {
        instance = new TimeManager();
    }
    return *instance;
}

esp_err_t TimeManager::Initialize() {
    if (initialized) {
        return ESP_OK;
    }

    ApplyTimezone(SettingsManager::getInstance().GetTimeZone());

    BaseType_t result = xTaskCreatePinnedToCore(
        &TimeManager::SyncTaskEntry,
        "TimeSyncTask",
        4096,
        this,
        1,
        &syncTaskHandle,
        0
    );

    if (result != pdPASS) {
        return ESP_FAIL;
    }

    initialized = true;
    return ESP_OK;
}

uint64_t TimeManager::GetCurrentUnixTimeMs() const {
    if (!timeSynced || bootUnixTimeMs == 0) {
        return 0;
    }

    const uint64_t sinceBootMs = static_cast<uint64_t>(esp_timer_get_time() / 1000);
    return bootUnixTimeMs + sinceBootMs;
}

uint64_t TimeManager::GetBootUnixTimeMs() const {
    if (!timeSynced) {
        return 0;
    }
    return bootUnixTimeMs;
}

bool TimeManager::GetLocalTime(std::tm* outLocalTime) const {
    if (outLocalTime == nullptr) {
        return false;
    }

    uint64_t nowMs = GetCurrentUnixTimeMs();
    if (nowMs == 0) {
        return false;
    }

    time_t nowSec = static_cast<time_t>(nowMs / 1000);
    return localtime_r(&nowSec, outLocalTime) != nullptr;
}

esp_err_t TimeManager::SetTimezone(const std::string& tz) {
    if (tz.empty()) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = SettingsManager::getInstance().SetTimeZone(tz);
    if (err != ESP_OK) {
        return err;
    }

    ApplyTimezone(tz);
    return ESP_OK;
}

std::string TimeManager::GetTimezone() const {
    return SettingsManager::getInstance().GetTimeZone();
}

void TimeManager::SyncTaskEntry(void* arg) {
    if (arg == nullptr) {
        vTaskDelete(nullptr);
        return;
    }

    static_cast<TimeManager*>(arg)->SyncTaskLoop();
    vTaskDelete(nullptr);
}

void TimeManager::SyncTaskLoop() {
    uint32_t backoffMs = INITIAL_BACKOFF_MS;

    while (true) {
        if (AttemptSync()) {
            timeSynced = true;
            backoffMs = INITIAL_BACKOFF_MS;
            vTaskDelay(pdMS_TO_TICKS(RESYNC_INTERVAL_MS));
        } else {
            timeSynced = false;
            vTaskDelay(pdMS_TO_TICKS(backoffMs));
            backoffMs = (backoffMs >= MAX_BACKOFF_MS / 2) ? MAX_BACKOFF_MS : backoffMs * 2;
        }
    }
}

bool TimeManager::AttemptSync() {
    ApplyTimezone(SettingsManager::getInstance().GetTimeZone());

    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }

    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.nist.gov");
    esp_sntp_init();

    constexpr int maxWaitCycles = 40;
    for (int i = 0; i < maxWaitCycles; ++i) {
        time_t now = 0;
        time(&now);
        if (now >= MIN_VALID_UNIX_EPOCH) {
            struct timeval tv = {};
            gettimeofday(&tv, nullptr);

            uint64_t nowMs = static_cast<uint64_t>(tv.tv_sec) * 1000ULL + static_cast<uint64_t>(tv.tv_usec / 1000);
            uint64_t sinceBootMs = static_cast<uint64_t>(esp_timer_get_time() / 1000);
            if (nowMs > sinceBootMs) {
                bootUnixTimeMs = nowMs - sinceBootMs;
                return true;
            }
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    return false;
}

void TimeManager::ApplyTimezone(const std::string& tz) const {
    setenv("TZ", tz.c_str(), 1);
    tzset();
}
