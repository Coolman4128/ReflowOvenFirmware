#include "SettingsManager.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

SettingsManager* SettingsManager::instance = nullptr;

namespace {
bool BuildRelayWeightKey(int relayIndex, char* outKey, std::size_t outLen) {
    if (outKey == nullptr || outLen < 6 || relayIndex < 0 || relayIndex > 7) {
        return false;
    }
    std::snprintf(outKey, outLen, "relw%d", relayIndex);
    return true;
}
}

SettingsManager& SettingsManager::getInstance(){
    if (instance == nullptr){
        instance = new SettingsManager();
    }
    return *instance;
}

esp_err_t SettingsManager::Initialize() {
    if (initialized) {
        return ESP_OK;
    }

    esp_err_t err = OpenNVS();
    if (err != ESP_OK) {
        return err;
    }

    err = LoadSettings();
    if (err != ESP_OK) {
        return err;
    }

    initialized = true;
    return ESP_OK;
}


esp_err_t SettingsManager::OpenNVS() {
    if (nvsOpen) {
        return ESP_OK; // Already open
    }

    esp_err_t err = nvs_flash_init_partition(NVS_PARTITION);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init_partition after erasing
        ESP_ERROR_CHECK(nvs_flash_erase_partition(NVS_PARTITION));
        err = nvs_flash_init_partition(NVS_PARTITION);
    }
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_open_from_partition(NVS_PARTITION, NVS_NAMESPACE, NVS_READWRITE, &m_handle);
    if (err == ESP_OK) {
        nvsOpen = true;
    }
    return err;
}

esp_err_t SettingsManager::NVS_Set_Double(const char* key, double value) {
    if (!nvsOpen) {
        return ESP_ERR_INVALID_STATE;
    }

    uint64_t rawValue = 0;
    std::memcpy(&rawValue, &value, sizeof(double));
    esp_err_t err = nvs_set_u64(m_handle, key, rawValue);
    if (err != ESP_OK) {
        return err;
    }
    return nvs_commit(m_handle);
}

esp_err_t SettingsManager::NVS_Set_U8(const char* key, uint8_t value) {
    if (!nvsOpen) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = nvs_set_u8(m_handle, key, value);
    if (err != ESP_OK) {
        return err;
    }
    return nvs_commit(m_handle);
}

esp_err_t SettingsManager::NVS_Set_I32(const char* key, int32_t value) {
    if (!nvsOpen) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = nvs_set_i32(m_handle, key, value);
    if (err != ESP_OK) {
        return err;
    }
    return nvs_commit(m_handle);
}

esp_err_t SettingsManager::NVS_Set_String(const char* key, const std::string& value) {
    if (!nvsOpen) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = nvs_set_str(m_handle, key, value.c_str());
    if (err != ESP_OK) {
        return err;
    }
    return nvs_commit(m_handle);
}

esp_err_t SettingsManager::CloseNVS() {
    if (!nvsOpen) {
        return ESP_OK; // Already closed
    }
    nvs_close(m_handle);
    m_handle = 0;
    nvsOpen = false;
    return ESP_OK;
}

// This is a helper function that read in a double value from the nvs. The nvs has no native double support so we save and load as u_int_64 and then reinterpret the bits as a double
esp_err_t SettingsManager::nvs_get_double(nvs_handle_t handle, const char* key, double* outValue) {
    uint64_t rawValue = 0;
    esp_err_t err = nvs_get_u64(handle, key, &rawValue);
    if (err != ESP_OK) {
        return err;
    }
    std::memcpy(outValue, &rawValue, sizeof(double));
    return ESP_OK;
}

esp_err_t SettingsManager::LoadSettings() {
    if (!nvsOpen) {
        return ESP_ERR_INVALID_STATE; // NVS must be open to load settings
    }
    
    // ======================================
    // ==ADD LOADING FOR EACH SETTING HERE ==
    // ======================================

    esp_err_t err = this->nvs_get_double(m_handle, KEY_HEAT_KP, &heatingProportionalGain);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = this->nvs_get_double(m_handle, KEY_PROPORTIONAL_GAIN, &heatingProportionalGain);
    }
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        // If the error is ESP_ERR_NVS_NOT_FOUND, it just means this setting hasn't been saved before, so we can ignore that error and keep the default value.
        return err;
    }

    err = this->nvs_get_double(m_handle, KEY_INPUT_FILTER_TIME, &inputFilterTime);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    err = nvs_get_u8(m_handle, KEY_INPUTS_INCLUDED, &inputsIncludedMask);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    err = this->nvs_get_double(m_handle, KEY_HEAT_KI, &heatingIntegralGain);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = this->nvs_get_double(m_handle, KEY_INTEGRAL_GAIN, &heatingIntegralGain);
    }
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    err = this->nvs_get_double(m_handle, KEY_HEAT_KD, &heatingDerivativeGain);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = this->nvs_get_double(m_handle, KEY_DERIVATIVE_GAIN, &heatingDerivativeGain);
    }
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    err = this->nvs_get_double(m_handle, KEY_COOL_KP, &coolingProportionalGain);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    err = this->nvs_get_double(m_handle, KEY_COOL_KI, &coolingIntegralGain);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    err = this->nvs_get_double(m_handle, KEY_COOL_KD, &coolingDerivativeGain);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    err = this->nvs_get_double(m_handle, KEY_DERIV_FILTER_TIME, &derivativeFilterTime);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    err = this->nvs_get_double(m_handle, KEY_SETPOINT_WEIGHT, &setpointWeight);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    err = this->nvs_get_double(m_handle, KEY_I_ZONE_C, &integralZoneC);
    if (err == ESP_OK) {
        integralZoneC = std::max(integralZoneC, 0.0);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    err = this->nvs_get_double(m_handle, KEY_I_LEAK_S, &integralLeakTimeSeconds);
    if (err == ESP_OK) {
        integralLeakTimeSeconds = std::max(integralLeakTimeSeconds, 0.0);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    err = nvs_get_u8(m_handle, KEY_RELAYS_PWM, &relaysPWMMask);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    for (int relayIndex = 0; relayIndex < 8; ++relayIndex) {
        char key[16] = {};
        if (!BuildRelayWeightKey(relayIndex, key, sizeof(key))) {
            return ESP_ERR_INVALID_ARG;
        }
        double value = relayPWMWeights[static_cast<std::size_t>(relayIndex)];
        err = this->nvs_get_double(m_handle, key, &value);
        if (err == ESP_OK) {
            relayPWMWeights[static_cast<std::size_t>(relayIndex)] = std::clamp(value, 0.0, 1.0);
        } else if (err != ESP_ERR_NVS_NOT_FOUND) {
            return err;
        }
    }

    err = nvs_get_u8(m_handle, KEY_RELAYS_ON, &relaysOnMask);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    size_t requiredSize = 0;
    err = nvs_get_str(m_handle, KEY_TIMEZONE, nullptr, &requiredSize);
    if (err == ESP_OK && requiredSize > 0) {
        std::vector<char> buffer(requiredSize);
        err = nvs_get_str(m_handle, KEY_TIMEZONE, buffer.data(), &requiredSize);
        if (err != ESP_OK) {
            return err;
        }
        timeZone = std::string(buffer.data());
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    requiredSize = 0;
    err = nvs_get_str(m_handle, KEY_WIFI_SSID, nullptr, &requiredSize);
    if (err == ESP_OK && requiredSize > 0) {
        std::vector<char> buffer(requiredSize);
        err = nvs_get_str(m_handle, KEY_WIFI_SSID, buffer.data(), &requiredSize);
        if (err != ESP_OK) {
            return err;
        }
        wifiSSID = std::string(buffer.data());
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    requiredSize = 0;
    err = nvs_get_str(m_handle, KEY_WIFI_PASSWORD, nullptr, &requiredSize);
    if (err == ESP_OK && requiredSize > 0) {
        std::vector<char> buffer(requiredSize);
        err = nvs_get_str(m_handle, KEY_WIFI_PASSWORD, buffer.data(), &requiredSize);
        if (err != ESP_OK) {
            return err;
        }
        wifiPassword = std::string(buffer.data());
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    err = nvs_get_i32(m_handle, KEY_DATA_LOG_INTERVAL, &dataLogIntervalMs);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    err = nvs_get_i32(m_handle, KEY_MAX_DATA_LOG_TIME, &maxDataLogTimeMs);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    err = this->nvs_get_double(m_handle, KEY_DOOR_CLOSED_ANGLE, &doorClosedAngleDeg);
    if (err == ESP_OK) {
        doorClosedAngleDeg = std::clamp(doorClosedAngleDeg, 0.0, 180.0);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    err = this->nvs_get_double(m_handle, KEY_DOOR_OPEN_ANGLE, &doorOpenAngleDeg);
    if (err == ESP_OK) {
        doorOpenAngleDeg = std::clamp(doorOpenAngleDeg, 0.0, 180.0);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    err = this->nvs_get_double(m_handle, KEY_DOOR_MAX_SPEED, &doorMaxSpeedDegPerSec);
    if (err == ESP_OK) {
        doorMaxSpeedDegPerSec = std::clamp(doorMaxSpeedDegPerSec, 1.0, 360.0);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    err = this->nvs_get_double(m_handle, KEY_COOL_ON_BAND, &coolOnBandC);
    if (err == ESP_OK) {
        coolOnBandC = std::max(coolOnBandC, 0.0);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    err = this->nvs_get_double(m_handle, KEY_COOL_OFF_BAND, &coolOffBandC);
    if (err == ESP_OK) {
        coolOffBandC = std::max(coolOffBandC, 0.0);
    } else if (err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    if (coolOffBandC >= coolOnBandC) {
        coolOnBandC = 5.0;
        coolOffBandC = 2.0;
    }

    return ESP_OK;
}

esp_err_t SettingsManager::SetInputFilterTime(double newValue) {
    inputFilterTime = newValue;
    return NVS_Set_Double(KEY_INPUT_FILTER_TIME, inputFilterTime);
}

esp_err_t SettingsManager::SetInputsIncludedMask(uint8_t newValue) {
    inputsIncludedMask = newValue;
    return NVS_Set_U8(KEY_INPUTS_INCLUDED, inputsIncludedMask);
}

esp_err_t SettingsManager::SetHeatingProportionalGain(double newValue) {
    heatingProportionalGain = newValue;
    esp_err_t err = NVS_Set_Double(KEY_HEAT_KP, heatingProportionalGain);
    if (err != ESP_OK) {
        return err;
    }
    return NVS_Set_Double(KEY_PROPORTIONAL_GAIN, heatingProportionalGain);
}

esp_err_t SettingsManager::SetHeatingIntegralGain(double newValue) {
    heatingIntegralGain = newValue;
    esp_err_t err = NVS_Set_Double(KEY_HEAT_KI, heatingIntegralGain);
    if (err != ESP_OK) {
        return err;
    }
    return NVS_Set_Double(KEY_INTEGRAL_GAIN, heatingIntegralGain);
}

esp_err_t SettingsManager::SetHeatingDerivativeGain(double newValue) {
    heatingDerivativeGain = newValue;
    esp_err_t err = NVS_Set_Double(KEY_HEAT_KD, heatingDerivativeGain);
    if (err != ESP_OK) {
        return err;
    }
    return NVS_Set_Double(KEY_DERIVATIVE_GAIN, heatingDerivativeGain);
}

esp_err_t SettingsManager::SetCoolingProportionalGain(double newValue) {
    coolingProportionalGain = newValue;
    return NVS_Set_Double(KEY_COOL_KP, coolingProportionalGain);
}

esp_err_t SettingsManager::SetCoolingIntegralGain(double newValue) {
    coolingIntegralGain = newValue;
    return NVS_Set_Double(KEY_COOL_KI, coolingIntegralGain);
}

esp_err_t SettingsManager::SetCoolingDerivativeGain(double newValue) {
    coolingDerivativeGain = newValue;
    return NVS_Set_Double(KEY_COOL_KD, coolingDerivativeGain);
}

esp_err_t SettingsManager::SetProportionalGain(double newValue) {
    return SetHeatingProportionalGain(newValue);
}

esp_err_t SettingsManager::SetIntegralGain(double newValue) {
    return SetHeatingIntegralGain(newValue);
}

esp_err_t SettingsManager::SetDerivativeGain(double newValue) {
    return SetHeatingDerivativeGain(newValue);
}

esp_err_t SettingsManager::SetDerivativeFilterTime(double newValue) {
    derivativeFilterTime = newValue;
    return NVS_Set_Double(KEY_DERIV_FILTER_TIME, derivativeFilterTime);
}

esp_err_t SettingsManager::SetSetpointWeight(double newValue) {
    setpointWeight = newValue;
    return NVS_Set_Double(KEY_SETPOINT_WEIGHT, setpointWeight);
}

esp_err_t SettingsManager::SetIntegralZoneC(double newValue) {
    if (newValue < 0.0) {
        return ESP_ERR_INVALID_ARG;
    }
    integralZoneC = newValue;
    return NVS_Set_Double(KEY_I_ZONE_C, integralZoneC);
}

esp_err_t SettingsManager::SetIntegralLeakTimeSeconds(double newValue) {
    if (newValue < 0.0) {
        return ESP_ERR_INVALID_ARG;
    }
    integralLeakTimeSeconds = newValue;
    return NVS_Set_Double(KEY_I_LEAK_S, integralLeakTimeSeconds);
}

esp_err_t SettingsManager::SetRelaysPWMMask(uint8_t newValue) {
    relaysPWMMask = newValue;
    return NVS_Set_U8(KEY_RELAYS_PWM, relaysPWMMask);
}

double SettingsManager::GetRelayPWMWeight(int relayIndex) const {
    if (relayIndex < 0 || relayIndex > 7) {
        return 1.0;
    }
    return relayPWMWeights[static_cast<std::size_t>(relayIndex)];
}

esp_err_t SettingsManager::SetRelayPWMWeight(int relayIndex, double newValue) {
    if (relayIndex < 0 || relayIndex > 7 || newValue < 0.0 || newValue > 1.0) {
        return ESP_ERR_INVALID_ARG;
    }

    relayPWMWeights[static_cast<std::size_t>(relayIndex)] = std::clamp(newValue, 0.0, 1.0);

    char key[16] = {};
    if (!BuildRelayWeightKey(relayIndex, key, sizeof(key))) {
        return ESP_ERR_INVALID_ARG;
    }

    return NVS_Set_Double(key, relayPWMWeights[static_cast<std::size_t>(relayIndex)]);
}

esp_err_t SettingsManager::SetRelayPWMWeights(const std::array<double, 8>& newValues) {
    for (int relayIndex = 0; relayIndex < 8; ++relayIndex) {
        esp_err_t err = SetRelayPWMWeight(relayIndex, newValues[static_cast<std::size_t>(relayIndex)]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

esp_err_t SettingsManager::SetRelaysOnMask(uint8_t newValue) {
    relaysOnMask = newValue;
    return NVS_Set_U8(KEY_RELAYS_ON, relaysOnMask);
}

esp_err_t SettingsManager::SetTimeZone(const std::string& newValue) {
    timeZone = newValue;
    return NVS_Set_String(KEY_TIMEZONE, timeZone);
}

esp_err_t SettingsManager::SetWiFiSSID(const std::string& newValue) {
    wifiSSID = newValue;
    return NVS_Set_String(KEY_WIFI_SSID, wifiSSID);
}

esp_err_t SettingsManager::SetWiFiPassword(const std::string& newValue) {
    wifiPassword = newValue;
    return NVS_Set_String(KEY_WIFI_PASSWORD, wifiPassword);
}

esp_err_t SettingsManager::SetDataLogIntervalMs(int32_t newValue) {
    dataLogIntervalMs = newValue;
    return NVS_Set_I32(KEY_DATA_LOG_INTERVAL, dataLogIntervalMs);
}

esp_err_t SettingsManager::SetMaxDataLogTimeMs(int32_t newValue) {
    maxDataLogTimeMs = newValue;
    return NVS_Set_I32(KEY_MAX_DATA_LOG_TIME, maxDataLogTimeMs);
}

esp_err_t SettingsManager::SetDoorClosedAngleDeg(double newValue) {
    if (newValue < 0.0 || newValue > 180.0) {
        return ESP_ERR_INVALID_ARG;
    }
    doorClosedAngleDeg = newValue;
    return NVS_Set_Double(KEY_DOOR_CLOSED_ANGLE, doorClosedAngleDeg);
}

esp_err_t SettingsManager::SetDoorOpenAngleDeg(double newValue) {
    if (newValue < 0.0 || newValue > 180.0) {
        return ESP_ERR_INVALID_ARG;
    }
    doorOpenAngleDeg = newValue;
    return NVS_Set_Double(KEY_DOOR_OPEN_ANGLE, doorOpenAngleDeg);
}

esp_err_t SettingsManager::SetDoorMaxSpeedDegPerSec(double newValue) {
    if (newValue < 1.0 || newValue > 360.0) {
        return ESP_ERR_INVALID_ARG;
    }
    doorMaxSpeedDegPerSec = newValue;
    return NVS_Set_Double(KEY_DOOR_MAX_SPEED, doorMaxSpeedDegPerSec);
}

esp_err_t SettingsManager::SetCoolOnBandC(double newValue) {
    if (newValue < 0.0 || newValue <= coolOffBandC) {
        return ESP_ERR_INVALID_ARG;
    }
    coolOnBandC = newValue;
    return NVS_Set_Double(KEY_COOL_ON_BAND, coolOnBandC);
}

esp_err_t SettingsManager::SetCoolOffBandC(double newValue) {
    if (newValue < 0.0 || newValue >= coolOnBandC) {
        return ESP_ERR_INVALID_ARG;
    }
    coolOffBandC = newValue;
    return NVS_Set_Double(KEY_COOL_OFF_BAND, coolOffBandC);
}
