#pragma once

#include "esp_err.h"
#include <string>
#include <vector>
#include <unordered_map>
#include "PID.hpp"
#include "PWM.hpp"

class Controller{
    public:
        Controller();

        esp_err_t RunTick();


        // Getting state info for controller:
        double GetSetPoint() const { return setPoint; }
        double GetProcessValue() const { return processValue; }
        std::string GetState() const { return state; }
        double GetPIDOutput() const { return PIDOutput; }
        bool IsRunning() const { return running; }
        bool IsDoorOpen() const { return doorOpen; }
        PID* GetPIDController() { return &pidController; }
        std::string GetStateTUI() const;

        // Interfacing with the controller settings:
        esp_err_t Start();
        esp_err_t Stop();
        esp_err_t OpenDoor();
        esp_err_t CloseDoor();
        esp_err_t SetSetPoint(double newSetPoint);
        esp_err_t SetInputFilterTime(double newFilterTimeMs);
        esp_err_t AddInputChannel(int channel);
        esp_err_t RemoveInputChannel(int channel);
        esp_err_t AddSetRelayPWM(int relayIndex, double pwmValue);
        esp_err_t RemoveRelayPWM(int relayIndex);
        esp_err_t AddRelayWhenRunning(int relayIndex);
        esp_err_t RemoveRelayWhenRunning(int relayIndex);



    private:
        constexpr static double TICK_INTERVAL_MS = 250.0; // 100 ms tick interval
        constexpr static double MAX_SETPOINT = 300.0; // Max temp in Celsius
        constexpr static double MIN_SETPOINT = 0.0; // Min temp in Celsius
        constexpr static double MIN_PROCESS_VALUE = -100.0; // Minimum process value (alarm will turn on if value is below this, to catch sensor errors)
        constexpr static double MAX_PROCESS_VALUE = 300.0; // Max temp in Celsius (alarm will turn on if value is above this, to catch sensor errors and prevent overheating)
        
        
        PID pidController; // The actual PID controller object that will do the calculations
        PWM relayPWM;
        bool running = false;
        std::string state = "Idle";
        bool alarming = false;
        bool doorOpen = false;

        // Controller Runtime properties
        double setPoint = 0.0;
        double processValue = 0.0;
        double PIDOutput = 0.0;

        // Controller Tuning Settings
        double inputFilterTimeMs = 100.0;
        std::vector<int> inputsBeingUsed = {0}; // Default to channel 0 only.
        std::unordered_map<int, double> relaysPWM = {{0, 1.0}, {1, 0.5}}; // Default to relay 0 at 100 strength, and relay 1 at 50% strength
        std::vector<int> relaysWhenControllerRunning = {2}; // Default to turning on relay 2 when the controller is running, off otherwise
        



        esp_err_t PerformOnRunning();
        esp_err_t PerformOnNotRunning();
        esp_err_t Perform();

        esp_err_t UpdateProcessValue();
        bool CheckAlarmingConditions();


        esp_err_t RunningRelaysOn();
        esp_err_t RunningRelaysOff();
        static void RelayOnThunk(void* ctx);
        static void RelayOffThunk(void* ctx);
        void RelayOn();
        void RelayOff();



};