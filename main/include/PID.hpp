#pragma once

#include "esp_err.h"

class PID{
    public:
        PID() = default;
        double Calculate(double setPoint, double processValue);
        double GetPreviousOutput() const { return previousOutput; }
        double GetPreviousP() const { return previousP; }
        double GetPreviousI() const { return previousI; }
        double GetPreviousD() const { return previousD; }
        double GetKp() const { return heatingKp; } // Backward-compatible alias for heating Kp
        double GetKi() const { return heatingKi; } // Backward-compatible alias for heating Ki
        double GetKd() const { return heatingKd; } // Backward-compatible alias for heating Kd
        double GetHeatingKp() const { return heatingKp; }
        double GetHeatingKi() const { return heatingKi; }
        double GetHeatingKd() const { return heatingKd; }
        double GetCoolingKp() const { return coolingKp; }
        double GetCoolingKi() const { return coolingKi; }
        double GetCoolingKd() const { return coolingKd; }
        double GetDerivativeFilterTime() const { return derivativeFilterTime; }
        double GetSetpointWeight() const { return setpointWeight; }
        double GetIntegralZoneC() const { return integralZoneC; }
        double GetIntegralLeakTimeSeconds() const { return integralLeakTimeSeconds; }
        esp_err_t Tune(double Kp, double Ki, double Kd);
        esp_err_t TuneHeating(double Kp, double Ki, double Kd);
        esp_err_t TuneCooling(double Kp, double Ki, double Kd);
        esp_err_t SetDerivativeFilterAlpha(double alpha);
        esp_err_t SetDerivativeFilterTime(double filterTimeSeconds);
        esp_err_t SetSetpointWeight(double weight);
        esp_err_t SetIntegralZoneC(double zoneC);
        esp_err_t SetIntegralLeakTimeSeconds(double leakTimeSeconds);
        esp_err_t Reset();

    private:
        double heatingKp = 1.0;
        double heatingKi = 0.0;
        double heatingKd = 0.0;
        double coolingKp = 1.0;
        double coolingKi = 0.0;
        double coolingKd = 0.0;
        double OutputMin = -100.0;
        double OutputMax = 100.0;
        double setpointWeight = 0.5; // Weight for the setpoint in the error calculation, between 0 and 1
        double integralZoneC = 0.0; // Integrator active only when |error| <= integralZoneC. 0 disables zone gating.
        double integralLeakTimeSeconds = 0.0; // Exponential leak time constant. 0 disables leak.

        double DerivativeFilterAlpha = 1; // Smoothing factor for derivative term
        double derivativeFilterTime = 0.0; // Time constant in seconds

        double integral = 0.0;
        double previousError = 0.0;
        double previousPV = 0.0; // Previous process value for derivative calculation
        double dFiltered = 0.0; // Filtered derivative term

        double previousOutput = 0.0;
        double previousP = 0.0; 
        double previousI = 0.0;
        double previousD = 0.0;

        bool firstRun = true; // Flag to handle the first run for derivative calculation

        uint64_t lastTimeUs = 0; // Timestamp of the last calculation in microseconds

};

