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
        double GetKp() const { return Kp; }
        double GetKi() const { return Ki; }
        double GetKd() const { return Kd; }
        esp_err_t Tune(double Kp, double Ki, double Kd);
        esp_err_t Reset();

    private:
        double Kp = 1.0; 
        double Ki = 0.0; 
        double Kd = 0.0;
        double OutputMin = -100.0;
        double OutputMax = 100.0;

        double DerivativeFilterAlpha = 1; // Smoothing factor for derivative term

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

