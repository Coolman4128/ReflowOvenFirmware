#include "DataManager.hpp"

#include "Controller.hpp"
#include "HardwareManager.hpp"
#include "PID.hpp"
#include "SettingsManager.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

namespace {
class ScopedDataLock {
public:
    explicit ScopedDataLock(SemaphoreHandle_t mutex)
        : mutex_(mutex), locked_(false) {
        if (mutex_ != nullptr) {
            locked_ = (xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE);
        }
    }

    ~ScopedDataLock() {
        if (locked_ && mutex_ != nullptr) {
            xSemaphoreGive(mutex_);
        }
    }

    bool Locked() const { return locked_; }

private:
    SemaphoreHandle_t mutex_;
    bool locked_;
};

std::size_t EstimateDataPoints(std::size_t intervalMs, std::size_t windowMs) {
    if (intervalMs == 0) {
        return 0;
    }
    return windowMs / intervalMs;
}
}

DataManager* DataManager::instance = nullptr;

DataManager& DataManager::getInstance() {
    if (instance == nullptr) {
        instance = new DataManager();
    }
    return *instance;
}

esp_err_t DataManager::LogginOn() {
    {
        ScopedDataLock lock(dataMutex);
        if (LogData) {
            return ESP_ERR_INVALID_STATE;
        }
        LogData = true;
    }

    return StartDataLogLoop();
}

esp_err_t DataManager::LoggingOff() {
    {
        ScopedDataLock lock(dataMutex);
        if (!LogData) {
            return ESP_ERR_INVALID_STATE;
        }
        LogData = false;
    }

    return StopDataLogLoop();
}

esp_err_t DataManager::SetLoggingEnabled(bool enabled) {
    if (enabled == IsLogging()) {
        return ESP_OK;
    }
    if (enabled) {
        return LogginOn();
    }
    return LoggingOff();
}

int DataManager::GetDataLogIntervalMs() const {
    ScopedDataLock lock(dataMutex);
    return DataLogIntervalMs;
}

int DataManager::GetMaxTimeSavedMS() const {
    ScopedDataLock lock(dataMutex);
    return MaxTimeSavedMS;
}

bool DataManager::IsLogging() const {
    ScopedDataLock lock(dataMutex);
    return LogData;
}

std::vector<DataPoint> DataManager::GetRecentData(std::size_t limit) const {
    std::vector<DataPoint> out;

    ScopedDataLock lock(dataMutex);
    if (dataLog.empty()) {
        return out;
    }

    const std::size_t count = dataLog.size();
    const std::size_t take = (limit == 0 || limit > count) ? count : limit;
    const std::size_t startIndex = count - take;

    out.reserve(take);
    for (std::size_t i = startIndex; i < count; ++i) {
        out.push_back(dataLog[i]);
    }
    return out;
}

std::vector<DataPoint> DataManager::GetAllData() const {
    return GetRecentData(0);
}

esp_err_t DataManager::ClearData() {
    ScopedDataLock lock(dataMutex);
    dataLog.clear();
    return ESP_OK;
}

std::size_t DataManager::GetDataPointCount() const {
    ScopedDataLock lock(dataMutex);
    return dataLog.size();
}

std::size_t DataManager::GetStorageBytesUsed() const {
    ScopedDataLock lock(dataMutex);
    return dataLog.size() * sizeof(DataPoint);
}

esp_err_t DataManager::ChangeDataLogInterval(int newIntervalMs) {
    if (newIntervalMs < 250 || newIntervalMs > 10000) {
        return ESP_ERR_INVALID_ARG;
    }

    int currentMaxTimeMs = 0;
    bool currentlyLogging = false;
    {
        ScopedDataLock lock(dataMutex);
        currentMaxTimeMs = MaxTimeSavedMS;
        currentlyLogging = LogData;
    }

    if (EstimateDataPoints(static_cast<std::size_t>(newIntervalMs), static_cast<std::size_t>(currentMaxTimeMs)) > MAX_DATA_POINTS) {
        return ESP_ERR_INVALID_ARG;
    }

    {
        ScopedDataLock lock(dataMutex);
        DataLogIntervalMs = newIntervalMs;
    }

    esp_err_t persistErr = SettingsManager::getInstance().SetDataLogIntervalMs(newIntervalMs);
    if (persistErr != ESP_OK) {
        return persistErr;
    }

    if (currentlyLogging) {
        esp_err_t err = StopDataLogLoop();
        if (err != ESP_OK) {
            return err;
        }
        return StartDataLogLoop();
    }

    return ESP_OK;
}

esp_err_t DataManager::ChangeMaxTimeSaved(int newMaxTimeSavedMs) {
    if (newMaxTimeSavedMs < 1000 * 60 || newMaxTimeSavedMs > 1000 * 60 * 60 * 24) {
        return ESP_ERR_INVALID_ARG;
    }

    int currentIntervalMs = 0;
    bool currentlyLogging = false;
    {
        ScopedDataLock lock(dataMutex);
        currentIntervalMs = DataLogIntervalMs;
        currentlyLogging = LogData;
    }

    if (EstimateDataPoints(static_cast<std::size_t>(currentIntervalMs), static_cast<std::size_t>(newMaxTimeSavedMs)) > MAX_DATA_POINTS) {
        return ESP_ERR_INVALID_ARG;
    }

    {
        ScopedDataLock lock(dataMutex);
        MaxTimeSavedMS = newMaxTimeSavedMs;
    }

    esp_err_t persistErr = SettingsManager::getInstance().SetMaxDataLogTimeMs(newMaxTimeSavedMs);
    if (persistErr != ESP_OK) {
        return persistErr;
    }

    if (currentlyLogging) {
        esp_err_t err = StopDataLogLoop();
        if (err != ESP_OK) {
            return err;
        }
        return StartDataLogLoop();
    }

    return ESP_OK;
}

DataManager::DataManager() {
    dataMutex = xSemaphoreCreateMutex();

    SettingsManager& settings = SettingsManager::getInstance();
    DataLogIntervalMs = settings.GetDataLogIntervalMs();
    MaxTimeSavedMS = settings.GetMaxDataLogTimeMs();

    dataLog.reserve(MAX_DATA_POINTS);
    if (!CheckSettingsValid()) {
        DataLogIntervalMs = 1000;
        MaxTimeSavedMS = 1000 * 60 * 30;
        LogData = true;
    }

    if (LogData) {
        esp_err_t err = StartDataLogLoop();
        if (err != ESP_OK) {
            LogData = false;
        }
    }
}

esp_err_t DataManager::StartDataLogLoop() {
    if (dataLogTaskHandle != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    BaseType_t result;

#if CONFIG_FREERTOS_UNICORE
    result = xTaskCreate(
        dataLogTaskEntry,
        "DataLogTask",
        4096,
        this,
        1,
        &dataLogTaskHandle
    );
#else
    result = xTaskCreatePinnedToCore(
        dataLogTaskEntry,
        "DataLogTask",
        4096,
        this,
        1,
        &dataLogTaskHandle,
        0
    );
#endif

    return (result == pdPASS) ? ESP_OK : ESP_FAIL;
}

esp_err_t DataManager::StopDataLogLoop() {
    if (dataLogTaskHandle == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    TaskHandle_t taskToDelete = dataLogTaskHandle;
    dataLogTaskHandle = nullptr;
    vTaskDelete(taskToDelete);

    return ESP_OK;
}

void DataManager::dataLogTaskEntry(void* arg) {
    DataManager* manager = static_cast<DataManager*>(arg);
    manager->DataLogLoop();
    manager->dataLogTaskHandle = nullptr;
    vTaskDelete(nullptr);
}

esp_err_t DataManager::DataLogLoop() {
    while (IsLogging()) {
        esp_err_t err = LogDataPoint();
        if (err != ESP_OK) {
            ScopedDataLock lock(dataMutex);
            LogData = false;
            return err;
        }

        const int intervalMs = GetDataLogIntervalMs();
        vTaskDelay(pdMS_TO_TICKS(intervalMs));
    }

    return ESP_OK;
}

esp_err_t DataManager::LogDataPoint() {
    DataPoint newDataPoint{};

    newDataPoint.timestamp = static_cast<uint64_t>(esp_timer_get_time() / 1000000);
    newDataPoint.setPoint = static_cast<float>(Controller::getInstance().GetSetPoint());
    newDataPoint.processValue = static_cast<float>(Controller::getInstance().GetProcessValue());
    newDataPoint.PIDOutput = static_cast<float>(Controller::getInstance().GetPIDOutput());

    PID* pid = Controller::getInstance().GetPIDController();
    newDataPoint.PTerm = static_cast<float>(pid->GetPreviousP());
    newDataPoint.ITerm = static_cast<float>(pid->GetPreviousI());
    newDataPoint.DTerm = static_cast<float>(pid->GetPreviousD());

    for (int i = 0; i < 4; i++) {
        newDataPoint.temperatureReadings[i] = static_cast<float>(HardwareManager::getInstance().getThermocoupleValue(i));
    }

    uint8_t relayStates = 0;
    for (int i = 0; i < 6; i++) {
        if (HardwareManager::getInstance().getRelayState(i)) {
            relayStates |= static_cast<uint8_t>(1 << i);
        }
    }

    newDataPoint.relayStates = relayStates;
    newDataPoint.servoAngle = static_cast<uint8_t>(HardwareManager::getInstance().getServoAngle());
    newDataPoint.chamberRunning = Controller::getInstance().IsRunning();

    ScopedDataLock lock(dataMutex);
    if (dataLog.size() >= MAX_DATA_POINTS) {
        dataLog.erase(dataLog.begin());
    }
    dataLog.push_back(newDataPoint);

    return ESP_OK;
}

bool DataManager::CheckSettingsValid() {
    if (DataLogIntervalMs < 250 || DataLogIntervalMs > 10000) {
        return false;
    }
    if (MaxTimeSavedMS < 1000 * 60 || MaxTimeSavedMS > 1000 * 60 * 60 * 24) {
        return false;
    }
    if (EstimateDataPoints(static_cast<std::size_t>(DataLogIntervalMs), static_cast<std::size_t>(MaxTimeSavedMS)) > MAX_DATA_POINTS) {
        return false;
    }
    return true;
}
