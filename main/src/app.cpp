#include "app.hpp"

#include "Controller.hpp"
#include "DataManager.hpp"
#include "HardwareManager.hpp"
#include "SettingsManager.hpp"
#include "TimeManager.hpp"
#include "WebServerManager.hpp"
#include "WiFiManager.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr const char* TAG = "app";
constexpr uint32_t CONTROLLER_TICK_MS = 250;
TaskHandle_t controllerTaskHandle = nullptr;

void ControllerTaskEntry(void* /*arg*/) {
    Controller& controller = Controller::getInstance();
    while (true) {
        (void)controller.RunTick();
        vTaskDelay(pdMS_TO_TICKS(CONTROLLER_TICK_MS));
    }
}

esp_err_t StartControllerTask() {
    if (controllerTaskHandle != nullptr) {
        return ESP_OK;
    }

    BaseType_t result;
#if CONFIG_FREERTOS_UNICORE
    result = xTaskCreate(
        &ControllerTaskEntry,
        "ControllerTask",
        4096,
        nullptr,
        2,
        &controllerTaskHandle
    );
#else
    result = xTaskCreatePinnedToCore(
        &ControllerTaskEntry,
        "ControllerTask",
        4096,
        nullptr,
        2,
        &controllerTaskHandle,
        1
    );
#endif

    return (result == pdPASS) ? ESP_OK : ESP_FAIL;
}
}

void app_start()
{
    SettingsManager& settings = SettingsManager::getInstance();
    ESP_ERROR_CHECK(settings.Initialize());

    WiFiManager& wifiManager = WiFiManager::getInstance();
    ESP_ERROR_CHECK(wifiManager.Initialize());
    (void)wifiManager.ConnectToSavedNetwork();

    TimeManager& timeManager = TimeManager::getInstance();
    ESP_ERROR_CHECK(timeManager.Initialize());

    (void)HardwareManager::getInstance();
    (void)Controller::getInstance();
    (void)DataManager::getInstance();

    ESP_ERROR_CHECK(StartControllerTask());
    ESP_ERROR_CHECK(WebServerManager::getInstance().Initialize());

    ESP_LOGI(TAG, "Application startup complete");
}
