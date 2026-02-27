#pragma once

#include "esp_err.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <array>
#include <cstdint>
#include <string>

class SettingsManager {

    public:
        static SettingsManager& getInstance();
        SettingsManager(const SettingsManager&) = delete;
        SettingsManager& operator=(const SettingsManager&) = delete;
        SettingsManager(SettingsManager&&) = delete;
        SettingsManager& operator=(SettingsManager&&) = delete;

        esp_err_t Initialize();

        double GetInputFilterTime() const { return inputFilterTime; }
        esp_err_t SetInputFilterTime(double newValue);

        uint8_t GetInputsIncludedMask() const { return inputsIncludedMask; }
        esp_err_t SetInputsIncludedMask(uint8_t newValue);

        double GetProportionalGain() const { return proportionalGain; }
        esp_err_t SetProportionalGain(double newValue);

        double GetIntegralGain() const { return integralGain; }
        esp_err_t SetIntegralGain(double newValue);

        double GetDerivativeGain() const { return derivativeGain; }
        esp_err_t SetDerivativeGain(double newValue);

        double GetDerivativeFilterTime() const { return derivativeFilterTime; }
        esp_err_t SetDerivativeFilterTime(double newValue);

        double GetSetpointWeight() const { return setpointWeight; }
        esp_err_t SetSetpointWeight(double newValue);

        uint8_t GetRelaysPWMMask() const { return relaysPWMMask; }
        esp_err_t SetRelaysPWMMask(uint8_t newValue);
        std::array<double, 8> GetRelayPWMWeights() const { return relayPWMWeights; }
        double GetRelayPWMWeight(int relayIndex) const;
        esp_err_t SetRelayPWMWeight(int relayIndex, double newValue);
        esp_err_t SetRelayPWMWeights(const std::array<double, 8>& newValues);

        uint8_t GetRelaysOnMask() const { return relaysOnMask; }
        esp_err_t SetRelaysOnMask(uint8_t newValue);

        const std::string& GetTimeZone() const { return timeZone; }
        esp_err_t SetTimeZone(const std::string& newValue);

        const std::string& GetWiFiSSID() const { return wifiSSID; }
        esp_err_t SetWiFiSSID(const std::string& newValue);

        const std::string& GetWiFiPassword() const { return wifiPassword; }
        esp_err_t SetWiFiPassword(const std::string& newValue);

        int32_t GetDataLogIntervalMs() const { return dataLogIntervalMs; }
        esp_err_t SetDataLogIntervalMs(int32_t newValue);

        int32_t GetMaxDataLogTimeMs() const { return maxDataLogTimeMs; }
        esp_err_t SetMaxDataLogTimeMs(int32_t newValue);

    private:
        // NVS helper variables
        constexpr static const char* NVS_PARTITION = "nvs";
        constexpr static const char* NVS_NAMESPACE = "settings";
        nvs_handle_t m_handle = 0;
        bool nvsOpen = false;

        SettingsManager() = default;
        static SettingsManager* instance;
        bool initialized = false;

        esp_err_t OpenNVS();
        esp_err_t CloseNVS();
        esp_err_t LoadSettings();
        esp_err_t nvs_get_double(nvs_handle_t handle, const char* key, double* outValue);
        esp_err_t NVS_Set_Double(const char* key, double value);
        esp_err_t NVS_Set_U8(const char* key, uint8_t value);
        esp_err_t NVS_Set_I32(const char* key, int32_t value);
        esp_err_t NVS_Set_String(const char* key, const std::string& value);

        // THESE ARE THE SETTINGS
        // Every setting needs the following defined:
        // 1. A const for the key name ( <= 15 chars )
        // 2. A variable to hold the value in memory
        // 3. A getter function to access the value publically
        // 4. A setter function to change the value publically (which also needs to save the new value to NVS)

        constexpr static const char* KEY_INPUT_FILTER_TIME = "in_filt_t";
        constexpr static const char* KEY_INPUTS_INCLUDED = "in_mask";
        constexpr static const char* KEY_PROPORTIONAL_GAIN = "prop_gain";
        constexpr static const char* KEY_INTEGRAL_GAIN = "int_gain";
        constexpr static const char* KEY_DERIVATIVE_GAIN = "der_gain";
        constexpr static const char* KEY_DERIV_FILTER_TIME = "der_filt_t";
        constexpr static const char* KEY_SETPOINT_WEIGHT = "sp_weight";
        constexpr static const char* KEY_RELAYS_PWM = "rel_pwm";
        constexpr static const char* KEY_RELAYS_ON = "rel_on";
        constexpr static const char* KEY_TIMEZONE = "timezone";
        constexpr static const char* KEY_WIFI_SSID = "wifi_ssid";
        constexpr static const char* KEY_WIFI_PASSWORD = "wifi_pass";
        constexpr static const char* KEY_DATA_LOG_INTERVAL = "log_int_ms";
        constexpr static const char* KEY_MAX_DATA_LOG_TIME = "max_log_ms";

        double inputFilterTime = 1000.0;
        uint8_t inputsIncludedMask = 0x01;
        double proportionalGain = 15.0;
        double integralGain = 2.0;
        double derivativeGain = 0.0;
        double derivativeFilterTime = 0.0;
        double setpointWeight = 0.5;
        uint8_t relaysPWMMask = 0x03;
        std::array<double, 8> relayPWMWeights = {1.0, 0.5, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
        uint8_t relaysOnMask = 0x04;
        std::string timeZone = "EST";
        std::string wifiSSID = "NETGEAR";
        std::string wifiPassword = "TYLERSETUP";
        int32_t dataLogIntervalMs = 1000;
        int32_t maxDataLogTimeMs = 1000 * 60 * 30;


};
