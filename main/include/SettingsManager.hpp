#pragma once

#include "esp_err.h"
#include "nvs.h"
#include "nvs_flash.h"

class SettingsManager {

    public:
        static SettingsManager getInstance();


        // Getters and Setters for Settings go here:
        double GetProportionalGain() const {
            return proportionalGain;
        }
        esp_err_t SetProportionalGain(double newGain) {
            proportionalGain = newGain;
            return NVS_Set_Double(KEY_PROPORTIONAL_GAIN, proportionalGain);
        }



    private:
        // NVS helper variables
        constexpr static const char* NVS_PARTITION = "nvs";
        constexpr static const char* NVS_NAMESPACE = "settings";
        nvs_handle_t m_handle = 0;
        bool nvsOpen = false;

        SettingsManager() = default;
        static SettingsManager* instance;

        esp_err_t OpenNVS();
        esp_err_t CloseNVS();
        esp_err_t LoadSettings();
        esp_err_t nvs_get_double(nvs_handle_t handle, const char* key, double* outValue);
        esp_err_t NVS_Set_Double(const char* key, double value);

        // THESE ARE THE SETTINGS
        // Every setting needs the following defined:
        // 1. A const for the key name ( <= 15 chars )
        // 2. A variable to hold the value in memory
        // 3. A getter function to access the value publically
        // 4. A setter function to change the value publically (which also needs to save the new value to NVS)

        constexpr static const char* KEY_PROPORTIONAL_GAIN = "prop_gain";
        double proportionalGain = 0.0;


};