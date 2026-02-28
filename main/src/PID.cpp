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
    auto clampPTermToBand = [](double pTerm, double error) {
        if (error > 0.0) {
            return std::max(0.0, pTerm);
        }
        if (error < 0.0) {
            return std::min(0.0, pTerm);
        }
        return pTerm;
    };
    
    if (firstRun) {
        const double error = setPoint - processValue;
        const double errorWeighted = (setpointWeight * setPoint) - processValue;
        previousError = error;
        previousPV = processValue;
        previousP = clampPTermToBand(Kp * errorWeighted, error);
        previousI = 0.0;
        previousD = 0.0;
        previousOutput = std::clamp(previousP, OutputMin, OutputMax);
        firstRun = false;
        lastTimeUs = esp_timer_get_time();
        return previousOutput; // First run has no valid derivative term yet
    }

    const int64_t nowUs = esp_timer_get_time();
    double dt = (nowUs - lastTimeUs) / 1e6; // Convert microseconds to seconds
    lastTimeUs = nowUs;
    if (dt <= 0.0) {
        dt = 1e-6;
    }

    const double error = setPoint - processValue;
    const double errorWeighted = (setpointWeight * setPoint) - processValue;

    if (derivativeFilterTime > 0.0) {
        DerivativeFilterAlpha = dt / (derivativeFilterTime + dt);
    } else {
        DerivativeFilterAlpha = 1.0;
    }

    const double derivative = -1 * (processValue - previousPV) / dt;
    previousPV = processValue;

    // Apply derivative filtering
    dFiltered = DerivativeFilterAlpha * derivative + (1 - DerivativeFilterAlpha) * dFiltered;

    const double pTerm = clampPTermToBand(Kp * errorWeighted, error);
    const double dTerm = Kd * dFiltered;
    const double outputNoI = pTerm + dTerm;

    // Conditional integration anti-windup:
    // Integrate only if it helps move saturated output back toward the linear region.
    const double integralCandidate = integral + error * dt;
    const double outputICandidate = Ki * integralCandidate;
    const double outputCandidate = outputNoI + outputICandidate;

    const bool saturatingHigh = (outputCandidate > OutputMax);
    const bool saturatingLow = (outputCandidate < OutputMin);
    const bool drivesFurtherIntoHighSat = saturatingHigh && ((error * Ki) > 0.0);
    const bool drivesFurtherIntoLowSat = saturatingLow && ((error * Ki) < 0.0);

    if (!(drivesFurtherIntoHighSat || drivesFurtherIntoLowSat)) {
        integral = integralCandidate;
    }

    const double iTerm = Ki * integral;
    const double output = std::clamp(outputNoI + iTerm, OutputMin, OutputMax);

    previousError = error;
    previousP = pTerm;
    previousI = iTerm;
    previousD = dTerm;
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

esp_err_t PID::SetSetpointWeight(double weight) {
    if (weight < 0.0 || weight > 1.0) {
        return ESP_ERR_INVALID_ARG;
    }
    setpointWeight = weight;
    return ESP_OK;
}
