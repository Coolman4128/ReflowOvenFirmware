#pragma once

#include <vector>
#include "esp_err.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/mcpwm_prelude.h"

class HardwareManager {
public:
    static HardwareManager& getInstance();
    double getThermocoupleValue(int index);
    esp_err_t setRelayState(int relayIndex, bool state);
    bool getRelayState(int relayIndex);
    esp_err_t setServoAngle(double angle);
    double getServoAngle();

private:
    // Hardware configuration constants (CHANGE THESE FOR DIFFERENT HARDWARE SETUP)
    static constexpr int NUM_RELAYS = 6;
    static constexpr int RELAY_GPIO_PINS[NUM_RELAYS] = {9, 10, 11, 12, 13, 14};

    static constexpr int NUM_THERMOCOUPLES = 4;
    static constexpr int THERMOCOUPLE_SPI_SCK_PIN = 15;
    static constexpr int THERMOCOUPLE_SPI_SO_PIN = 16;
    static constexpr int THERMOCOUPLE_SPI_CS_PINS[NUM_THERMOCOUPLES] = {4, 5, 6, 7};
    static constexpr double THERMOCOUPLE_ERROR_VALUE = -3000.0;
    static constexpr int THERMOCOUPLE_READ_INTERVAL_MS = 220;

    static constexpr int SERVO_GPIO_PIN = 8;
    static constexpr int SERVO_PERIOD_US = 20000; // Standard servo period is 20ms (20,000 microseconds) or 50hz frequency
    static constexpr int SERVO_MIN_PULSE_WIDTH_US = 1000; // Minimum pulse width 
    static constexpr int SERVO_MAX_PULSE_WIDTH_US = 2000; // Maximum pulse width 

    static constexpr int SERVO_MIN_ANGLE = 0;
    static constexpr int SERVO_MAX_ANGLE = 180;

    // SPI Bus settings
    spi_host_device_t spiHost = SPI2_HOST;
    int clockSpeedHz = 4000000; // 4 MHz
    spi_dma_chan_t dmaChannel = SPI_DMA_CH_AUTO;

    // SPI device handles for the thermocouples
    std::vector<spi_device_handle_t> spiDevices;

    // Task handle for the thermocouple reading task and helper functions for it
    TaskHandle_t thermocoupleReadTaskHandle = nullptr;
    static void taskEntry(void* arg);
    void readLoop();

    // Hardware Level Servo properties
    mcpwm_timer_handle_t servoTimer = nullptr;
    mcpwm_oper_handle_t servoOperator = nullptr;
    mcpwm_cmpr_handle_t servoComparator = nullptr;
    mcpwm_gen_handle_t servoGenerator = nullptr;


    // Singleton instance
    static HardwareManager* instance;

    // Array of current thermocouple values, indexed the same as the SPI device handles
    std::vector<double> thermocoupleValues;

    // Relay states, indexed the same as the RELAY_GPIO_PINS array
    std::vector<bool> relayStates;

    double servoAngle = 0;

    HardwareManager();
    esp_err_t initializeHardware();
    esp_err_t thermocoupleSPISetup();
    esp_err_t relaySetup();
    esp_err_t servoSetup();
    esp_err_t startThermocoupleReadTask();
    esp_err_t readThermocouples();
};
