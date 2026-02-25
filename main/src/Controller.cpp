#include "Controller.hpp"
#include "HardwareManager.hpp"
#include "SettingsManager.hpp"
#include <algorithm>
#include <cstdio>

Controller* Controller::instance = nullptr;


// =================================================
// ================ PUBLIC METHODS =================
// =================================================

Controller& Controller::getInstance() {
    if (instance == nullptr) {
        instance = new Controller();
    }
    return *instance;
}

// This is the constructor for the controller. 
Controller::Controller()
    : pidController(),
      relayPWM(1000, 0.0f, &Controller::RelayOnThunk, &Controller::RelayOffThunk, this)
{
    SettingsManager& settings = SettingsManager::getInstance();
    inputFilterTimeMs = settings.GetInputFilterTime();
    (void)pidController.Tune(settings.GetProportionalGain(), settings.GetIntegralGain(), settings.GetDerivativeGain());
    (void)pidController.SetDerivativeFilterTime(settings.GetDerivativeFilterTime());
    ApplyInputsMask(settings.GetInputsIncludedMask());
    ApplyRelaysPWMMask(settings.GetRelaysPWMMask());
    ApplyRelaysOnMask(settings.GetRelaysOnMask());
}

esp_err_t Controller::RunTick() {
    Perform();

    if (running) {
        return PerformOnRunning();
    } else {
        return PerformOnNotRunning();
    }
}

esp_err_t Controller::Start() {
    // 0. Check if we are are in a state where we cannot start
    if (alarming) {
        return ESP_ERR_INVALID_STATE; // Cannot start if we are in an alarming state
    }

    if (running) {
        return ESP_ERR_INVALID_STATE; // Already running
    }

    // 1. Turn on any relays that should be on when the controller is running
    esp_err_t err = RunningRelaysOn();
    if (err != ESP_OK) {
        return err;
    }

    err = relayPWM.Start();
    if (err != ESP_OK) {
        (void)RunningRelaysOff();
        return err;
    }


    running = true;
    state = "Steady State";
    return ESP_OK;
}

esp_err_t Controller::Stop() {
    // 0. Check if we are already stopped
    if (!running) {
        return ESP_ERR_INVALID_STATE; // Already stopped
    }

    // 1. Turn off any relays that should be on when the controller is running
    esp_err_t err = RunningRelaysOff();
    if (err != ESP_OK) {
        return err;
    }

    (void)relayPWM.SetDutyCycle(0.0f);
    (void)relayPWM.ForceOff();
    err = relayPWM.Stop();
    if (err != ESP_OK) {
        return err;
    }

    running = false;
    state = "Idle";
    return ESP_OK;
}

esp_err_t Controller::OpenDoor() {
    doorOpen = true;
    return ESP_OK;
}

esp_err_t Controller::CloseDoor() {
    doorOpen = false;
    return ESP_OK;
}

esp_err_t Controller::SetSetPoint(double newSetPoint) {
    if (newSetPoint < MIN_SETPOINT || newSetPoint > MAX_SETPOINT) {
        return ESP_ERR_INVALID_ARG; // Setpoint out of range
    }
    setPoint = newSetPoint;
    return ESP_OK;
}

esp_err_t Controller::SetInputFilterTime(double newFilterTimeMs) {
    if (newFilterTimeMs <= 0) {
        return ESP_ERR_INVALID_ARG; // Filter time must be positive
    }
    inputFilterTimeMs = newFilterTimeMs;
    return SettingsManager::getInstance().SetInputFilterTime(newFilterTimeMs);
}

esp_err_t Controller::SetPIDGains(double newKp, double newKi, double newKd) {
    esp_err_t err = pidController.Tune(newKp, newKi, newKd);
    if (err != ESP_OK) {
        return err;
    }

    SettingsManager& settings = SettingsManager::getInstance();
    err = settings.SetProportionalGain(newKp);
    if (err != ESP_OK) {
        return err;
    }
    err = settings.SetIntegralGain(newKi);
    if (err != ESP_OK) {
        return err;
    }
    return settings.SetDerivativeGain(newKd);
}

esp_err_t Controller::SetDerivativeFilterTime(double newFilterTimeSeconds) {
    if (newFilterTimeSeconds < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = pidController.SetDerivativeFilterTime(newFilterTimeSeconds);
    if (err != ESP_OK) {
        return err;
    }
    return SettingsManager::getInstance().SetDerivativeFilterTime(newFilterTimeSeconds);
}

esp_err_t Controller::AddInputChannel(int channel) {
    if (channel < 0 || channel > 7) {
        return ESP_ERR_INVALID_ARG;
    }
    if (std::find(inputsBeingUsed.begin(), inputsBeingUsed.end(), channel) != inputsBeingUsed.end()) {
        return ESP_ERR_INVALID_ARG; // Channel already being used
    }
    inputsBeingUsed.push_back(channel);
    return SettingsManager::getInstance().SetInputsIncludedMask(BuildInputsMask());
}

esp_err_t Controller::RemoveInputChannel(int channel) {
    auto it = std::find(inputsBeingUsed.begin(), inputsBeingUsed.end(), channel);
    if (it == inputsBeingUsed.end()) {
        return ESP_ERR_INVALID_ARG; // Channel not found
    }
    inputsBeingUsed.erase(it);
    return SettingsManager::getInstance().SetInputsIncludedMask(BuildInputsMask());
}

std::string Controller::GetStateTUI() const {
    std::string channels = "-";
    if (!inputsBeingUsed.empty()) {
        channels.clear();
        for (size_t index = 0; index < inputsBeingUsed.size(); ++index) {
            char part[16];
            std::snprintf(part, sizeof(part), "%s%d", (index == 0 ? "" : ","), inputsBeingUsed[index]);
            channels += part;
        }
    }

    const char* runText = running ? "RUN" : "STOP";
    const char* doorText = doorOpen ? "OPEN" : "CLOSED";
    const char* alarmText = alarming ? "YES" : "NO";
    const char* pidMode = PIDOutput > 0 ? "HEAT" : (PIDOutput < 0 ? "VENT" : "HOLD");

    char line1[80];
    char line2[80];
    char line3[80];
    char line4[80];
    char line5[80];
    char line6[80];
    char line7[80];
    char line8[80];
    char line9[80];
    char line10[80];
    char line11[80];
    char line12[80];

    std::snprintf(line1, sizeof(line1), "+---------------------------------------------------------------+");
    std::snprintf(line2, sizeof(line2), "|                    REFLOW CONTROLLER STATUS                   |");
    std::snprintf(line3, sizeof(line3), "+---------------------------------------------------------------+");
    std::snprintf(line4, sizeof(line4), "| Mode:%-6s State:%-16.16s Alarm:%-3s                           |", runText, state.c_str(), alarmText);
    std::snprintf(line5, sizeof(line5), "| Door:%-6s Tick(ms):%-6.0f Filter(ms):%-7.1f                    |", doorText, TICK_INTERVAL_MS, inputFilterTimeMs);
    std::snprintf(line6, sizeof(line6), "| Setpoint:%8.2f  PV:%10.2f  Error:%10.2f                      |", setPoint, processValue, (setPoint - processValue));
    std::snprintf(line7, sizeof(line7), "| PID Out:%9.2f  PID Mode:%-8s                                 |", PIDOutput, pidMode);
    std::snprintf(line8, sizeof(line8), "| Inputs:%3zu  Ch:%-47.47s |", inputsBeingUsed.size(), channels.c_str());
    std::snprintf(line9, sizeof(line9), "| RelayPWM entries:%3zu  Running-relays:%3zu                     |", relaysPWM.size(), relaysWhenControllerRunning.size());
    std::snprintf(line10, sizeof(line10), "| Bounds PV:[%6.1f,%6.1f] SP:[%6.1f,%6.1f]                     |", MIN_PROCESS_VALUE, MAX_PROCESS_VALUE, MIN_SETPOINT, MAX_SETPOINT);
    std::snprintf(line11, sizeof(line11), "| Legend: RUN=active HEAT=relay PWM VENT=servo HOLD=idle PID    |");
    std::snprintf(line12, sizeof(line12), "+---------------------------------------------------------------+");

    std::string tui;
    tui.reserve(1024);
    tui += line1; tui += "\n";
    tui += line2; tui += "\n";
    tui += line3; tui += "\n";
    tui += line4; tui += "\n";
    tui += line5; tui += "\n";
    tui += line6; tui += "\n";
    tui += line7; tui += "\n";
    tui += line8; tui += "\n";
    tui += line9; tui += "\n";
    tui += line10; tui += "\n";
    tui += line11; tui += "\n";
    tui += line12;
    return tui;
}



// =================================================
// =============== PRIVATE METHODS =================
// =================================================

esp_err_t Controller::Perform() {
    // 1. Update the process value from the hardware
    esp_err_t err = UpdateProcessValue();
    if (err != ESP_OK) {
        // Handle error (e.g., log it, set alarming state, etc.)
        return err;
    }
    return ESP_OK;

    // 2. Check if we should be alarming based on the new process value
    bool shouldAlarm = CheckAlarmingConditions();
    if (shouldAlarm && !alarming) {
        alarming = true;
        state = "Alarming";
        Stop();

    } else if (!shouldAlarm && alarming) {
        alarming = false;
        state = "Idle";
    }
}

esp_err_t Controller::PerformOnRunning() {
    PIDOutput = pidController.Calculate(setPoint, processValue);
    if (PIDOutput < 0){

        // If the output is negative, we will control the servo. The more negative, the more we will turn it (up to 180 degrees at -100 or below)
        double angleFromPercent = 180 * std::min(-PIDOutput / 100, 1.0);
        HardwareManager::getInstance().setServoAngle(angleFromPercent);
    }
    if (PIDOutput > 0){
        // If the PID os postive then we will control the relays with PWM. The more positive, the stronger the PWM (up to 100% at 100 or above)
        double pwmValue = std::min(PIDOutput / 100, 1.0);
        relayPWM.SetDutyCycle(pwmValue);
        HardwareManager::getInstance().setServoAngle(0); // If we are using the relays to heat, we want the vent closed
    }

    if (PIDOutput == 0){
        relayPWM.SetDutyCycle(0);
        HardwareManager::getInstance().setServoAngle(0);
    }
    return ESP_OK;
}

esp_err_t Controller::PerformOnNotRunning() {
    PIDOutput = 0.0;
    relayPWM.SetDutyCycle(0);
    if (doorOpen){
        HardwareManager::getInstance().setServoAngle(180); // If the door is open, turn the servo to 90 degrees to open the vent
    } else {
        HardwareManager::getInstance().setServoAngle(0); // If the door is closed, turn the servo to 0 degrees to close the vent
    }
    return ESP_OK;
}

esp_err_t Controller::UpdateProcessValue() {

    double newProcessValue = 0.0;
    int inputsReadCorrectly = 0;
    for (int channel : inputsBeingUsed) {
        double value = HardwareManager::getInstance().getThermocoupleValue(channel);
        if (value == -3000.0) { // Check for error value from thermocouple reading
            continue;
        }
        newProcessValue += value;
        inputsReadCorrectly++;
    }

    if (inputsReadCorrectly == 0) {
        return ESP_ERR_INVALID_STATE; // No valid inputs read, cannot update process value
    }

    processValue = newProcessValue / inputsReadCorrectly; // Average the values from the channels being used

    // Here we will also apply our filtering
    // Simple low pass filter: filteredValue = alpha * newValue + (1 - alpha) * oldValue
    // alpha = dt / (filterTime + dt), where dt is the time between updates
    double dt = TICK_INTERVAL_MS;
    double alpha = dt / (inputFilterTimeMs + dt);
    processValue = alpha * processValue + (1 - alpha) * newProcessValue / inputsReadCorrectly;

    return ESP_OK;
}

bool Controller::CheckAlarmingConditions() {
    if (processValue < MIN_PROCESS_VALUE || processValue > MAX_PROCESS_VALUE) {
        return true;
    }
    return false;
}

esp_err_t Controller::RunningRelaysOn(){
    for (int relayIndex : relaysWhenControllerRunning) {
        esp_err_t err = HardwareManager::getInstance().setRelayState(relayIndex, true);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t Controller::RunningRelaysOff(){
    for (int relayIndex : relaysWhenControllerRunning) {
        esp_err_t err = HardwareManager::getInstance().setRelayState(relayIndex, false);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}


void Controller::RelayOnThunk(void* ctx) {
    if (ctx == nullptr) {
        return;
    }
    static_cast<Controller*>(ctx)->RelayOn();
}

void Controller::RelayOffThunk(void* ctx) {
    if (ctx == nullptr) {
        return;
    }
    static_cast<Controller*>(ctx)->RelayOff();
}

void Controller::RelayOn() {
    for (const auto& [relayIndex, pwmValue] : relaysPWM) {
        if (pwmValue > 0) {
            HardwareManager::getInstance().setRelayState(relayIndex, true);
        }
    }
}

void Controller::RelayOff() {
    for (const auto& [relayIndex, pwmValue] : relaysPWM) {
        HardwareManager::getInstance().setRelayState(relayIndex, false);
    }
}

esp_err_t Controller::AddSetRelayPWM(int relayIndex, double pwmValue) {
    if (relayIndex < 0 || relayIndex > 7) {
        return ESP_ERR_INVALID_ARG;
    }
    if (pwmValue <= 0.0) {
        return RemoveRelayPWM(relayIndex);
    }
    relaysPWM[relayIndex] = pwmValue;
    return SettingsManager::getInstance().SetRelaysPWMMask(BuildRelaysPWMMask());
}

esp_err_t Controller::RemoveRelayPWM(int relayIndex) {
    auto it = relaysPWM.find(relayIndex);
    if (it == relaysPWM.end()) {
        return ESP_ERR_INVALID_ARG;
    }
    relaysPWM.erase(it);
    return SettingsManager::getInstance().SetRelaysPWMMask(BuildRelaysPWMMask());
}

esp_err_t Controller::AddRelayWhenRunning(int relayIndex) {
    if (relayIndex < 0 || relayIndex > 7) {
        return ESP_ERR_INVALID_ARG;
    }
    if (std::find(relaysWhenControllerRunning.begin(), relaysWhenControllerRunning.end(), relayIndex) != relaysWhenControllerRunning.end()) {
        return ESP_ERR_INVALID_ARG;
    }
    relaysWhenControllerRunning.push_back(relayIndex);
    return SettingsManager::getInstance().SetRelaysOnMask(BuildRelaysOnMask());
}

esp_err_t Controller::RemoveRelayWhenRunning(int relayIndex) {
    auto it = std::find(relaysWhenControllerRunning.begin(), relaysWhenControllerRunning.end(), relayIndex);
    if (it == relaysWhenControllerRunning.end()) {
        return ESP_ERR_INVALID_ARG;
    }
    relaysWhenControllerRunning.erase(it);
    return SettingsManager::getInstance().SetRelaysOnMask(BuildRelaysOnMask());
}

uint8_t Controller::BuildInputsMask() const {
    uint8_t mask = 0;
    for (int channel : inputsBeingUsed) {
        if (channel >= 0 && channel <= 7) {
            mask |= static_cast<uint8_t>(1u << channel);
        }
    }
    return mask;
}

uint8_t Controller::BuildRelaysPWMMask() const {
    uint8_t mask = 0;
    for (const auto& [relayIndex, pwmValue] : relaysPWM) {
        if (relayIndex >= 0 && relayIndex <= 7 && pwmValue > 0) {
            mask |= static_cast<uint8_t>(1u << relayIndex);
        }
    }
    return mask;
}

uint8_t Controller::BuildRelaysOnMask() const {
    uint8_t mask = 0;
    for (int relayIndex : relaysWhenControllerRunning) {
        if (relayIndex >= 0 && relayIndex <= 7) {
            mask |= static_cast<uint8_t>(1u << relayIndex);
        }
    }
    return mask;
}

void Controller::ApplyInputsMask(uint8_t mask) {
    inputsBeingUsed.clear();
    for (int channel = 0; channel < 8; ++channel) {
        if ((mask & static_cast<uint8_t>(1u << channel)) != 0) {
            inputsBeingUsed.push_back(channel);
        }
    }
    if (inputsBeingUsed.empty()) {
        inputsBeingUsed.push_back(0);
    }
}

void Controller::ApplyRelaysPWMMask(uint8_t mask) {
    relaysPWM.clear();
    for (int relayIndex = 0; relayIndex < 8; ++relayIndex) {
        if ((mask & static_cast<uint8_t>(1u << relayIndex)) != 0) {
            relaysPWM[relayIndex] = 1.0;
        }
    }
    if (relaysPWM.empty()) {
        relaysPWM[0] = 1.0;
    }
}

void Controller::ApplyRelaysOnMask(uint8_t mask) {
    relaysWhenControllerRunning.clear();
    for (int relayIndex = 0; relayIndex < 8; ++relayIndex) {
        if ((mask & static_cast<uint8_t>(1u << relayIndex)) != 0) {
            relaysWhenControllerRunning.push_back(relayIndex);
        }
    }
}

