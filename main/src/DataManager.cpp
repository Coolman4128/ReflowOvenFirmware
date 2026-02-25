#include "DataManager.hpp"
#include "Controller.hpp"
#include "HardwareManager.hpp"
#include "PID.hpp"
#include "SettingsManager.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

DataManager* DataManager::instance = nullptr;

DataManager& DataManager::getInstance() {
    if (instance == nullptr) {
        instance = new DataManager();
    }
    return *instance;
}

esp_err_t DataManager::LogginOn() {
    if (LogData) {
        return ESP_ERR_INVALID_STATE; // Already logging
    }
    LogData = true;
    return StartDataLogLoop();
}

esp_err_t DataManager::LoggingOff() {
    if (!LogData) {
        return ESP_ERR_INVALID_STATE; // Already not logging
    }
    LogData = false;
    return StopDataLogLoop();
}

esp_err_t DataManager::ChangeDataLogInterval(int newIntervalMs) {
    if (newIntervalMs < 250 || newIntervalMs > 10000) {
        return ESP_ERR_INVALID_ARG; // Data log interval must be between 250ms and 10s
    }
    if ((1000 / newIntervalMs) * MaxTimeSavedMS / 1000 > MAX_DATA_POINTS) {
        return ESP_ERR_INVALID_ARG; // The combination of data log interval and max time saved must not exceed the max data points we can save
    }
    DataLogIntervalMs = newIntervalMs;
    esp_err_t persistErr = SettingsManager::getInstance().SetDataLogIntervalMs(DataLogIntervalMs);
    if (persistErr != ESP_OK) {
        return persistErr;
    }
    if (LogData) {
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
        return ESP_ERR_INVALID_ARG; // Max time saved must be between 1 minute and 24 hours
    }
    if ((1000 / DataLogIntervalMs) * newMaxTimeSavedMs / 1000 > MAX_DATA_POINTS) {
        return ESP_ERR_INVALID_ARG; // The combination of data log interval and max time saved must not exceed the max data points we can save
    }
    MaxTimeSavedMS = newMaxTimeSavedMs;
    esp_err_t persistErr = SettingsManager::getInstance().SetMaxDataLogTimeMs(MaxTimeSavedMS);
    if (persistErr != ESP_OK) {
        return persistErr;
    }
    if (LogData) {
        esp_err_t err = StopDataLogLoop();
        if (err != ESP_OK) {
            return err;
        }
        return StartDataLogLoop();
    }
    return ESP_OK;
}



DataManager::DataManager() {
    // First thing is load in all the settings from the NVS service so we can start using them right away
    SettingsManager& settings = SettingsManager::getInstance();
    DataLogIntervalMs = settings.GetDataLogIntervalMs();
    MaxTimeSavedMS = settings.GetMaxDataLogTimeMs();
    
    dataLog.reserve(MAX_DATA_POINTS); // Reserve the max number of data points in the vector to prevent fragmentation and ensure we never exceed our max data size
    if (!CheckSettingsValid()) {
        // Settings are not valid, reset to defaults
        DataLogIntervalMs = 1000.0;
        MaxTimeSavedMS = 1000 * 60 * 30;
        LogData = true;
    }

    if (LogData) {
        esp_err_t err = StartDataLogLoop();
        if (err != ESP_OK) {
            // Failed to start data log loop, disable logging
            LogData = false;
        }
    }
}

esp_err_t DataManager::StartDataLogLoop() {
    if (dataLogTaskHandle != nullptr) {
        return ESP_ERR_INVALID_STATE; // Task already running
    }

    BaseType_t result;

    #if CONFIG_FREERTOS_UNICORE
        result = xTaskCreate(
            dataLogTaskEntry,
            "DataLogTask",
            4096, // Stack size in words
            this,
            1, // Priority
            &dataLogTaskHandle
        );
    #else
        result = xTaskCreatePinnedToCore(
            dataLogTaskEntry,
            "DataLogTask",
            4096, // Stack size in words
            this,
            1, // Priority
            &dataLogTaskHandle,
            0 // Run on core 0
        );
    #endif

    return (result == pdPASS) ? ESP_OK : ESP_FAIL;
}

esp_err_t DataManager::StopDataLogLoop() {
    if (dataLogTaskHandle == nullptr) {
        return ESP_ERR_INVALID_STATE; // Task not running
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
    while (LogData) {
        esp_err_t err = LogDataPoint();
        if (err != ESP_OK) {
            // Failed to log data point, disable logging to prevent further errors
            LogData = false;
            return err;
        }
        vTaskDelay(pdMS_TO_TICKS(DataLogIntervalMs));
    }
    return ESP_OK;
}

esp_err_t DataManager::LogDataPoint() {
    DataPoint newDataPoint;
    // Fill out here
    newDataPoint.timestamp = static_cast<uint64_t>(esp_timer_get_time() / 1000000);
    newDataPoint.setPoint = Controller::getInstance().GetSetPoint();
    newDataPoint.processValue = Controller::getInstance().GetProcessValue();
    newDataPoint.PIDOutput = Controller::getInstance().GetPIDOutput();
    newDataPoint.PTerm = Controller::getInstance().GetPIDController()->GetPreviousP();
    newDataPoint.ITerm = Controller::getInstance().GetPIDController()->GetPreviousI();
    newDataPoint.DTerm = Controller::getInstance().GetPIDController()->GetPreviousD();

    for (int i = 0; i < 4; i++) {
        newDataPoint.temperatureReadings[i] = HardwareManager::getInstance().getThermocoupleValue(i);
    }

    uint8_t relayStates = 0;
    for (int i = 0; i < 6; i++) {
        if (HardwareManager::getInstance().getRelayState(i)) {
            relayStates |= (1 << i);
        }
    }
    newDataPoint.relayStates = relayStates;
    newDataPoint.servoAngle = static_cast<uint8_t>(HardwareManager::getInstance().getServoAngle());
    newDataPoint.chamberRunning = Controller::getInstance().IsRunning();




    // Add the new data point to the log
    if (dataLog.size() + 1 > dataLog.capacity()) {
        // we have reacted our max data size, need to remove the first data point to make room for the new one
        dataLog.erase(dataLog.begin());
    }
    dataLog.push_back(newDataPoint);
    return ESP_OK;
}

bool DataManager::CheckSettingsValid() {
    if (DataLogIntervalMs < 250 || DataLogIntervalMs > 10000) {
        return false; // Data log interval must be between 250ms and 10s
    }
    if (MaxTimeSavedMS < 1000 * 60 || MaxTimeSavedMS > 1000 * 60 * 60 * 24) {
        return false; // Max time saved must be between 1 minute and 24 hours
    }
    if ((1000 / DataLogIntervalMs) * MaxTimeSavedMS / 1000 > MAX_DATA_POINTS) {
        return false; // The combination of data log interval and max time saved must not exceed the max data points we can save
    }
    return true;
}