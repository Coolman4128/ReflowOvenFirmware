#pragma once

#include "esp_err.h"
#include <string>
#include <vector>
#include <unordered_map>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "PID.hpp"
#include "PWM.hpp"

class Controller{
    public:
        static Controller& getInstance();
        Controller(const Controller&) = delete;
        Controller& operator=(const Controller&) = delete;
        Controller(Controller&&) = delete;
        Controller& operator=(Controller&&) = delete;

        esp_err_t RunTick();


        // Getting state info for controller:
        double GetSetPoint() const;
        double GetProcessValue() const;
        std::string GetState() const;
        double GetPIDOutput() const;
        bool IsRunning() const;
        bool IsDoorOpen() const;
        bool IsAlarming() const;
        bool IsSetpointLockedByProfile() const;
        double GetInputFilterTimeMs() const;
        std::vector<int> GetInputChannels() const;
        std::vector<int> GetRelaysPWMEnabled() const;
        std::unordered_map<int, double> GetRelaysPWMWeights() const;
        std::vector<int> GetRelaysWhenRunning() const;
        PID* GetPIDController() { return &pidController; }
        std::string GetStateTUI() const;

        // Interfacing with the controller settings:
        esp_err_t Start();
        esp_err_t Stop();
        esp_err_t OpenDoor();
        esp_err_t CloseDoor();
        esp_err_t SetSetPoint(double newSetPoint);
        esp_err_t SetSetPointFromProfile(double newSetPoint);
        void SetProfileSetpointLock(bool locked);
        esp_err_t SetInputFilterTime(double newFilterTimeMs);
        esp_err_t SetPIDGains(double newKp, double newKi, double newKd);
        esp_err_t SetDerivativeFilterTime(double newFilterTimeSeconds);
        esp_err_t SetSetpointWeight(double newWeight);
        esp_err_t AddInputChannel(int channel);
        esp_err_t RemoveInputChannel(int channel);
        esp_err_t SetInputChannels(const std::vector<int>& channels);
        esp_err_t AddSetRelayPWM(int relayIndex, double pwmValue);
        esp_err_t RemoveRelayPWM(int relayIndex);
        esp_err_t SetRelayPWMEnabled(const std::vector<int>& relayIndices);
        esp_err_t SetRelaysPWM(const std::unordered_map<int, double>& relayWeights);
        esp_err_t AddRelayWhenRunning(int relayIndex);
        esp_err_t RemoveRelayWhenRunning(int relayIndex);
        esp_err_t SetRelaysWhenRunning(const std::vector<int>& relayIndices);



    private:
        static Controller* instance;
        Controller();
        constexpr static double TICK_INTERVAL_MS = 250.0; // 100 ms tick interval
        constexpr static double MAX_SETPOINT = 300.0; // Max temp in Celsius
        constexpr static double MIN_SETPOINT = 0.0; // Min temp in Celsius
        constexpr static double MIN_PROCESS_VALUE = -100.0; // Minimum process value (alarm will turn on if value is below this, to catch sensor errors)
        constexpr static double MAX_PROCESS_VALUE = 300.0; // Max temp in Celsius (alarm will turn on if value is above this, to catch sensor errors and prevent overheating)
        constexpr static double ROOM_TEMPERATURE_C = 24.0;
        constexpr static double MIN_DOOR_COOLING_EFFECTIVENESS = 0.45;
        constexpr static double DOOR_COOLING_NONLINEARITY = 3.0;
        
        
        PID pidController; // The actual PID controller object that will do the calculations
        PWM relayPWM;
        bool running = false;
        std::string state = "Idle";
        bool alarming = false;
        bool doorOpen = false;
        bool setpointLockedByProfile = false;

        // Controller Runtime properties
        double setPoint = 0.0;
        double processValue = 0.0;
        double filteredProcessValue = 0.0;
        bool hasFilteredProcessValue = false;
        double PIDOutput = 0.0;

        // Controller Tuning Settings
        double inputFilterTimeMs = 100.0;
        std::vector<int> inputsBeingUsed = {0}; // Default to channel 0 only.
        std::unordered_map<int, double> relaysPWM = {{0, 1.0}, {1, 0.5}}; // Default to relay 0 at 100 strength, and relay 1 at 50% strength
        std::unordered_map<int, double> relayPWMCycleAccumulators;
        std::vector<int> relaysWhenControllerRunning = {2}; // Default to turning on relay 2 when the controller is running, off otherwise

        mutable SemaphoreHandle_t stateMutex = nullptr;



        esp_err_t PerformOnRunning();
        esp_err_t PerformOnNotRunning();
        esp_err_t Perform();

        esp_err_t UpdateProcessValue();
        bool CheckAlarmingConditions();

        uint8_t BuildInputsMask() const;
        uint8_t BuildRelaysPWMMask() const;
        uint8_t BuildRelaysOnMask() const;
        void ApplyInputsMask(uint8_t mask);
        void ApplyRelaysPWMMask(uint8_t mask);
        void ApplyRelaysOnMask(uint8_t mask);
        void SyncRelayPWMAccumulatorsLocked();
        esp_err_t PersistRelaysPWMSettings();
        double ComputeCoolingDoorOpenFraction(double pidOutput, double processValueC) const;


        esp_err_t RunningRelaysOn();
        esp_err_t RunningRelaysOff();
        static void RelayOnThunk(void* ctx);
        static void RelayOffThunk(void* ctx);
        void RelayOn();
        void RelayOff();



};
