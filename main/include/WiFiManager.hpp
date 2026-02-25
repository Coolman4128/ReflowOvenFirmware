#pragma once

#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi_types_generic.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <cstdint>
#include <string>
#include <vector>

struct WiFiNetworkInfo {
    std::string ssid;
    int rssi;
    wifi_auth_mode_t authMode;
};

struct WiFiConnectionStatus {
    bool connected = false;
    std::string ssid;
    std::string ipAddress;
    int rssi = -127;
};

class WiFiManager {
public:
    static WiFiManager& getInstance();
    WiFiManager(const WiFiManager&) = delete;
    WiFiManager& operator=(const WiFiManager&) = delete;
    WiFiManager(WiFiManager&&) = delete;
    WiFiManager& operator=(WiFiManager&&) = delete;

    esp_err_t Initialize();
    std::vector<WiFiNetworkInfo> ScanNetworks();
    esp_err_t Connect(const std::string& ssid, const std::string& password, uint32_t timeoutMs = 15000);
    esp_err_t ConnectToSavedNetwork(uint32_t timeoutMs = 15000);
    esp_err_t Disconnect();
    bool IsConnected() const;
    std::string GetConnectedSSID() const;
    int GetConnectedRSSI() const;
    std::string GetLocalIPAddress() const;
    WiFiConnectionStatus GetConnectionStatus() const;

private:
    WiFiManager() = default;
    static WiFiManager* instance;

    bool initialized = false;
    EventGroupHandle_t eventGroup = nullptr;
    esp_netif_t* staNetif = nullptr;
    esp_event_handler_instance_t wifiHandlerInstance = nullptr;
    esp_event_handler_instance_t ipHandlerInstance = nullptr;

    static void WiFiEventHandler(void* arg, esp_event_base_t eventBase, int32_t eventId, void* eventData);
};
