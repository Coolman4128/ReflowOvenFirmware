#include "PWM.hpp"

#include <algorithm>
#include "esp_timer.h"
#include "esp_log.h"

static const char* TAG = "PWM";

PWM::PWM(uint32_t period_ms,
                       float duty_cycle,
                       ToggleCallback on_on,
                       ToggleCallback on_off,
                       void* user_ctx)
    : period_ms_(period_ms),
      duty_cycle_(duty_cycle),
      on_on_(on_on),
      on_off_(on_off),
      user_ctx_(user_ctx)
{
    // sanitize inputs
    if (period_ms_ == 0) {
        period_ms_ = 1;
    }
    duty_cycle_ = std::clamp(duty_cycle_, 0.0f, 1.0f);

    RecomputeDurations();
}

PWM::~PWM()
{
    // Best-effort stop + cleanup
    Stop();

    if (timer_ != nullptr) {
        esp_timer_delete(reinterpret_cast<esp_timer_handle_t>(timer_));
        timer_ = nullptr;
    }
}

esp_err_t PWM::Start()
{
    if (running_) {
        return ESP_OK;
    }

    if (timer_ == nullptr) {
        esp_timer_create_args_t args = {};
        args.callback = &PWM::TimerThunk;
        args.arg = this;
        args.dispatch_method = ESP_TIMER_TASK; // simple + safe for user callbacks
        args.name = "PWM";

        esp_timer_handle_t handle = nullptr;
        esp_err_t err = esp_timer_create(&args, &handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_timer_create failed: %s", esp_err_to_name(err));
            return err;
        }
        timer_ = reinterpret_cast<void*>(handle);
    }

    // Start from OFF, schedule first edge based on off_ms_ (or immediate if off_ms_==0).
    state_ = State::Off;
    running_ = true;

    // Invoke OFF callback once at start? Usually you *don't* want that unless forcing state.
    // So we do not call on_off_ here.

    return ScheduleNextEdge();
}

esp_err_t PWM::Stop()
{
    if (!running_) {
        return ESP_OK;
    }

    CancelTimer();
    running_ = false;
    return ESP_OK;
}

esp_err_t PWM::SetPeriodMs(uint32_t period_ms)
{
    if (period_ms == 0) {
        period_ms = 1;
    }
    period_ms_ = period_ms;
    RecomputeDurations();
    // takes effect on next edge; optionally you could reschedule here.
    return ESP_OK;
}

esp_err_t PWM::SetDutyCycle(float duty_cycle)
{
    duty_cycle_ = std::clamp(duty_cycle, 0.0f, 1.0f);
    RecomputeDurations();
    // takes effect on next edge
    return ESP_OK;
}

esp_err_t PWM::ForceOn()
{
    if (state_ != State::On) {
        state_ = State::On;
        if (on_on_) {
            on_on_(user_ctx_);
        }
    }
    if (running_) {
        CancelTimer();
        return ScheduleNextEdge();
    }
    return ESP_OK;
}

esp_err_t PWM::ForceOff()
{
    if (state_ != State::Off) {
        state_ = State::Off;
        if (on_off_) {
            on_off_(user_ctx_);
        }
    }
    if (running_) {
        CancelTimer();
        return ScheduleNextEdge();
    }
    return ESP_OK;
}

void PWM::TimerThunk(void* arg)
{
    auto* self = static_cast<PWM*>(arg);
    self->OnTimer();
}

void PWM::OnTimer()
{
    timer_armed_ = false;

    if (!running_) {
        return;
    }

    // Toggle state
    if (state_ == State::Off) {
        state_ = State::On;
        if (on_on_) {
            on_on_(user_ctx_);
        }
    } else {
        state_ = State::Off;
        if (on_off_) {
            on_off_(user_ctx_);
        }
    }

    // Schedule next edge
    (void)ScheduleNextEdge();
}

void PWM::RecomputeDurations()
{
    // Compute on/off in ms. Ensure they sum to period_ms_ (within rounding).
    // For very low frequency this is fine.
    const float p = static_cast<float>(period_ms_);
    uint32_t on = static_cast<uint32_t>(p * duty_cycle_ + 0.5f);

    if (on > period_ms_) {
        on = period_ms_;
    }
    uint32_t off = period_ms_ - on;

    on_ms_ = on;
    off_ms_ = off;
}

esp_err_t PWM::ScheduleNextEdge()
{
    if (timer_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t delay_ms = (state_ == State::Off) ? off_ms_ : on_ms_;

    // Handle 0ms sections (duty=0 or duty=1). We still need edges:
    // - duty=0 => on_ms_=0, off_ms_=period; state toggles to ON, then immediately to OFF next tick.
    // - duty=1 => off_ms_=0, on_ms_=period; similarly.
    // To avoid a tight loop, force a minimum delay of 1 ms when running.
    if (delay_ms == 0) {
        delay_ms = 1;
    }

    const int64_t delay_us = static_cast<int64_t>(delay_ms) * 1000;

    esp_timer_handle_t handle = reinterpret_cast<esp_timer_handle_t>(timer_);

    // One-shot timer; we re-arm it after each edge.
    esp_err_t err = esp_timer_start_once(handle, delay_us);
    if (err == ESP_ERR_INVALID_STATE) {
        // Timer might still be armed (rare race). Try stopping and rearming.
        esp_timer_stop(handle);
        err = esp_timer_start_once(handle, delay_us);
    }

    if (err == ESP_OK) {
        timer_armed_ = true;
    } else {
        ESP_LOGE(TAG, "esp_timer_start_once failed: %s", esp_err_to_name(err));
    }
    return err;
}

void PWM::CancelTimer()
{
    if (timer_ == nullptr) {
        return;
    }
    if (!timer_armed_) {
        return;
    }

    esp_timer_handle_t handle = reinterpret_cast<esp_timer_handle_t>(timer_);
    (void)esp_timer_stop(handle);
    timer_armed_ = false;
}