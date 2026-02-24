#include "app.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

extern "C" void app_main(void)
{
    app_start();

    while (true) {
        ESP_LOGI(TAG, "Reflow oven firmware running");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
