#pragma once

#include <cstdint>
#include <vector>
#include <cstdlib>
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

template <typename T>
class PsramAllocator {
public:
    using value_type = T;

    PsramAllocator() noexcept = default;
    template <typename U>
    PsramAllocator(const PsramAllocator<U>&) noexcept {}

    T* allocate(std::size_t n) {
        void* ptr = heap_caps_malloc(n * sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (ptr == nullptr) {
            std::abort();
        }
        return static_cast<T*>(ptr);
    }

    void deallocate(T* ptr, std::size_t) noexcept {
        heap_caps_free(ptr);
    }

    template <typename U>
    bool operator==(const PsramAllocator<U>&) const noexcept { return true; }
    template <typename U>
    bool operator!=(const PsramAllocator<U>&) const noexcept { return false; }
};

struct DataPoint {
    uint64_t timestamp; // The timestamp of the data point in seconds since boot
    float setPoint; // The set point at the time of the data point
    float processValue; // The process value at the time of the data point
    float PIDOutput; // The PID output at the time of the data point
    float PTerm; // The proportional term of the PID output at the time of the data point
    float ITerm; // The integral term of the PID output at the time of the data
    float DTerm; // The derivative term of the PID output at the time of the data point
    float temperatureReadings[4]; // The raw temperature readings from the 4 channels at the time of the data point
    uint8_t relayStates; // The state of the relays at the time of the data point, each bit represents a relay
    uint8_t servoAngle; // The angle of the servo at the time of the data point, from 0 to 180
    bool chamberRunning; // Whether the chamber was running at the time of the data point
};


class DataManager{
    public:
        static DataManager& getInstance();
        esp_err_t LogginOn();
        esp_err_t LoggingOff();
        esp_err_t ChangeDataLogInterval(int newIntervalMs);
        esp_err_t ChangeMaxTimeSaved(int newMaxTimeSavedMs);
        int GetDataLogIntervalMs() const { return DataLogIntervalMs; }
        int GetMaxTimeSavedMS() const { return MaxTimeSavedMS; }
        bool IsLogging() const { return LogData; }


    private:
        static DataManager* instance;
        DataManager();
        constexpr static int MAX_DATA_SIZE_KB = 500; // The max size of the data log in kilobytes.
        constexpr static int MAX_DATA_POINTS = (MAX_DATA_SIZE_KB * 1024) / sizeof(DataPoint); // The max number of data points we can save based on the max data size and the size of each data point

        // Settings
        bool LogData = true; // Whether to log data at all, if false, no data will be logged regardless of other settings
        int DataLogIntervalMs = 1000.0; // How often to log data in milliseconds, 250ms to 10s
        int MaxTimeSavedMS = 1000 * 60 * 30; // How much historical data to save in milliseconds, 1 minute to 24 hours, resets at boot time

        std::vector<DataPoint, PsramAllocator<DataPoint>> dataLog;

        bool CheckSettingsValid();

        TaskHandle_t dataLogTaskHandle = nullptr;
        static void dataLogTaskEntry(void* arg);

        esp_err_t StartDataLogLoop();
        esp_err_t StopDataLogLoop();

        esp_err_t DataLogLoop();
        esp_err_t LogDataPoint();
};