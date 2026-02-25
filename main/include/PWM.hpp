#pragma once

#include <cstdint>
#include "esp_err.h"

class PWM
{
public:
    using ToggleCallback = void(*)(void* user_ctx);

    // period_ms: full PWM period in milliseconds (e.g., 1000 for 1 Hz)
    // duty_cycle: 0.0..1.0
    PWM(uint32_t period_ms,
               float duty_cycle,
               ToggleCallback on_on,
               ToggleCallback on_off,
               void* user_ctx = nullptr);

    ~PWM();

    // Start/stop the software PWM. Start() begins in the OFF state, then schedules the first transition.
    esp_err_t Start();
    esp_err_t Stop();

    bool IsRunning() const { return running_; }

    // Update parameters (safe to call while running; takes effect on next edge).
    esp_err_t SetPeriodMs(uint32_t period_ms);
    esp_err_t SetDutyCycle(float duty_cycle); // clamps to [0,1]

    uint32_t GetPeriodMs() const { return period_ms_; }
    float GetDutyCycle() const { return duty_cycle_; }

    // Force immediate state and invoke the corresponding callback.
    // If running, the next edge is rescheduled from "now".
    esp_err_t ForceOn();
    esp_err_t ForceOff();

private:
    enum class State : uint8_t { Off = 0, On = 1 };

    static void TimerThunk(void* arg);
    void OnTimer();

    // Recompute cached on/off durations based on period_ms_ and duty_cycle_
    void RecomputeDurations();

    // Schedule next timer event based on current state.
    esp_err_t ScheduleNextEdge();

    // Schedule a specific delay in microseconds.
    esp_err_t ScheduleDelayUs(int64_t delay_us);

    // Cancel any active timer.
    void CancelTimer();

private:
    uint32_t period_ms_{1000};
    float duty_cycle_{0.5f};

    uint32_t on_ms_{500};
    uint32_t off_ms_{500};

    ToggleCallback on_on_{nullptr};
    ToggleCallback on_off_{nullptr};
    void* user_ctx_{nullptr};

    void* timer_{nullptr}; // esp_timer_handle_t but kept opaque in header

    State state_{State::Off};
    bool running_{false};
    bool timer_armed_{false};
    int64_t section_started_us_{0};
};