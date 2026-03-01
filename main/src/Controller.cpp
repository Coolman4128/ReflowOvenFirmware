#include "Controller.hpp"

#include "HardwareManager.hpp"
#include "SettingsManager.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

Controller* Controller::instance = nullptr;

namespace {
class ScopedLock {
public:
    explicit ScopedLock(SemaphoreHandle_t mutex)
        : mutex_(mutex), locked_(false) {
        if (mutex_ != nullptr) {
            locked_ = (xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE);
        }
    }

    ~ScopedLock() {
        if (locked_ && mutex_ != nullptr) {
            xSemaphoreGive(mutex_);
        }
    }

    bool Locked() const { return locked_; }

private:
    SemaphoreHandle_t mutex_;
    bool locked_;
};
}

// =================================================
// ================ PUBLIC METHODS =================
// =================================================

Controller& Controller::getInstance() {
    if (instance == nullptr) {
        instance = new Controller();
    }
    return *instance;
}

Controller::Controller()
    : pidController(),
      relayPWM(1000, 0.0f, &Controller::RelayOnThunk, &Controller::RelayOffThunk, this)
{
    stateMutex = xSemaphoreCreateMutex();

    SettingsManager& settings = SettingsManager::getInstance();
    inputFilterTimeMs = settings.GetInputFilterTime();
    (void)pidController.Tune(settings.GetProportionalGain(), settings.GetIntegralGain(), settings.GetDerivativeGain());
    (void)pidController.SetDerivativeFilterTime(settings.GetDerivativeFilterTime());
    (void)pidController.SetSetpointWeight(settings.GetSetpointWeight());
    ApplyInputsMask(settings.GetInputsIncludedMask());
    ApplyRelaysPWMMask(settings.GetRelaysPWMMask());
    const std::array<double, 8> relayWeights = settings.GetRelayPWMWeights();
    for (auto& entry : relaysPWM) {
        if (entry.first >= 0 && entry.first < 8) {
            entry.second = std::clamp(relayWeights[static_cast<std::size_t>(entry.first)], 0.0, 1.0);
        }
    }
    SyncRelayPWMAccumulatorsLocked();
    ApplyRelaysOnMask(settings.GetRelaysOnMask());
    doorClosedAngleDeg = std::clamp(settings.GetDoorClosedAngleDeg(), 0.0, 180.0);
    doorOpenAngleDeg = std::clamp(settings.GetDoorOpenAngleDeg(), 0.0, 180.0);
    doorMaxSpeedDegPerSec = std::clamp(settings.GetDoorMaxSpeedDegPerSec(), 1.0, 360.0);
    doorPreviewAngleDeg = doorOpenAngleDeg;
}

double Controller::GetSetPoint() const {
    ScopedLock lock(stateMutex);
    return setPoint;
}

double Controller::GetProcessValue() const {
    ScopedLock lock(stateMutex);
    return processValue;
}

std::string Controller::GetState() const {
    ScopedLock lock(stateMutex);
    return state;
}

double Controller::GetPIDOutput() const {
    ScopedLock lock(stateMutex);
    return PIDOutput;
}

bool Controller::IsRunning() const {
    ScopedLock lock(stateMutex);
    return running;
}

bool Controller::IsDoorOpen() const {
    ScopedLock lock(stateMutex);
    return doorOpen;
}

bool Controller::IsAlarming() const {
    ScopedLock lock(stateMutex);
    return alarming;
}

bool Controller::IsSetpointLockedByProfile() const {
    ScopedLock lock(stateMutex);
    return setpointLockedByProfile;
}

double Controller::GetInputFilterTimeMs() const {
    ScopedLock lock(stateMutex);
    return inputFilterTimeMs;
}

std::vector<int> Controller::GetInputChannels() const {
    ScopedLock lock(stateMutex);
    return inputsBeingUsed;
}

std::vector<int> Controller::GetRelaysPWMEnabled() const {
    ScopedLock lock(stateMutex);
    std::vector<int> relays;
    relays.reserve(relaysPWM.size());
    for (const auto& entry : relaysPWM) {
        if (entry.first >= 0 && entry.first <= 7) {
            relays.push_back(entry.first);
        }
    }
    std::sort(relays.begin(), relays.end());
    return relays;
}

std::unordered_map<int, double> Controller::GetRelaysPWMWeights() const {
    ScopedLock lock(stateMutex);
    return relaysPWM;
}

std::vector<int> Controller::GetRelaysWhenRunning() const {
    ScopedLock lock(stateMutex);
    return relaysWhenControllerRunning;
}

double Controller::GetDoorClosedAngleDeg() const {
    ScopedLock lock(stateMutex);
    return doorClosedAngleDeg;
}

double Controller::GetDoorOpenAngleDeg() const {
    ScopedLock lock(stateMutex);
    return doorOpenAngleDeg;
}

double Controller::GetDoorMaxSpeedDegPerSec() const {
    ScopedLock lock(stateMutex);
    return doorMaxSpeedDegPerSec;
}

esp_err_t Controller::RunTick() {
    esp_err_t err = Perform();
    if (err != ESP_OK) {
        return err;
    }

    bool isRunning = false;
    {
        ScopedLock lock(stateMutex);
        isRunning = running;
    }

    if (isRunning) {
        return PerformOnRunning();
    }
    return PerformOnNotRunning();
}

esp_err_t Controller::Start() {
    bool isAlarming = false;
    bool isRunning = false;
    {
        ScopedLock lock(stateMutex);
        isAlarming = alarming;
        isRunning = running;
    }

    if (isAlarming || isRunning) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = RunningRelaysOn();
    if (err != ESP_OK) {
        return err;
    }

    err = relayPWM.Start();
    if (err != ESP_OK) {
        (void)RunningRelaysOff();
        return err;
    }

    {
        ScopedLock lock(stateMutex);
        running = true;
        doorPreviewActive = false;
        state = "Steady State";
    }

    return ESP_OK;
}

esp_err_t Controller::Stop() {
    bool isRunning = false;
    {
        ScopedLock lock(stateMutex);
        isRunning = running;
    }

    if (!isRunning) {
        return ESP_ERR_INVALID_STATE;
    }

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

    {
        ScopedLock lock(stateMutex);
        running = false;
        state = "Idle";
        PIDOutput = 0.0;
    }

    return ESP_OK;
}

esp_err_t Controller::OpenDoor() {
    ScopedLock lock(stateMutex);
    if (running) {
        return ESP_ERR_INVALID_STATE;
    }
    doorOpen = true;
    doorPreviewActive = false;
    return ESP_OK;
}

esp_err_t Controller::CloseDoor() {
    ScopedLock lock(stateMutex);
    if (running) {
        return ESP_ERR_INVALID_STATE;
    }
    doorOpen = false;
    doorPreviewActive = false;
    return ESP_OK;
}

esp_err_t Controller::SetSetPoint(double newSetPoint) {
    if (newSetPoint < MIN_SETPOINT || newSetPoint > MAX_SETPOINT) {
        return ESP_ERR_INVALID_ARG;
    }

    ScopedLock lock(stateMutex);
    if (setpointLockedByProfile) {
        return ESP_ERR_INVALID_STATE;
    }
    setPoint = newSetPoint;
    return ESP_OK;
}

esp_err_t Controller::SetSetPointFromProfile(double newSetPoint) {
    if (newSetPoint < MIN_SETPOINT || newSetPoint > MAX_SETPOINT) {
        return ESP_ERR_INVALID_ARG;
    }

    ScopedLock lock(stateMutex);
    setPoint = newSetPoint;
    return ESP_OK;
}

void Controller::SetProfileSetpointLock(bool locked) {
    ScopedLock lockGuard(stateMutex);
    setpointLockedByProfile = locked;
}

esp_err_t Controller::SetInputFilterTime(double newFilterTimeMs) {
    if (newFilterTimeMs <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    {
        ScopedLock lock(stateMutex);
        inputFilterTimeMs = newFilterTimeMs;
    }

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

esp_err_t Controller::SetSetpointWeight(double newWeight) {
    esp_err_t err = pidController.SetSetpointWeight(newWeight);
    if (err != ESP_OK) {
        return err;
    }

    return SettingsManager::getInstance().SetSetpointWeight(newWeight);
}

esp_err_t Controller::AddInputChannel(int channel) {
    if (channel < 0 || channel > 7) {
        return ESP_ERR_INVALID_ARG;
    }

    {
        ScopedLock lock(stateMutex);
        if (std::find(inputsBeingUsed.begin(), inputsBeingUsed.end(), channel) != inputsBeingUsed.end()) {
            return ESP_ERR_INVALID_ARG;
        }
        inputsBeingUsed.push_back(channel);
    }

    return SettingsManager::getInstance().SetInputsIncludedMask(BuildInputsMask());
}

esp_err_t Controller::RemoveInputChannel(int channel) {
    {
        ScopedLock lock(stateMutex);
        auto it = std::find(inputsBeingUsed.begin(), inputsBeingUsed.end(), channel);
        if (it == inputsBeingUsed.end()) {
            return ESP_ERR_INVALID_ARG;
        }
        inputsBeingUsed.erase(it);
        if (inputsBeingUsed.empty()) {
            inputsBeingUsed.push_back(0);
        }
    }

    return SettingsManager::getInstance().SetInputsIncludedMask(BuildInputsMask());
}

esp_err_t Controller::SetInputChannels(const std::vector<int>& channels) {
    if (channels.empty()) {
        return ESP_ERR_INVALID_ARG;
    }

    std::vector<int> sanitized;
    sanitized.reserve(channels.size());
    for (int channel : channels) {
        if (channel < 0 || channel > 7) {
            return ESP_ERR_INVALID_ARG;
        }
        if (std::find(sanitized.begin(), sanitized.end(), channel) == sanitized.end()) {
            sanitized.push_back(channel);
        }
    }

    {
        ScopedLock lock(stateMutex);
        inputsBeingUsed = sanitized;
    }

    return SettingsManager::getInstance().SetInputsIncludedMask(BuildInputsMask());
}

std::string Controller::GetStateTUI() const {
    std::vector<int> channelsCopy;
    std::unordered_map<int, double> relaysPWMCopy;
    std::vector<int> relaysOnCopy;
    bool runningCopy = false;
    bool doorOpenCopy = false;
    bool alarmingCopy = false;
    std::string stateCopy;
    double setpointCopy = 0.0;
    double processCopy = 0.0;
    double pidOutputCopy = 0.0;
    double filterCopy = 0.0;

    {
        ScopedLock lock(stateMutex);
        channelsCopy = inputsBeingUsed;
        relaysPWMCopy = relaysPWM;
        relaysOnCopy = relaysWhenControllerRunning;
        runningCopy = running;
        doorOpenCopy = doorOpen;
        alarmingCopy = alarming;
        stateCopy = state;
        setpointCopy = setPoint;
        processCopy = processValue;
        pidOutputCopy = PIDOutput;
        filterCopy = inputFilterTimeMs;
    }

    std::string channels = "-";
    if (!channelsCopy.empty()) {
        channels.clear();
        for (size_t index = 0; index < channelsCopy.size(); ++index) {
            char part[16];
            std::snprintf(part, sizeof(part), "%s%d", (index == 0 ? "" : ","), channelsCopy[index]);
            channels += part;
        }
    }

    const char* runText = runningCopy ? "RUN" : "STOP";
    const char* doorText = doorOpenCopy ? "OPEN" : "CLOSED";
    const char* alarmText = alarmingCopy ? "YES" : "NO";
    const char* pidMode = pidOutputCopy > 0 ? "HEAT" : (pidOutputCopy < 0 ? "VENT" : "HOLD");

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
    std::snprintf(line4, sizeof(line4), "| Mode:%-6s State:%-16.16s Alarm:%-3s                           |", runText, stateCopy.c_str(), alarmText);
    std::snprintf(line5, sizeof(line5), "| Door:%-6s Tick(ms):%-6.0f Filter(ms):%-7.1f                    |", doorText, TICK_INTERVAL_MS, filterCopy);
    std::snprintf(line6, sizeof(line6), "| Setpoint:%8.2f  PV:%10.2f  Error:%10.2f                      |", setpointCopy, processCopy, (setpointCopy - processCopy));
    std::snprintf(line7, sizeof(line7), "| PID Out:%9.2f  PID Mode:%-8s                                 |", pidOutputCopy, pidMode);
    std::snprintf(line8, sizeof(line8), "| Inputs:%3zu  Ch:%-47.47s |", channelsCopy.size(), channels.c_str());
    std::snprintf(line9, sizeof(line9), "| RelayPWM entries:%3zu  Running-relays:%3zu                     |", relaysPWMCopy.size(), relaysOnCopy.size());
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
    esp_err_t err = UpdateProcessValue();
    if (err != ESP_OK) {
        bool wasRunning = false;
        {
            ScopedLock lock(stateMutex);
            alarming = true;
            state = "Sensor Error";
            wasRunning = running;
        }

        if (wasRunning) {
            (void)Stop();
        }

        return err;
    }

    bool shouldAlarm = CheckAlarmingConditions();
    bool wasAlarming = false;
    bool wasRunning = false;

    {
        ScopedLock lock(stateMutex);
        wasAlarming = alarming;
        wasRunning = running;

        if (shouldAlarm) {
            alarming = true;
            state = "Alarming";
        } else if (alarming) {
            alarming = false;
            if (!running) {
                state = "Idle";
            }
        }
    }

    if (shouldAlarm && !wasAlarming && wasRunning) {
        (void)Stop();
    }

    return ESP_OK;
}

esp_err_t Controller::PerformOnRunning() {
    double setPointCopy = 0.0;
    double processValueCopy = 0.0;

    {
        ScopedLock lock(stateMutex);
        setPointCopy = setPoint;
        processValueCopy = processValue;
    }

    const double output = pidController.Calculate(setPointCopy, processValueCopy);

    {
        ScopedLock lock(stateMutex);
        PIDOutput = output;
    }

    if (output < 0) {
        const double doorOpenFraction = ComputeCoolingDoorOpenFraction(output, processValueCopy);
        const double angleFromPercent = ComputeDoorAngleFromFraction(doorOpenFraction);
        ApplyDoorTargetAngle(angleFromPercent, TICK_INTERVAL_MS / 1000.0);
        relayPWM.SetDutyCycle(0.0f);
        (void)relayPWM.ForceOff();
    } else if (output > 0) {
        double pwmValue = std::min(output / 100, 1.0);
        relayPWM.SetDutyCycle(static_cast<float>(pwmValue));
        ApplyDoorTargetAngle(GetDoorClosedAngleDeg(), TICK_INTERVAL_MS / 1000.0);
    } else {
        relayPWM.SetDutyCycle(0);
        (void)relayPWM.ForceOff();
        ApplyDoorTargetAngle(GetDoorClosedAngleDeg(), TICK_INTERVAL_MS / 1000.0);
    }

    return ESP_OK;
}

esp_err_t Controller::PerformOnNotRunning() {
    bool localDoorOpen = false;
    bool localDoorPreviewActive = false;
    double localDoorPreviewAngleDeg = 0.0;
    double localDoorClosedAngleDeg = 0.0;
    double localDoorOpenAngleDeg = 0.0;

    {
        ScopedLock lock(stateMutex);
        PIDOutput = 0.0;
        localDoorOpen = doorOpen;
        localDoorPreviewActive = doorPreviewActive;
        localDoorPreviewAngleDeg = doorPreviewAngleDeg;
        localDoorClosedAngleDeg = doorClosedAngleDeg;
        localDoorOpenAngleDeg = doorOpenAngleDeg;
    }

    relayPWM.SetDutyCycle(0);
    if (localDoorPreviewActive) {
        ApplyDoorTargetAngle(localDoorPreviewAngleDeg, TICK_INTERVAL_MS / 1000.0);
    } else if (localDoorOpen) {
        ApplyDoorTargetAngle(localDoorOpenAngleDeg, TICK_INTERVAL_MS / 1000.0);
    } else {
        ApplyDoorTargetAngle(localDoorClosedAngleDeg, TICK_INTERVAL_MS / 1000.0);
    }

    return ESP_OK;
}

esp_err_t Controller::UpdateProcessValue() {
    std::vector<int> channels;
    double filterTimeMs = 0.0;
    double previousFiltered = 0.0;
    bool hasPrev = false;

    {
        ScopedLock lock(stateMutex);
        channels = inputsBeingUsed;
        filterTimeMs = inputFilterTimeMs;
        previousFiltered = filteredProcessValue;
        hasPrev = hasFilteredProcessValue;
    }

    double newProcessValue = 0.0;
    int inputsReadCorrectly = 0;
    for (int channel : channels) {
        double value = HardwareManager::getInstance().getThermocoupleValue(channel);
        if (value == -3000.0) {
            continue;
        }
        newProcessValue += value;
        inputsReadCorrectly++;
    }

    if (inputsReadCorrectly == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    const double averagedValue = newProcessValue / inputsReadCorrectly;
    const double dt = TICK_INTERVAL_MS;
    const double alpha = dt / (filterTimeMs + dt);
    const double filteredValue = hasPrev ? (alpha * averagedValue + (1 - alpha) * previousFiltered) : averagedValue;

    {
        ScopedLock lock(stateMutex);
        filteredProcessValue = filteredValue;
        hasFilteredProcessValue = true;
        processValue = filteredValue;
    }

    return ESP_OK;
}

bool Controller::CheckAlarmingConditions() {
    double value = 0.0;
    {
        ScopedLock lock(stateMutex);
        value = processValue;
    }

    if (value < MIN_PROCESS_VALUE || value > MAX_PROCESS_VALUE) {
        return true;
    }
    return false;
}

esp_err_t Controller::RunningRelaysOn(){
    std::vector<int> relays;
    {
        ScopedLock lock(stateMutex);
        relays = relaysWhenControllerRunning;
    }

    for (int relayIndex : relays) {
        esp_err_t err = HardwareManager::getInstance().setRelayState(relayIndex, true);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t Controller::RunningRelaysOff(){
    std::vector<int> relays;
    {
        ScopedLock lock(stateMutex);
        relays = relaysWhenControllerRunning;
    }

    for (int relayIndex : relays) {
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
    std::vector<std::pair<int, bool>> nextStates;
    {
        ScopedLock lock(stateMutex);
        SyncRelayPWMAccumulatorsLocked();
        nextStates.reserve(relaysPWM.size());
        for (const auto& entry : relaysPWM) {
            const int relayIndex = entry.first;
            const double weight = std::clamp(entry.second, 0.0, 1.0);
            bool relayOn = false;

            if (weight >= 1.0) {
                relayOn = true;
            } else if (weight > 0.0) {
                double& accumulator = relayPWMCycleAccumulators[relayIndex];
                accumulator += weight;
                if (accumulator >= 1.0) {
                    relayOn = true;
                    while (accumulator >= 1.0) {
                        accumulator -= 1.0;
                    }
                }
            }

            nextStates.emplace_back(relayIndex, relayOn);
        }
    }

    for (const auto& entry : nextStates) {
        HardwareManager::getInstance().setRelayState(entry.first, entry.second);
    }
}

void Controller::RelayOff() {
    std::vector<int> relays;
    {
        ScopedLock lock(stateMutex);
        relays.reserve(relaysPWM.size());
        for (const auto& entry : relaysPWM) {
            relays.push_back(entry.first);
        }
    }

    for (int relayIndex : relays) {
        HardwareManager::getInstance().setRelayState(relayIndex, false);
    }
}

esp_err_t Controller::AddSetRelayPWM(int relayIndex, double pwmValue) {
    if (relayIndex < 0 || relayIndex > 7) {
        return ESP_ERR_INVALID_ARG;
    }

    if (pwmValue < 0.0 || pwmValue > 1.0) {
        return ESP_ERR_INVALID_ARG;
    }

    {
        ScopedLock lock(stateMutex);
        relaysPWM[relayIndex] = std::clamp(pwmValue, 0.0, 1.0);
        SyncRelayPWMAccumulatorsLocked();
    }

    return PersistRelaysPWMSettings();
}

esp_err_t Controller::RemoveRelayPWM(int relayIndex) {
    {
        ScopedLock lock(stateMutex);
        auto it = relaysPWM.find(relayIndex);
        if (it == relaysPWM.end()) {
            return ESP_ERR_INVALID_ARG;
        }
        relaysPWM.erase(it);
        SyncRelayPWMAccumulatorsLocked();
    }

    return PersistRelaysPWMSettings();
}

esp_err_t Controller::SetRelayPWMEnabled(const std::vector<int>& relayIndices) {
    std::unordered_map<int, double> nextMap;
    for (int relay : relayIndices) {
        if (relay < 0 || relay > 7) {
            return ESP_ERR_INVALID_ARG;
        }
        nextMap[relay] = 1.0;
    }

    {
        ScopedLock lock(stateMutex);
        for (auto& entry : nextMap) {
            auto existing = relaysPWM.find(entry.first);
            if (existing != relaysPWM.end()) {
                entry.second = std::clamp(existing->second, 0.0, 1.0);
            }
        }
        relaysPWM = nextMap;
        SyncRelayPWMAccumulatorsLocked();
    }

    return PersistRelaysPWMSettings();
}

esp_err_t Controller::SetRelaysPWM(const std::unordered_map<int, double>& relayWeights) {
    std::unordered_map<int, double> sanitized;
    sanitized.reserve(relayWeights.size());
    for (const auto& entry : relayWeights) {
        if (entry.first < 0 || entry.first > 7) {
            return ESP_ERR_INVALID_ARG;
        }
        if (entry.second < 0.0 || entry.second > 1.0) {
            return ESP_ERR_INVALID_ARG;
        }
        sanitized[entry.first] = std::clamp(entry.second, 0.0, 1.0);
    }

    {
        ScopedLock lock(stateMutex);
        relaysPWM = sanitized;
        SyncRelayPWMAccumulatorsLocked();
    }

    return PersistRelaysPWMSettings();
}

esp_err_t Controller::AddRelayWhenRunning(int relayIndex) {
    if (relayIndex < 0 || relayIndex > 7) {
        return ESP_ERR_INVALID_ARG;
    }

    {
        ScopedLock lock(stateMutex);
        if (std::find(relaysWhenControllerRunning.begin(), relaysWhenControllerRunning.end(), relayIndex) != relaysWhenControllerRunning.end()) {
            return ESP_ERR_INVALID_ARG;
        }
        relaysWhenControllerRunning.push_back(relayIndex);
    }

    return SettingsManager::getInstance().SetRelaysOnMask(BuildRelaysOnMask());
}

esp_err_t Controller::RemoveRelayWhenRunning(int relayIndex) {
    {
        ScopedLock lock(stateMutex);
        auto it = std::find(relaysWhenControllerRunning.begin(), relaysWhenControllerRunning.end(), relayIndex);
        if (it == relaysWhenControllerRunning.end()) {
            return ESP_ERR_INVALID_ARG;
        }
        relaysWhenControllerRunning.erase(it);
    }

    return SettingsManager::getInstance().SetRelaysOnMask(BuildRelaysOnMask());
}

esp_err_t Controller::SetRelaysWhenRunning(const std::vector<int>& relayIndices) {
    std::vector<int> sanitized;
    sanitized.reserve(relayIndices.size());
    for (int relay : relayIndices) {
        if (relay < 0 || relay > 7) {
            return ESP_ERR_INVALID_ARG;
        }
        if (std::find(sanitized.begin(), sanitized.end(), relay) == sanitized.end()) {
            sanitized.push_back(relay);
        }
    }

    {
        ScopedLock lock(stateMutex);
        relaysWhenControllerRunning = sanitized;
    }

    return SettingsManager::getInstance().SetRelaysOnMask(BuildRelaysOnMask());
}

esp_err_t Controller::SetDoorCalibrationAngles(double closedAngleDeg, double openAngleDeg) {
    if (closedAngleDeg < 0.0 || closedAngleDeg > 180.0 || openAngleDeg < 0.0 || openAngleDeg > 180.0) {
        return ESP_ERR_INVALID_ARG;
    }

    SettingsManager& settings = SettingsManager::getInstance();
    esp_err_t err = settings.SetDoorClosedAngleDeg(closedAngleDeg);
    if (err != ESP_OK) {
        return err;
    }
    err = settings.SetDoorOpenAngleDeg(openAngleDeg);
    if (err != ESP_OK) {
        return err;
    }

    bool localRunning = false;
    bool localDoorOpen = false;
    double localTargetAngle = 0.0;
    {
        ScopedLock lock(stateMutex);
        doorClosedAngleDeg = closedAngleDeg;
        doorOpenAngleDeg = openAngleDeg;
        localRunning = running;
        localDoorOpen = doorOpen;
        if (doorPreviewActive) {
            doorPreviewAngleDeg = std::clamp(doorPreviewAngleDeg, 0.0, 180.0);
            localTargetAngle = doorPreviewAngleDeg;
        } else {
            localTargetAngle = localDoorOpen ? openAngleDeg : closedAngleDeg;
        }
    }

    if (localRunning) {
        return ESP_OK;
    }
    ApplyDoorTargetAngle(localTargetAngle, TICK_INTERVAL_MS / 1000.0);
    return ESP_OK;
}

esp_err_t Controller::SetDoorMaxSpeedDegPerSec(double speedDegPerSec) {
    if (speedDegPerSec < 1.0 || speedDegPerSec > 360.0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = SettingsManager::getInstance().SetDoorMaxSpeedDegPerSec(speedDegPerSec);
    if (err != ESP_OK) {
        return err;
    }

    {
        ScopedLock lock(stateMutex);
        doorMaxSpeedDegPerSec = speedDegPerSec;
    }

    return ESP_OK;
}

esp_err_t Controller::SetDoorPreviewAngle(double angleDeg) {
    if (angleDeg < 0.0 || angleDeg > 180.0) {
        return ESP_ERR_INVALID_ARG;
    }

    {
        ScopedLock lock(stateMutex);
        if (running) {
            return ESP_ERR_INVALID_STATE;
        }
        doorPreviewActive = true;
        doorPreviewAngleDeg = angleDeg;
    }

    ApplyDoorTargetAngle(angleDeg, TICK_INTERVAL_MS / 1000.0);
    return ESP_OK;
}

esp_err_t Controller::ClearDoorPreview() {
    bool localRunning = false;
    bool localDoorOpen = false;
    double localClosedAngle = 0.0;
    double localOpenAngle = 0.0;
    {
        ScopedLock lock(stateMutex);
        if (running) {
            return ESP_ERR_INVALID_STATE;
        }
        doorPreviewActive = false;
        localRunning = running;
        localDoorOpen = doorOpen;
        localClosedAngle = doorClosedAngleDeg;
        localOpenAngle = doorOpenAngleDeg;
    }

    if (localRunning) {
        return ESP_OK;
    }
    ApplyDoorTargetAngle(localDoorOpen ? localOpenAngle : localClosedAngle, TICK_INTERVAL_MS / 1000.0);
    return ESP_OK;
}

uint8_t Controller::BuildInputsMask() const {
    uint8_t mask = 0;
    ScopedLock lock(stateMutex);
    for (int channel : inputsBeingUsed) {
        if (channel >= 0 && channel <= 7) {
            mask |= static_cast<uint8_t>(1u << channel);
        }
    }
    return mask;
}

uint8_t Controller::BuildRelaysPWMMask() const {
    uint8_t mask = 0;
    ScopedLock lock(stateMutex);
    for (const auto& entry : relaysPWM) {
        if (entry.first >= 0 && entry.first <= 7) {
            mask |= static_cast<uint8_t>(1u << entry.first);
        }
    }
    return mask;
}

uint8_t Controller::BuildRelaysOnMask() const {
    uint8_t mask = 0;
    ScopedLock lock(stateMutex);
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
    SyncRelayPWMAccumulatorsLocked();
}

void Controller::ApplyRelaysOnMask(uint8_t mask) {
    relaysWhenControllerRunning.clear();
    for (int relayIndex = 0; relayIndex < 8; ++relayIndex) {
        if ((mask & static_cast<uint8_t>(1u << relayIndex)) != 0) {
            relaysWhenControllerRunning.push_back(relayIndex);
        }
    }
}

void Controller::SyncRelayPWMAccumulatorsLocked() {
    for (auto it = relayPWMCycleAccumulators.begin(); it != relayPWMCycleAccumulators.end();) {
        if (relaysPWM.find(it->first) == relaysPWM.end()) {
            it = relayPWMCycleAccumulators.erase(it);
        } else {
            ++it;
        }
    }

    for (const auto& entry : relaysPWM) {
        if (relayPWMCycleAccumulators.find(entry.first) == relayPWMCycleAccumulators.end()) {
            relayPWMCycleAccumulators[entry.first] = 0.0;
        }
    }
}

esp_err_t Controller::PersistRelaysPWMSettings() {
    SettingsManager& settings = SettingsManager::getInstance();
    uint8_t mask = 0;
    std::array<double, 8> weights = settings.GetRelayPWMWeights();

    {
        ScopedLock lock(stateMutex);
        for (const auto& entry : relaysPWM) {
            if (entry.first >= 0 && entry.first <= 7) {
                mask |= static_cast<uint8_t>(1u << entry.first);
                weights[static_cast<std::size_t>(entry.first)] = std::clamp(entry.second, 0.0, 1.0);
            }
        }
    }
    esp_err_t err = settings.SetRelaysPWMMask(mask);
    if (err != ESP_OK) {
        return err;
    }
    return settings.SetRelayPWMWeights(weights);
}

double Controller::ComputeCoolingDoorOpenFraction(double pidOutput, double processValueC) const {
    if (pidOutput >= 0.0) {
        return 0.0;
    }

    const double coolingDemand = std::clamp(-pidOutput / 100.0, 0.0, 1.0);
    const double tempRange = std::max(MAX_PROCESS_VALUE - ROOM_TEMPERATURE_C, 1.0);
    const double normalizedTemp = std::clamp((processValueC - ROOM_TEMPERATURE_C) / tempRange, 0.0, 1.0);

    const double tempEffectiveness = MIN_DOOR_COOLING_EFFECTIVENESS +
        (1.0 - MIN_DOOR_COOLING_EFFECTIVENESS) * normalizedTemp;
    const double compensatedDemand = std::clamp(coolingDemand / std::max(tempEffectiveness, 0.05), 0.0, 1.0);

    // Door cooling is strongly nonlinear: small openings provide most of the effect.
    const double doorOpenFraction = 1.0 - std::pow(1.0 - compensatedDemand, 1.0 / DOOR_COOLING_NONLINEARITY);
    return std::clamp(doorOpenFraction, 0.0, 1.0);
}

double Controller::ComputeDoorAngleFromFraction(double openFraction) const {
    const double clampedFraction = std::clamp(openFraction, 0.0, 1.0);
    double closedAngle = 0.0;
    double openAngle = 0.0;
    {
        ScopedLock lock(stateMutex);
        closedAngle = doorClosedAngleDeg;
        openAngle = doorOpenAngleDeg;
    }

    return closedAngle + clampedFraction * (openAngle - closedAngle);
}

void Controller::ApplyDoorTargetAngle(double targetAngle, double dtSeconds) {
    const double clampedTarget = std::clamp(targetAngle, 0.0, 180.0);
    const double safeDt = std::max(dtSeconds, 0.0);
    double speedDegPerSec = 60.0;
    {
        ScopedLock lock(stateMutex);
        speedDegPerSec = doorMaxSpeedDegPerSec;
    }
    speedDegPerSec = std::clamp(speedDegPerSec, 1.0, 360.0);

    const double currentAngle = HardwareManager::getInstance().getServoAngle();
    const double maxStep = speedDegPerSec * safeDt;
    const double delta = clampedTarget - currentAngle;

    double nextAngle = clampedTarget;
    if (std::abs(delta) > maxStep) {
        nextAngle = currentAngle + std::copysign(maxStep, delta);
    }

    (void)HardwareManager::getInstance().setServoAngle(nextAngle);
}
