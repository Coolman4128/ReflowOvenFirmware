#include "SettingsManager.hpp"

SettingsManager SettingsManager::getInstance(){
    if (instance == nullptr){
        instance = new SettingsManager();
    }
    return *instance;
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
    *outValue = *reinterpret_cast<double*>(&rawValue);
    return ESP_OK;
}

esp_err_t SettingsManager::LoadSettings() {
    if (!nvsOpen) {
        return ESP_ERR_INVALID_STATE; // NVS must be open to load settings
    }
    
    // ======================================
    // ==ADD LOADING FOR EACH SETTING HERE ==
    // ======================================

    esp_err_t err = nvs_get_double(m_handle, KEY_PROPORTIONAL_GAIN, &proportionalGain);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        // If the error is ESP_ERR_NVS_NOT_FOUND, it just means this setting hasn't been saved before, so we can ignore that error and keep the default value.
        return err; 
    }
    

    return ESP_OK;
}


