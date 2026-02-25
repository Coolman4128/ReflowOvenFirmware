#include "app.hpp"
#include "esp_log.h"
#include "HardwareManager.hpp"
#include "Controller.hpp"
#include <cstdio>

static constexpr int CONTROLLER_TUI_LINES = 12;

static void PrintControllerStateTUI(const Controller& controller)
{
    static bool firstDraw = true;
    if (!firstDraw) {
        std::printf("\033[%dA", CONTROLLER_TUI_LINES);
    }
    std::printf("%s\n", controller.GetStateTUI().c_str());
    std::fflush(stdout);
    firstDraw = false;
}


void app_start()
{
    HardwareManager& manager = HardwareManager::getInstance();
    Controller* controller = new Controller();
    controller->SetSetPoint(30.0); // Set initial setpoint to 30 degrees Celsius
    controller->Start();
     // Initialize hardware
    while(true){
        controller->RunTick();
        PrintControllerStateTUI(*controller);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
