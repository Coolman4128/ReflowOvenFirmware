#pragma once

#include "DataManager.hpp"
#include "esp_err.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string>
#include <vector>

class WebServerManager {
public:
    static WebServerManager& getInstance();
    WebServerManager(const WebServerManager&) = delete;
    WebServerManager& operator=(const WebServerManager&) = delete;
    WebServerManager(WebServerManager&&) = delete;
    WebServerManager& operator=(WebServerManager&&) = delete;

    esp_err_t Initialize();
    bool IsInitialized() const { return initialized; }

private:
    WebServerManager() = default;
    static WebServerManager* instance;

    bool initialized = false;
    bool spiffsMounted = false;
    httpd_handle_t server = nullptr;

    TaskHandle_t wsTelemetryTaskHandle = nullptr;
    SemaphoreHandle_t wsClientsMutex = nullptr;
    std::vector<int> wsClients;

    esp_err_t MountSpiffs();
    esp_err_t StartServer();
    esp_err_t RegisterHandlers();
    esp_err_t StartWebsocketTelemetryTask();

    esp_err_t HandleApiRequest(httpd_req_t* req);
    esp_err_t HandleStaticFileRequest(httpd_req_t* req);
    esp_err_t HandleWebsocketRequest(httpd_req_t* req);

    esp_err_t HandleApiGet(httpd_req_t* req, const std::string& path);
    esp_err_t HandleApiPost(httpd_req_t* req, const std::string& path);
    esp_err_t HandleApiPut(httpd_req_t* req, const std::string& path);
    esp_err_t HandleApiDelete(httpd_req_t* req, const std::string& path);

    std::string BuildStatusEnvelopeJson(const char* eventType = "status") const;
    std::string BuildTelemetryEnvelopeJson(const char* eventType) const;

    std::string GetRequestPath(httpd_req_t* req) const;
    std::string GetRequestQuery(httpd_req_t* req) const;
    esp_err_t ReadRequestBody(httpd_req_t* req, std::string& outBody) const;
    esp_err_t SendHistoryJson(httpd_req_t* req, const DataPointStorage& points) const;
    esp_err_t SendHistoryCsv(httpd_req_t* req, const DataPointStorage& points) const;

    esp_err_t SendJsonSuccess(httpd_req_t* req, const std::string& dataJson) const;
    esp_err_t SendJsonError(httpd_req_t* req, int statusCode, const char* code, const char* message) const;

    static esp_err_t ApiGetHandler(httpd_req_t* req);
    static esp_err_t ApiPostHandler(httpd_req_t* req);
    static esp_err_t ApiPutHandler(httpd_req_t* req);
    static esp_err_t ApiDeleteHandler(httpd_req_t* req);
    static esp_err_t WsHandler(httpd_req_t* req);
    static esp_err_t StaticFileHandler(httpd_req_t* req);

    static void WsTelemetryTaskEntry(void* arg);
    void WsTelemetryTaskLoop();
    void BroadcastWebsocketMessage(const std::string& payload);
    bool HasWsClients() const;
    void AddWsClient(int fd);
    void RemoveWsClient(int fd);
};
