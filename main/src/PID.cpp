#include "PID.hpp"
#include <algorithm>
#include "esp_timer.h"

esp_err_t PID::Reset() {
    integral = 0.0;
    previousError = 0.0;
    dFiltered = 0.0;
    previousOutput = 0.0;
    firstRun = true;
    lastTimeUs = 0;
    previousPV = 0.0;
    return ESP_OK;
}

double PID::Calculate(double setPoint, double processValue) {
    
    if (firstRun) {
        previousError = setPoint - processValue;
        previousPV = processValue;
        previousOutput = previousError * Kp; // Proportional term on first run
        firstRun = false;
        lastTimeUs = esp_timer_get_time();
        return previousError * Kp; // Return proportional term on first run, as we don't have a valid derivative yet
    }

    double error = setPoint - processValue;
    double dt = (esp_timer_get_time() - lastTimeUs) / 1e6; // Convert microseconds to seconds
    lastTimeUs = esp_timer_get_time();
    if (dt <= 0.0) {
        dt = 1e-6;
    }

    if (derivativeFilterTime > 0.0) {
        DerivativeFilterAlpha = dt / (derivativeFilterTime + dt);
    } else {
        DerivativeFilterAlpha = 1.0;
    }

    integral += error * dt;
    double derivative = (processValue - previousPV) / dt;
    previousPV = processValue;

    // Apply derivative filtering
    dFiltered = DerivativeFilterAlpha * derivative + (1 - DerivativeFilterAlpha) * dFiltered;

    double outputNoI = Kp * error + Kd * dFiltered;
    double outputI = Ki * integral;
    if ((outputI + outputNoI) > OutputMax) {
        outputI = std::max(0.0, OutputMax - outputNoI); // Prevent integral windup
        if (Ki != 0.0) {
            integral = outputI / Ki; // Adjust integral to match the clamped output
        }
    }
    else if ((outputI + outputNoI) < OutputMin) {
        outputI = std::min(0.0, OutputMin - outputNoI); // Prevent integral windup
        if (Ki != 0.0) {
            integral = outputI / Ki; // Adjust integral to match the clamped output
        }
    }
    double output = outputNoI + outputI;
    output = std::clamp(output, OutputMin, OutputMax);
    previousError = error;
    previousOutput = output;

    return output;
}

esp_err_t PID::Tune(double Kp, double Ki, double Kd) {
    this->Kp = Kp;
    this->Ki = Ki;
    this->Kd = Kd;
    return ESP_OK;
}

esp_err_t PID::SetDerivativeFilterAlpha(double alpha) {
    if (alpha < 0 || alpha > 1) {
        return ESP_ERR_INVALID_ARG; // Alpha must be between 0 and 1
    }
    DerivativeFilterAlpha = alpha;
    derivativeFilterTime = 0.0;
    return ESP_OK;
}

esp_err_t PID::SetDerivativeFilterTime(double filterTimeSeconds) {
    if (filterTimeSeconds < 0.0) {
        return ESP_ERR_INVALID_ARG;
    }
    derivativeFilterTime = filterTimeSeconds;
    return ESP_OK;
}
