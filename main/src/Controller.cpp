#include "Controller.hpp"
#include "HardwareManager.hpp"
#include <algorithm>


// =================================================
// ================ PUBLIC METHODS =================
// =================================================

// This is the constructor for the controller. 
Controller::Controller()
    : pidController(),
      relayPWM(1000, 0.0f, &Controller::RelayOnThunk, &Controller::RelayOffThunk, this)
{
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
    return ESP_OK;
}

esp_err_t Controller::AddInputChannel(int channel) {
    if (std::find(inputsBeingUsed.begin(), inputsBeingUsed.end(), channel) != inputsBeingUsed.end()) {
        return ESP_ERR_INVALID_ARG; // Channel already being used
    }
    inputsBeingUsed.push_back(channel);
    return ESP_OK;
}

esp_err_t Controller::RemoveInputChannel(int channel) {
    auto it = std::find(inputsBeingUsed.begin(), inputsBeingUsed.end(), channel);
    if (it == inputsBeingUsed.end()) {
        return ESP_ERR_INVALID_ARG; // Channel not found
    }
    inputsBeingUsed.erase(it);
    return ESP_OK;
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

}

esp_err_t Controller::PerformOnNotRunning() {
    PIDOutput = 0.0;
    relayPWM.SetDutyCycle(0);
    if (doorOpen){
        HardwareManager::getInstance().setServoAngle(180); // If the door is open, turn the servo to 90 degrees to open the vent
    } else {
        HardwareManager::getInstance().setServoAngle(0); // If the door is closed, turn the servo to 0 degrees to close the vent
    }
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

