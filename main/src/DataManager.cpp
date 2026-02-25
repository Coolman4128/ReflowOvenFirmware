#include "DataManager.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

    // TO DO: Load settings from NVS, for now just use defaults
    
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
    // TO DO: Start a FreeRTOS task that runs the DataLogLoop function at the specified interval
    return ESP_OK;
}

esp_err_t DataManager::StopDataLogLoop() {
    // TO DO: Stop the FreeRTOS task that is running the DataLogLoop function
    return ESP_OK;
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
    
    // TO DO: Read data and fill out the struct
    DataPoint newDataPoint;
    // Fill out here

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