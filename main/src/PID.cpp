#include "PID.hpp"
#include <algorithm>
#include <cmath>
#include "esp_timer.h"

esp_err_t PID::Reset() {
    integral = 0.0;
    previousError = 0.0;
    dFiltered = 0.0;
    previousOutput = 0.0;
    previousP = 0.0;
    previousI = 0.0;
    previousD = 0.0;
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

    const int64_t nowUs = esp_timer_get_time();
    double dt = 1e-6;
    if (!firstRun) {
        dt = (nowUs - lastTimeUs) / 1e6; // Convert microseconds to seconds
        if (dt <= 0.0) {
            dt = 1e-6;
        }
    }
    lastTimeUs = nowUs;

    const double error = setPoint - processValue;
    const double errorWeighted = (setpointWeight * setPoint) - processValue;

    const bool wasFirstRun = firstRun;
    if (wasFirstRun) {
        previousError = error;
        previousPV = processValue;
        dFiltered = 0.0;
        firstRun = false;
    }

    if (derivativeFilterTime > 0.0) {
        DerivativeFilterAlpha = dt / (derivativeFilterTime + dt);
    } else {
        DerivativeFilterAlpha = 1.0;
    }

    const double derivative = (wasFirstRun ? 0.0 : (-1.0 * (processValue - previousPV) / dt));
    previousPV = processValue;

    // Apply derivative filtering
    dFiltered = DerivativeFilterAlpha * derivative + (1.0 - DerivativeFilterAlpha) * dFiltered;

    const double pTermHeat = clampPTermToBand(heatingKp * errorWeighted, error);
    const double dTermHeat = heatingKd * dFiltered;

    const double pTermCool = clampPTermToBand(coolingKp * errorWeighted, error);
    const double dTermCool = coolingKd * dFiltered;
    const double outputNoICool = pTermCool + dTermCool;

    // Explicit asymmetric mode handling:
    // If cooling P+D is asking for a negative command, run in cooling gain set.
    const bool coolingMode = (outputNoICool < 0.0);
    const double activeKi = coolingMode ? coolingKi : heatingKi;
    const double pTerm = coolingMode ? pTermCool : pTermHeat;
    const double dTerm = coolingMode ? dTermCool : dTermHeat;
    const double outputNoI = pTerm + dTerm;

    if (integralLeakTimeSeconds > 0.0) {
        integral *= std::exp(-dt / integralLeakTimeSeconds);
    }

    const bool inIZone = (integralZoneC <= 0.0) || (std::abs(error) <= integralZoneC);
    if (activeKi > 0.0 && inIZone) {
        const double integralCandidate = integral + error * dt;

        // During cooling request (negative P+D), only allow integral updates that move toward zero.
        if (outputNoI < 0.0) {
            if (std::abs(integralCandidate) < std::abs(integral)) {
                integral = integralCandidate;
            }
        } else {
            integral = integralCandidate;
        }
    }

    double iTerm = 0.0;
    if (activeKi > 0.0) {
        iTerm = activeKi * integral;
        const double iMin = OutputMin - outputNoI;
        const double iMax = OutputMax - outputNoI;
        iTerm = std::clamp(iTerm, iMin, iMax);
        integral = iTerm / activeKi;
    }

    const double output = std::clamp(outputNoI + iTerm, OutputMin, OutputMax);

    previousError = error;
    previousP = pTerm;
    previousI = iTerm;
    previousD = dTerm;
    previousOutput = output;

    return output;
}

esp_err_t PID::Tune(double Kp, double Ki, double Kd) {
    return TuneHeating(Kp, Ki, Kd);
}

esp_err_t PID::TuneHeating(double Kp, double Ki, double Kd) {
    heatingKp = Kp;
    heatingKi = Ki;
    heatingKd = Kd;
    return ESP_OK;
}

esp_err_t PID::TuneCooling(double Kp, double Ki, double Kd) {
    coolingKp = Kp;
    coolingKi = Ki;
    coolingKd = Kd;
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

esp_err_t PID::SetIntegralZoneC(double zoneC) {
    if (zoneC < 0.0) {
        return ESP_ERR_INVALID_ARG;
    }
    integralZoneC = zoneC;
    return ESP_OK;
}

esp_err_t PID::SetIntegralLeakTimeSeconds(double leakTimeSeconds) {
    if (leakTimeSeconds < 0.0) {
        return ESP_ERR_INVALID_ARG;
    }
    integralLeakTimeSeconds = leakTimeSeconds;
    return ESP_OK;
}
