#include "HardwareManager.hpp"
#include <cstring>
#include <algorithm>
#include <cstdint>

HardwareManager* HardwareManager::instance = nullptr;


// =================================================
// ================ PUBLIC METHODS =================
// =================================================

HardwareManager& HardwareManager::getInstance() {
    if (instance == nullptr) {
        instance = new HardwareManager();
    }
    return *instance;
}

double HardwareManager::getThermocoupleValue(int index) {
    if (index < 0 || index >= NUM_THERMOCOUPLES) {
        return THERMOCOUPLE_ERROR_VALUE; // Return error value for out of bounds index
    }
    return thermocoupleValues[index];
}

esp_err_t HardwareManager::setRelayState(int relayIndex, bool state) {
    if (relayIndex < 0 || relayIndex >= NUM_RELAYS) {
        return ESP_ERR_INVALID_ARG; // Invalid relay index
    }
    gpio_set_level((gpio_num_t) RELAY_GPIO_PINS[relayIndex], state ? 1 : 0);
    relayStates[relayIndex] = state;
    return ESP_OK;
}

bool HardwareManager::getRelayState(int relayIndex) {
    if (relayIndex < 0 || relayIndex >= NUM_RELAYS) {
        return false; // Invalid relay index, return false as default
    }
    return relayStates[relayIndex];
}

esp_err_t HardwareManager::setServoAngle(double angle) {
    double clampedAngle = std::clamp(angle, static_cast<double>(SERVO_MIN_ANGLE), static_cast<double>(SERVO_MAX_ANGLE));
    double angleRange = SERVO_MAX_ANGLE - SERVO_MIN_ANGLE;
    double pulseRange = SERVO_MAX_PULSE_WIDTH_US - SERVO_MIN_PULSE_WIDTH_US;
    double pulseWidth = SERVO_MIN_PULSE_WIDTH_US + (clampedAngle - SERVO_MIN_ANGLE) * (pulseRange / angleRange);

    esp_err_t err = mcpwm_comparator_set_compare_value(servoComparator, static_cast<uint32_t>(pulseWidth));
    if (err != ESP_OK) {
        return err;
    }

    servoAngle = clampedAngle;
    return ESP_OK;
}

double HardwareManager::getServoAngle() {
    return servoAngle;
}



// =================================================
// ================ PRIVATE METHODS ================
// =================================================

HardwareManager::HardwareManager() {
    initializeHardware();
}

esp_err_t HardwareManager::initializeHardware() {

    // ====== THERMOCOUPLE SET UP =========
    // 1. Set up the array that hold the temperature
    thermocoupleValues.resize(NUM_THERMOCOUPLES);
    for (int i = 0; i < NUM_THERMOCOUPLES; i++) {
        thermocoupleValues[i] = THERMOCOUPLE_ERROR_VALUE;
    }

    // 2. Set up the SPI bus for thermocouples
    esp_err_t err = thermocoupleSPISetup();
    if (err != ESP_OK) {
        return err;
    }

    // 3. Start the task that will read the thermocouples
    esp_err_t taskErr = startThermocoupleReadTask();
    if (taskErr != ESP_OK) {
        return taskErr;
    }

    // ======= RELAY SET UP =========
    for (int i = 0; i < NUM_RELAYS; i++) {
        relayStates.push_back(false); // Initialize all relays to off
    }
    err = relaySetup();
    if (err != ESP_OK) {
        return err;
    }

    // ======= SERVO SET UP =========
    servoAngle = SERVO_MIN_ANGLE; // Start at min angle
    err = servoSetup();
    if (err != ESP_OK) {
        return err;
    }

    return ESP_OK;
}

esp_err_t HardwareManager::relaySetup() {
    if (NUM_RELAYS <= 0) {
        return ESP_OK; // No relays to setup
    }
    for (int i = 0; i < NUM_RELAYS; i++) {
        gpio_config_t io_conf = {};
        io_conf.pin_bit_mask = (1ULL << RELAY_GPIO_PINS[i]);
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type = GPIO_INTR_DISABLE;

        esp_err_t err = gpio_config(&io_conf);
        if (err != ESP_OK) {
            return err;
        }

        gpio_set_level((gpio_num_t) RELAY_GPIO_PINS[i], 0); // Start with all relays off
    }
    return ESP_OK;
}

esp_err_t HardwareManager::servoSetup() {
    if (servoTimer != nullptr) {
        return ESP_OK; // Servo already set up
    }

    // Create timer for the servo
    mcpwm_timer_config_t timerConfig {};
    timerConfig.group_id = 0; // Use group 0
    timerConfig.clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT;
    timerConfig.resolution_hz = 1000000; // 1 tick = 1us
    timerConfig.period_ticks = SERVO_PERIOD_US; // 20ms period for 50Hz servo PWM
    timerConfig.count_mode = MCPWM_TIMER_COUNT_MODE_UP;

    esp_err_t err = mcpwm_new_timer(&timerConfig, &servoTimer);
    if (err != ESP_OK) {
        return err;
    }

    // Create operator for the servo
    mcpwm_operator_config_t operatorConfig {};
    operatorConfig.group_id = 0; // Use group 0
    err = mcpwm_new_operator(&operatorConfig, &servoOperator);
    if (err != ESP_OK) {
        return err;
    }

    err = mcpwm_operator_connect_timer(servoOperator, servoTimer);
    if (err != ESP_OK) {
        return err;
    }

    // Create comparator for the servo
    mcpwm_comparator_config_t comparatorConfig {};
    comparatorConfig.flags.update_cmp_on_tez = true;
    err = mcpwm_new_comparator(servoOperator, &comparatorConfig, &servoComparator);
    if (err != ESP_OK) {
        return err;
    }

    // Create generator for the servo
    mcpwm_generator_config_t genConfig {};
    genConfig.gen_gpio_num = SERVO_GPIO_PIN;

    err = mcpwm_new_generator(servoOperator, &genConfig, &servoGenerator);
    if (err != ESP_OK) {
        return err;
    }

    // Set up events
    err = mcpwm_generator_set_action_on_timer_event(servoGenerator, MCPWM_GEN_TIMER_EVENT_ACTION(
        MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH));
    if (err != ESP_OK) {
        return err;
    }

    err = mcpwm_generator_set_action_on_compare_event(servoGenerator, MCPWM_GEN_COMPARE_EVENT_ACTION(
        MCPWM_TIMER_DIRECTION_UP, servoComparator, MCPWM_GEN_ACTION_LOW));
    if (err != ESP_OK) {
        return err;
    }

    // Set to min angle to start
    err = setServoAngle(SERVO_MIN_ANGLE);
    if (err != ESP_OK) {
        return err;
    }

    // Start the timer
    err = mcpwm_timer_enable(servoTimer);
    if (err != ESP_OK) {
        return err;
    }
    err = mcpwm_timer_start_stop(servoTimer, MCPWM_TIMER_START_NO_STOP);
    if (err != ESP_OK) {
        return err;
    }

    return ESP_OK;
}

// This function configures the SPI bus and adds the thermocouple devices to it. It should be called during initialization.
esp_err_t HardwareManager::thermocoupleSPISetup() {
    if (!spiDevices.empty()) {
        return ESP_OK; // SPI already initialized
    }

    esp_err_t err = ESP_OK;
    // Configure the SPI bus for the thermocouples (TODO, THIS MAY NEED CHANGED IN THE FUTURE WHEN A SCREEN IS INVOLVED)
    spi_bus_config_t busConfig {};
    busConfig.mosi_io_num = -1; // We do not need MOSI for thermocouple readings
    busConfig.miso_io_num = THERMOCOUPLE_SPI_SO_PIN;
    busConfig.sclk_io_num = THERMOCOUPLE_SPI_SCK_PIN;
    busConfig.quadwp_io_num = -1;
    busConfig.quadhd_io_num = -1; 

    // Initialize the SPI bus using the settings we configured and return an error if it fails
    err = spi_bus_initialize(spiHost, &busConfig, dmaChannel);
    if (err != ESP_OK) {
        return err;
    }

    // For each thermocouple, configure it's device struct and add it to the SPI bus, returning an error if any of the additions fail
    for (int i = 0; i < NUM_THERMOCOUPLES; i++) {
        spi_device_interface_config_t deviceConfig {};
        deviceConfig.clock_speed_hz = clockSpeedHz;
        deviceConfig.mode = 0; // SPI mode 0
        deviceConfig.spics_io_num = THERMOCOUPLE_SPI_CS_PINS[i];
        deviceConfig.queue_size = 1;
        deviceConfig.flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY;

        spi_device_handle_t spiDevice;
        err = spi_bus_add_device(spiHost, &deviceConfig, &spiDevice);
        if (err != ESP_OK) {
            return err;
        }
        spiDevices.push_back(spiDevice);
    }
    return ESP_OK;
}

esp_err_t HardwareManager::startThermocoupleReadTask(){
    if (spiDevices.empty()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (thermocoupleReadTaskHandle != nullptr) {
        return ESP_ERR_INVALID_STATE; // Task already running
    }

    BaseType_t result;

    #if CONFIG_FREERTOS_UNICORE
        result = xTaskCreate(
            taskEntry,
            "ThermocoupleReadTask",
            2048, // Stack size in words
            this,
            1, // Priority
            &thermocoupleReadTaskHandle
        );
    #else
        result = xTaskCreatePinnedToCore(
            taskEntry,
            "ThermocoupleReadTask",
            2048, // Stack size in words
            this,
            1, // Priority
            &thermocoupleReadTaskHandle,
            0 // Run on core 0
        );
    #endif

    return (result == pdPASS) ? ESP_OK : ESP_FAIL;
}

void HardwareManager::taskEntry(void* arg) {
    HardwareManager* manager = static_cast<HardwareManager*>(arg);
    manager->readLoop();
}

void HardwareManager::readLoop() {
    const TickType_t period = pdMS_TO_TICKS(THERMOCOUPLE_READ_INTERVAL_MS);
    TickType_t last_wake_time = xTaskGetTickCount();

    while (true){
        readThermocouples();
        vTaskDelayUntil(&last_wake_time, period);
    }
}

// This function will read the temps of the thermocouples in C and place them in the vector
esp_err_t HardwareManager::readThermocouples() {
    if (spiDevices.empty()) {
        return ESP_ERR_INVALID_STATE;
    }

    for (int i = 0; i < NUM_THERMOCOUPLES; i++) {
        // Read the data from the thermocouple
        spi_transaction_t t{};
        std::memset(&t, 0, sizeof(t));
        t.flags = SPI_TRANS_USE_RXDATA;
        t.rxlength = 16; // We read 16 bits of data
        t.length = 0; // No TX

        esp_err_t err = spi_device_transmit(spiDevices[i], &t);
        if (err != ESP_OK) {
            thermocoupleValues[i] = THERMOCOUPLE_ERROR_VALUE; // Set to error value on failure
            // TODO: print some error to the console here to specifiy that it was an SPI error, not an error state from the MAX6675
            continue;
        }

        // The data comes in as a MSB-first 16-bit word
        uint16_t rawData = (static_cast<uint16_t>(t.rx_data[0]) << 8) | t.rx_data[1];

        // Check bit 2 for thermocouple error (open circuit)
        if (rawData & (1u << 2)){
            thermocoupleValues[i] = THERMOCOUPLE_ERROR_VALUE; // Set to error value on open circuit
            continue;
        }

        // Bits 14:3 contain the temperature data, with bit 14 as the sign bit. Shift right by 3 to get the value and then multiply by 0.25 to get the temp in C
        uint16_t tempData = (rawData >> 3) & 0x0FFF; // Get bits 14:3
        double temperature = tempData * 0.25; // Each bit represents 0.25 degrees C
        thermocoupleValues[i] = temperature;
    }
    return ESP_OK;
}