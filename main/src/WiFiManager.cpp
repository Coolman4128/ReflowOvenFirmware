#include "WiFiManager.hpp"

#include "SettingsManager.hpp"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include <cstring>

namespace {
constexpr EventBits_t WIFI_CONNECTED_BIT = BIT0;
constexpr EventBits_t WIFI_DISCONNECTED_BIT = BIT1;
}

WiFiManager* WiFiManager::instance = nullptr;

WiFiManager& WiFiManager::getInstance() {
    if (instance == nullptr) {
        instance = new WiFiManager();
    }
    return *instance;
}

esp_err_t WiFiManager::Initialize() {
    if (initialized) {
        return ESP_OK;
    }

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    staNetif = esp_netif_create_default_wifi_sta();
    if (staNetif == nullptr) {
        return ESP_FAIL;
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    eventGroup = xEventGroupCreate();
    if (eventGroup == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &WiFiManager::WiFiEventHandler, this, &wifiHandlerInstance);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &WiFiManager::WiFiEventHandler, this, &ipHandlerInstance);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    initialized = true;
    return ESP_OK;
}

std::vector<WiFiNetworkInfo> WiFiManager::ScanNetworks() {
    std::vector<WiFiNetworkInfo> networks;
    if (Initialize() != ESP_OK) {
        return networks;
    }

    wifi_scan_config_t scanConfig = {};
    if (esp_wifi_scan_start(&scanConfig, true) != ESP_OK) {
        return networks;
    }

    uint16_t apCount = 0;
    if (esp_wifi_scan_get_ap_num(&apCount) != ESP_OK || apCount == 0) {
        return networks;
    }

    std::vector<wifi_ap_record_t> records(apCount);
    if (esp_wifi_scan_get_ap_records(&apCount, records.data()) != ESP_OK) {
        return networks;
    }

    networks.reserve(apCount);
    for (uint16_t i = 0; i < apCount; ++i) {
        WiFiNetworkInfo info;
        info.ssid = reinterpret_cast<const char*>(records[i].ssid);
        info.rssi = records[i].rssi;
        info.authMode = records[i].authmode;
        networks.push_back(info);
    }

    return networks;
}

esp_err_t WiFiManager::Connect(const std::string& ssid, const std::string& password, uint32_t timeoutMs) {
    if (ssid.empty()) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = Initialize();
    if (err != ESP_OK) {
        return err;
    }

    wifi_config_t wifiConfig = {};
    std::strncpy(reinterpret_cast<char*>(wifiConfig.sta.ssid), ssid.c_str(), sizeof(wifiConfig.sta.ssid) - 1);
    std::strncpy(reinterpret_cast<char*>(wifiConfig.sta.password), password.c_str(), sizeof(wifiConfig.sta.password) - 1);
    wifiConfig.sta.threshold.authmode = WIFI_AUTH_OPEN;
    wifiConfig.sta.pmf_cfg.capable = true;
    wifiConfig.sta.pmf_cfg.required = false;

    err = esp_wifi_set_config(WIFI_IF_STA, &wifiConfig);
    if (err != ESP_OK) {
        return err;
    }

    xEventGroupClearBits(eventGroup, WIFI_CONNECTED_BIT | WIFI_DISCONNECTED_BIT);

    err = esp_wifi_connect();
    if (err != ESP_OK) {
        return err;
    }

    EventBits_t bits = xEventGroupWaitBits(
        eventGroup,
        WIFI_CONNECTED_BIT | WIFI_DISCONNECTED_BIT,
        pdTRUE,
        pdFALSE,
        pdMS_TO_TICKS(timeoutMs)
    );

    if ((bits & WIFI_CONNECTED_BIT) != 0) {
        SettingsManager& settings = SettingsManager::getInstance();
        err = settings.SetWiFiSSID(ssid);
        if (err != ESP_OK) {
            return err;
        }
        return settings.SetWiFiPassword(password);
    }

    if ((bits & WIFI_DISCONNECTED_BIT) != 0) {
        return ESP_FAIL;
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t WiFiManager::ConnectToSavedNetwork(uint32_t timeoutMs) {
    SettingsManager& settings = SettingsManager::getInstance();
    if (settings.GetWiFiSSID().empty()) {
        return ESP_ERR_NOT_FOUND;
    }
    return Connect(settings.GetWiFiSSID(), settings.GetWiFiPassword(), timeoutMs);
}

esp_err_t WiFiManager::Disconnect() {
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_wifi_disconnect();
}

bool WiFiManager::IsConnected() const {
    if (!initialized || eventGroup == nullptr) {
        return false;
    }
    EventBits_t bits = xEventGroupGetBits(eventGroup);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

void WiFiManager::WiFiEventHandler(void* arg, esp_event_base_t eventBase, int32_t eventId, void* eventData) {
    (void)eventData;
    if (arg == nullptr) {
        return;
    }

    WiFiManager* self = static_cast<WiFiManager*>(arg);
    if (self->eventGroup == nullptr) {
        return;
    }

    EventGroupHandle_t group = self->eventGroup;

    if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupSetBits(group, WIFI_DISCONNECTED_BIT);
        xEventGroupClearBits(group, WIFI_CONNECTED_BIT);
    }

    if (eventBase == IP_EVENT && eventId == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(group, WIFI_CONNECTED_BIT);
        xEventGroupClearBits(group, WIFI_DISCONNECTED_BIT);
    }
}
