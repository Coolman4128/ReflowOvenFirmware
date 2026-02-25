#include "app.hpp"
#include "esp_log.h"

static const char *TAG = "main";

extern "C" void app_main(void)
{
    app_start();
    ESP_LOGI(TAG, "Reflow oven firmware running");
}
