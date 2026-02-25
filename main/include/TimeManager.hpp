#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdint>
#include <ctime>
#include <string>

class TimeManager {
public:
    static TimeManager& getInstance();
    TimeManager(const TimeManager&) = delete;
    TimeManager& operator=(const TimeManager&) = delete;
    TimeManager(TimeManager&&) = delete;
    TimeManager& operator=(TimeManager&&) = delete;

    esp_err_t Initialize();
    uint64_t GetCurrentUnixTimeMs() const;
    uint64_t GetBootUnixTimeMs() const;
    bool GetLocalTime(std::tm* outLocalTime) const;
    bool IsSynced() const { return timeSynced; }

    esp_err_t SetTimezone(const std::string& tz);
    std::string GetTimezone() const;

private:
    TimeManager() = default;
    static TimeManager* instance;

    bool initialized = false;
    bool timeSynced = false;
    uint64_t bootUnixTimeMs = 0;
    TaskHandle_t syncTaskHandle = nullptr;

    static void SyncTaskEntry(void* arg);
    void SyncTaskLoop();
    bool AttemptSync();
    void ApplyTimezone(const std::string& tz) const;
};
