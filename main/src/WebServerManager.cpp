
#include "WebServerManager.hpp"

#include "Controller.hpp"
#include "DataManager.hpp"
#include "HardwareManager.hpp"
#include "PID.hpp"
#include "ProfileEngine.hpp"
#include "TimeManager.hpp"
#include "WiFiManager.hpp"

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_timer.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <sys/stat.h>
#include <unordered_map>
#include <vector>

namespace {
constexpr const char* TAG = "WebServer";
constexpr const char* SPIFFS_BASE_PATH = "/spiffs";
constexpr const char* SPIFFS_PARTITION_LABEL = "spiffs";
constexpr TickType_t WS_TELEMETRY_PERIOD_TICKS = pdMS_TO_TICKS(500);
constexpr TickType_t WS_IDLE_PERIOD_TICKS = pdMS_TO_TICKS(1000);

std::string JsonStringFromObject(cJSON* json) {
    if (json == nullptr) {
        return "{}";
    }

    char* printed = cJSON_PrintUnformatted(json);
    if (printed == nullptr) {
        cJSON_Delete(json);
        return "{}";
    }

    std::string out(printed);
    cJSON_free(printed);
    cJSON_Delete(json);
    return out;
}

bool ParseIntArray(cJSON* arr, std::vector<int>& out) {
    if (!cJSON_IsArray(arr)) {
        return false;
    }

    out.clear();
    const int count = cJSON_GetArraySize(arr);
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        cJSON* entry = cJSON_GetArrayItem(arr, i);
        if (!cJSON_IsNumber(entry)) {
            return false;
        }
        out.push_back(entry->valueint);
    }

    return true;
}

bool ParseRelayWeightArray(cJSON* arr, std::unordered_map<int, double>& out) {
    if (!cJSON_IsArray(arr)) {
        return false;
    }

    out.clear();
    const int count = cJSON_GetArraySize(arr);
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        cJSON* entry = cJSON_GetArrayItem(arr, i);
        if (!cJSON_IsObject(entry)) {
            return false;
        }

        cJSON* relay = cJSON_GetObjectItem(entry, "relay");
        cJSON* weight = cJSON_GetObjectItem(entry, "weight");
        if (!cJSON_IsNumber(relay) || !cJSON_IsNumber(weight)) {
            return false;
        }

        const int relayIndex = relay->valueint;
        const double relayWeight = weight->valuedouble;
        if (relayIndex < 0 || relayIndex > 7 || relayWeight < 0.0 || relayWeight > 1.0) {
            return false;
        }

        out[relayIndex] = relayWeight;
    }

    return true;
}

bool ParseSlotPath(const std::string& path, int& outSlot) {
    constexpr const char* kPrefix = "/api/v1/profiles/slots/";
    if (path.rfind(kPrefix, 0) != 0) {
        return false;
    }

    const std::string suffix = path.substr(std::strlen(kPrefix));
    if (suffix.empty()) {
        return false;
    }

    for (char ch : suffix) {
        if (ch < '0' || ch > '9') {
            return false;
        }
    }

    outSlot = std::atoi(suffix.c_str());
    return true;
}

std::string BuildValidationMessage(const std::vector<ProfileValidationError>& errors) {
    if (errors.empty()) {
        return "Profile validation failed";
    }

    const ProfileValidationError& first = errors.front();
    std::string message = "Profile validation failed: ";
    if (first.stepIndex >= 0) {
        message += "step ";
        message += std::to_string(first.stepIndex + 1);
        message += " ";
    }
    if (!first.field.empty()) {
        message += first.field;
        message += " ";
    }
    message += first.message;
    return message;
}

const char* ContentTypeForPath(const std::string& path) {
    if (path.size() >= 5 && path.substr(path.size() - 5) == ".html") return "text/html; charset=utf-8";
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".css") return "text/css; charset=utf-8";
    if (path.size() >= 3 && path.substr(path.size() - 3) == ".js") return "application/javascript; charset=utf-8";
    if (path.size() >= 5 && path.substr(path.size() - 5) == ".json") return "application/json; charset=utf-8";
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".png") return "image/png";
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".svg") return "image/svg+xml";
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".ico") return "image/x-icon";
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".csv") return "text/csv; charset=utf-8";
    return "application/octet-stream";
}

cJSON* BuildStatusDataObject() {
    Controller& controller = Controller::getInstance();
    DataManager& dataManager = DataManager::getInstance();
    ProfileEngine& profileEngine = ProfileEngine::getInstance();
    WiFiManager& wifiManager = WiFiManager::getInstance();
    TimeManager& timeManager = TimeManager::getInstance();
    HardwareManager& hardware = HardwareManager::getInstance();

    cJSON* root = cJSON_CreateObject();

    cJSON* controllerObj = cJSON_CreateObject();
    cJSON_AddBoolToObject(controllerObj, "running", controller.IsRunning());
    cJSON_AddBoolToObject(controllerObj, "door_open", controller.IsDoorOpen());
    cJSON_AddBoolToObject(controllerObj, "alarming", controller.IsAlarming());
    cJSON_AddStringToObject(controllerObj, "state", controller.GetState().c_str());
    cJSON_AddNumberToObject(controllerObj, "setpoint_c", controller.GetSetPoint());
    cJSON_AddNumberToObject(controllerObj, "process_value_c", controller.GetProcessValue());
    cJSON_AddNumberToObject(controllerObj, "pid_output", controller.GetPIDOutput());
    cJSON_AddNumberToObject(controllerObj, "p_term", controller.GetPIDController()->GetPreviousP());
    cJSON_AddNumberToObject(controllerObj, "i_term", controller.GetPIDController()->GetPreviousI());
    cJSON_AddNumberToObject(controllerObj, "d_term", controller.GetPIDController()->GetPreviousD());
    cJSON_AddItemToObject(root, "controller", controllerObj);

    const ProfileRuntimeStatus profileStatus = profileEngine.GetRuntimeStatus();
    cJSON* profileObj = cJSON_CreateObject();
    cJSON_AddBoolToObject(profileObj, "running", profileStatus.running);
    cJSON_AddStringToObject(profileObj, "name", profileStatus.name.c_str());
    cJSON_AddStringToObject(profileObj, "source", profileStatus.source.c_str());
    cJSON_AddNumberToObject(profileObj, "slot_index", profileStatus.slotIndex);
    cJSON_AddNumberToObject(profileObj, "current_step_number", profileStatus.currentStepNumber);
    cJSON_AddStringToObject(profileObj, "current_step_type", profileStatus.currentStepType.c_str());
    cJSON_AddNumberToObject(profileObj, "step_elapsed_s", profileStatus.stepElapsedS);
    cJSON_AddNumberToObject(profileObj, "profile_elapsed_s", profileStatus.profileElapsedS);
    cJSON_AddStringToObject(profileObj, "last_end_reason", profileStatus.lastEndReason.c_str());
    cJSON_AddItemToObject(root, "profile", profileObj);

    cJSON* hardwareObj = cJSON_CreateObject();
    cJSON* temperatures = cJSON_CreateArray();
    for (int i = 0; i < 4; ++i) {
        cJSON_AddItemToArray(temperatures, cJSON_CreateNumber(hardware.getThermocoupleValue(i)));
    }
    cJSON_AddItemToObject(hardwareObj, "temperatures_c", temperatures);

    cJSON* relays = cJSON_CreateArray();
    for (int i = 0; i < 6; ++i) {
        cJSON_AddItemToArray(relays, cJSON_CreateBool(hardware.getRelayState(i)));
    }
    cJSON_AddItemToObject(hardwareObj, "relay_states", relays);
    cJSON_AddNumberToObject(hardwareObj, "servo_angle", hardware.getServoAngle());
    cJSON_AddItemToObject(root, "hardware", hardwareObj);

    const WiFiConnectionStatus wifiStatus = wifiManager.GetConnectionStatus();
    cJSON* wifiObj = cJSON_CreateObject();
    cJSON_AddBoolToObject(wifiObj, "connected", wifiStatus.connected);
    cJSON_AddStringToObject(wifiObj, "ssid", wifiStatus.ssid.c_str());
    cJSON_AddStringToObject(wifiObj, "ip", wifiStatus.ipAddress.c_str());
    cJSON_AddNumberToObject(wifiObj, "rssi", wifiStatus.rssi);
    cJSON_AddItemToObject(root, "wifi", wifiObj);

    cJSON* timeObj = cJSON_CreateObject();
    cJSON_AddBoolToObject(timeObj, "synced", timeManager.IsSynced());
    cJSON_AddNumberToObject(timeObj, "unix_time_ms", static_cast<double>(timeManager.GetCurrentUnixTimeMs()));
    cJSON_AddStringToObject(timeObj, "timezone", timeManager.GetTimezone().c_str());
    cJSON_AddItemToObject(root, "time", timeObj);

    cJSON* dataObj = cJSON_CreateObject();
    cJSON_AddBoolToObject(dataObj, "logging_enabled", dataManager.IsLogging());
    cJSON_AddNumberToObject(dataObj, "log_interval_ms", dataManager.GetDataLogIntervalMs());
    cJSON_AddNumberToObject(dataObj, "max_time_ms", dataManager.GetMaxTimeSavedMS());
    cJSON_AddNumberToObject(dataObj, "points", static_cast<double>(dataManager.GetDataPointCount()));
    cJSON_AddNumberToObject(dataObj, "bytes_used", static_cast<double>(dataManager.GetStorageBytesUsed()));
    cJSON_AddNumberToObject(dataObj, "max_points", static_cast<double>(dataManager.GetMaxDataPoints()));
    cJSON_AddItemToObject(root, "data", dataObj);

    cJSON* featuresObj = cJSON_CreateObject();
    cJSON_AddBoolToObject(featuresObj, "profiles_support_execution", true);
    cJSON_AddItemToObject(root, "features", featuresObj);

    return root;
}
}

WebServerManager* WebServerManager::instance = nullptr;

WebServerManager& WebServerManager::getInstance() {
    if (instance == nullptr) {
        instance = new WebServerManager();
    }
    return *instance;
}

esp_err_t WebServerManager::Initialize() {
    if (initialized) {
        return ESP_OK;
    }

    if (wsClientsMutex == nullptr) {
        wsClientsMutex = xSemaphoreCreateMutex();
        if (wsClientsMutex == nullptr) {
            return ESP_ERR_NO_MEM;
        }
    }

    esp_err_t err = MountSpiffs();
    if (err != ESP_OK) {
        return err;
    }

    err = StartServer();
    if (err != ESP_OK) {
        return err;
    }

    err = StartWebsocketTelemetryTask();
    if (err != ESP_OK) {
        return err;
    }

    initialized = true;
    return ESP_OK;
}

esp_err_t WebServerManager::MountSpiffs() {
    if (spiffsMounted) {
        return ESP_OK;
    }

    esp_vfs_spiffs_conf_t conf = {};
    conf.base_path = SPIFFS_BASE_PATH;
    conf.partition_label = SPIFFS_PARTITION_LABEL;
    conf.max_files = 8;
    conf.format_if_mount_failed = true;

    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to mount SPIFFS: %s", esp_err_to_name(err));
        return err;
    }

    spiffsMounted = true;
    return ESP_OK;
}

esp_err_t WebServerManager::StartServer() {
    if (server != nullptr) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 20;
    // cJSON + float formatting in status/config endpoints can exceed the default
    // httpd stack on ESP32-S3. Use a larger stack to avoid stack corruption/panics.
    config.stack_size = 8192;

    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    return RegisterHandlers();
}

esp_err_t WebServerManager::RegisterHandlers() {
    if (server == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    httpd_uri_t apiGet = {};
    apiGet.uri = "/api/v1/*";
    apiGet.method = HTTP_GET;
    apiGet.handler = &WebServerManager::ApiGetHandler;
    apiGet.user_ctx = this;

    httpd_uri_t apiPost = {};
    apiPost.uri = "/api/v1/*";
    apiPost.method = HTTP_POST;
    apiPost.handler = &WebServerManager::ApiPostHandler;
    apiPost.user_ctx = this;

    httpd_uri_t apiPut = {};
    apiPut.uri = "/api/v1/*";
    apiPut.method = HTTP_PUT;
    apiPut.handler = &WebServerManager::ApiPutHandler;
    apiPut.user_ctx = this;

    httpd_uri_t apiDelete = {};
    apiDelete.uri = "/api/v1/*";
    apiDelete.method = HTTP_DELETE;
    apiDelete.handler = &WebServerManager::ApiDeleteHandler;
    apiDelete.user_ctx = this;

    httpd_uri_t ws = {};
    ws.uri = "/ws";
    ws.method = HTTP_GET;
    ws.handler = &WebServerManager::WsHandler;
    ws.user_ctx = this;
    ws.is_websocket = true;

    httpd_uri_t files = {};
    files.uri = "/*";
    files.method = HTTP_GET;
    files.handler = &WebServerManager::StaticFileHandler;
    files.user_ctx = this;

    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &apiGet));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &apiPost));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &apiPut));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &apiDelete));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &ws));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &files));

    return ESP_OK;
}

esp_err_t WebServerManager::StartWebsocketTelemetryTask() {
    if (wsTelemetryTaskHandle != nullptr) {
        return ESP_OK;
    }

    BaseType_t result;
#if CONFIG_FREERTOS_UNICORE
    result = xTaskCreate(
        &WebServerManager::WsTelemetryTaskEntry,
        "WsTelemetryTask",
        4096,
        this,
        1,
        &wsTelemetryTaskHandle
    );
#else
    result = xTaskCreatePinnedToCore(
        &WebServerManager::WsTelemetryTaskEntry,
        "WsTelemetryTask",
        4096,
        this,
        1,
        &wsTelemetryTaskHandle,
        1
    );
#endif

    return (result == pdPASS) ? ESP_OK : ESP_FAIL;
}

void WebServerManager::WsTelemetryTaskEntry(void* arg) {
    if (arg == nullptr) {
        vTaskDelete(nullptr);
        return;
    }

    static_cast<WebServerManager*>(arg)->WsTelemetryTaskLoop();
    vTaskDelete(nullptr);
}

void WebServerManager::WsTelemetryTaskLoop() {
    while (true) {
        if (!HasWsClients()) {
            vTaskDelay(WS_IDLE_PERIOD_TICKS);
            continue;
        }
        BroadcastWebsocketMessage(BuildTelemetryEnvelopeJson("telemetry"));
        vTaskDelay(WS_TELEMETRY_PERIOD_TICKS);
    }
}

void WebServerManager::BroadcastWebsocketMessage(const std::string& payload) {
    if (server == nullptr || payload.empty()) {
        return;
    }

    std::vector<int> clientsCopy;
    if (wsClientsMutex != nullptr && xSemaphoreTake(wsClientsMutex, portMAX_DELAY) == pdTRUE) {
        clientsCopy = wsClients;
        xSemaphoreGive(wsClientsMutex);
    }

    for (int fd : clientsCopy) {
        httpd_ws_frame_t frame = {};
        frame.type = HTTPD_WS_TYPE_TEXT;
        frame.payload = reinterpret_cast<uint8_t*>(const_cast<char*>(payload.c_str()));
        frame.len = payload.size();

        esp_err_t err = httpd_ws_send_frame_async(server, fd, &frame);
        if (err != ESP_OK) {
            RemoveWsClient(fd);
        }
    }
}

bool WebServerManager::HasWsClients() const {
    if (wsClientsMutex == nullptr) {
        return false;
    }

    bool hasClients = false;
    if (xSemaphoreTake(wsClientsMutex, portMAX_DELAY) == pdTRUE) {
        hasClients = !wsClients.empty();
        xSemaphoreGive(wsClientsMutex);
    }

    return hasClients;
}

void WebServerManager::AddWsClient(int fd) {
    if (fd < 0 || wsClientsMutex == nullptr) {
        return;
    }

    if (xSemaphoreTake(wsClientsMutex, portMAX_DELAY) == pdTRUE) {
        if (std::find(wsClients.begin(), wsClients.end(), fd) == wsClients.end()) {
            wsClients.push_back(fd);
        }
        xSemaphoreGive(wsClientsMutex);
    }
}

void WebServerManager::RemoveWsClient(int fd) {
    if (fd < 0 || wsClientsMutex == nullptr) {
        return;
    }

    if (xSemaphoreTake(wsClientsMutex, portMAX_DELAY) == pdTRUE) {
        wsClients.erase(std::remove(wsClients.begin(), wsClients.end(), fd), wsClients.end());
        xSemaphoreGive(wsClientsMutex);
    }
}

std::string WebServerManager::BuildStatusEnvelopeJson(const char* eventType) const {
    cJSON* envelope = cJSON_CreateObject();
    cJSON_AddBoolToObject(envelope, "ok", true);
    cJSON_AddStringToObject(envelope, "type", eventType);
    cJSON_AddItemToObject(envelope, "data", BuildStatusDataObject());
    return JsonStringFromObject(envelope);
}

std::string WebServerManager::BuildTelemetryEnvelopeJson(const char* eventType) const {
    cJSON* envelope = cJSON_CreateObject();
    cJSON_AddStringToObject(envelope, "type", eventType);
    cJSON_AddItemToObject(envelope, "data", BuildStatusDataObject());
    return JsonStringFromObject(envelope);
}

std::string WebServerManager::GetRequestPath(httpd_req_t* req) const {
    if (req == nullptr) {
        return "";
    }

    std::string path(req->uri);
    const std::size_t queryStart = path.find('?');
    if (queryStart != std::string::npos) {
        path = path.substr(0, queryStart);
    }
    return path;
}

std::string WebServerManager::GetRequestQuery(httpd_req_t* req) const {
    if (req == nullptr) {
        return "";
    }

    const std::size_t len = httpd_req_get_url_query_len(req);
    if (len == 0) {
        return "";
    }

    std::string query(len + 1, '\0');
    if (httpd_req_get_url_query_str(req, query.data(), query.size()) != ESP_OK) {
        return "";
    }

    query.resize(std::strlen(query.c_str()));
    return query;
}

esp_err_t WebServerManager::ReadRequestBody(httpd_req_t* req, std::string& outBody) const {
    outBody.clear();
    if (req == nullptr || req->content_len <= 0) {
        return ESP_OK;
    }

    int remaining = req->content_len;
    outBody.reserve(static_cast<std::size_t>(req->content_len));
    while (remaining > 0) {
        char buffer[256] = {};
        const int toRead = std::min(remaining, static_cast<int>(sizeof(buffer)));
        const int received = httpd_req_recv(req, buffer, toRead);
        if (received <= 0) {
            return ESP_FAIL;
        }
        outBody.append(buffer, static_cast<std::size_t>(received));
        remaining -= received;
    }

    return ESP_OK;
}

esp_err_t WebServerManager::SendHistoryJson(httpd_req_t* req, const DataPointStorage& points) const {
    if (req == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_status(req, "200 OK");

    esp_err_t err = httpd_resp_send_chunk(req, "{\"ok\":true,\"data\":{\"points\":[", HTTPD_RESP_USE_STRLEN);
    if (err != ESP_OK) {
        return err;
    }

    for (std::size_t idx = 0; idx < points.size(); ++idx) {
        if (idx > 0) {
            err = httpd_resp_send_chunk(req, ",", 1);
            if (err != ESP_OK) {
                return err;
            }
        }

        const DataPoint& point = points[idx];
        char pointJson[512] = {};
        const int written = std::snprintf(
            pointJson,
            sizeof(pointJson),
            "{\"timestamp\":%llu,\"setpoint\":%.3f,\"process_value\":%.3f,\"pid_output\":%.3f,\"p\":%.3f,\"i\":%.3f,\"d\":%.3f,"
            "\"temperatures\":[%.3f,%.3f,%.3f,%.3f],\"relay_states\":%u,\"servo_angle\":%u,\"running\":%s}",
            static_cast<unsigned long long>(point.timestamp),
            point.setPoint,
            point.processValue,
            point.PIDOutput,
            point.PTerm,
            point.ITerm,
            point.DTerm,
            point.temperatureReadings[0],
            point.temperatureReadings[1],
            point.temperatureReadings[2],
            point.temperatureReadings[3],
            static_cast<unsigned>(point.relayStates),
            static_cast<unsigned>(point.servoAngle),
            point.chamberRunning ? "true" : "false");
        if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(pointJson)) {
            return ESP_FAIL;
        }

        err = httpd_resp_send_chunk(req, pointJson, static_cast<std::size_t>(written));
        if (err != ESP_OK) {
            return err;
        }
    }

    err = httpd_resp_send_chunk(req, "]}}", HTTPD_RESP_USE_STRLEN);
    if (err != ESP_OK) {
        return err;
    }

    return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t WebServerManager::SendHistoryCsv(httpd_req_t* req, const DataPointStorage& points) const {
    if (req == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    httpd_resp_set_type(req, "text/csv; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=history.csv");

    esp_err_t err = httpd_resp_send_chunk(
        req,
        "timestamp,setpoint,process_value,pid_output,p_term,i_term,d_term,temp0,temp1,temp2,temp3,relay_states,servo_angle,running\n",
        HTTPD_RESP_USE_STRLEN);
    if (err != ESP_OK) {
        return err;
    }

    for (const DataPoint& point : points) {
        char line[320] = {};
        const int written = std::snprintf(
            line,
            sizeof(line),
            "%llu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%u,%u,%u\n",
            static_cast<unsigned long long>(point.timestamp),
            point.setPoint,
            point.processValue,
            point.PIDOutput,
            point.PTerm,
            point.ITerm,
            point.DTerm,
            point.temperatureReadings[0],
            point.temperatureReadings[1],
            point.temperatureReadings[2],
            point.temperatureReadings[3],
            static_cast<unsigned>(point.relayStates),
            static_cast<unsigned>(point.servoAngle),
            point.chamberRunning ? 1U : 0U);
        if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(line)) {
            return ESP_FAIL;
        }

        err = httpd_resp_send_chunk(req, line, static_cast<std::size_t>(written));
        if (err != ESP_OK) {
            return err;
        }
    }

    return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t WebServerManager::SendJsonSuccess(httpd_req_t* req, const std::string& dataJson) const {
    if (req == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    std::string body = "{\"ok\":true,\"data\":" + (dataJson.empty() ? "{}" : dataJson) + "}";
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_status(req, "200 OK");
    return httpd_resp_send(req, body.c_str(), body.size());
}

esp_err_t WebServerManager::SendJsonError(httpd_req_t* req, int statusCode, const char* code, const char* message) const {
    if (req == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    const char* status = "500 Internal Server Error";
    if (statusCode == 400) status = "400 Bad Request";
    else if (statusCode == 404) status = "404 Not Found";
    else if (statusCode == 409) status = "409 Conflict";

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", false);
    cJSON* errorObj = cJSON_CreateObject();
    cJSON_AddStringToObject(errorObj, "code", (code != nullptr) ? code : "UNKNOWN");
    cJSON_AddStringToObject(errorObj, "message", (message != nullptr) ? message : "Unknown error");
    cJSON_AddItemToObject(root, "error", errorObj);

    std::string payload = JsonStringFromObject(root);
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_status(req, status);
    return httpd_resp_send(req, payload.c_str(), payload.size());
}

esp_err_t WebServerManager::ApiGetHandler(httpd_req_t* req) {
    auto* self = static_cast<WebServerManager*>(req->user_ctx);
    return (self == nullptr) ? ESP_FAIL : self->HandleApiRequest(req);
}

esp_err_t WebServerManager::ApiPostHandler(httpd_req_t* req) {
    auto* self = static_cast<WebServerManager*>(req->user_ctx);
    return (self == nullptr) ? ESP_FAIL : self->HandleApiRequest(req);
}

esp_err_t WebServerManager::ApiPutHandler(httpd_req_t* req) {
    auto* self = static_cast<WebServerManager*>(req->user_ctx);
    return (self == nullptr) ? ESP_FAIL : self->HandleApiRequest(req);
}

esp_err_t WebServerManager::ApiDeleteHandler(httpd_req_t* req) {
    auto* self = static_cast<WebServerManager*>(req->user_ctx);
    return (self == nullptr) ? ESP_FAIL : self->HandleApiRequest(req);
}

esp_err_t WebServerManager::HandleApiRequest(httpd_req_t* req) {
    if (req == nullptr) {
        return ESP_FAIL;
    }

    const std::string path = GetRequestPath(req);

    if (req->method == HTTP_GET) {
        return HandleApiGet(req, path);
    }
    if (req->method == HTTP_POST) {
        return HandleApiPost(req, path);
    }
    if (req->method == HTTP_PUT) {
        return HandleApiPut(req, path);
    }
    if (req->method == HTTP_DELETE) {
        return HandleApiDelete(req, path);
    }

    return SendJsonError(req, 404, "NOT_FOUND", "Endpoint not found");
}
esp_err_t WebServerManager::HandleApiGet(httpd_req_t* req, const std::string& path) {
    if (path == "/api/v1/status") {
        return SendJsonSuccess(req, JsonStringFromObject(BuildStatusDataObject()));
    }

    if (path == "/api/v1/controller/config") {
        Controller& controller = Controller::getInstance();
        PID* pid = controller.GetPIDController();

        cJSON* root = cJSON_CreateObject();

        cJSON* pidObj = cJSON_CreateObject();
        cJSON_AddNumberToObject(pidObj, "kp", pid->GetKp());
        cJSON_AddNumberToObject(pidObj, "ki", pid->GetKi());
        cJSON_AddNumberToObject(pidObj, "kd", pid->GetKd());
        cJSON_AddNumberToObject(pidObj, "derivative_filter_s", pid->GetDerivativeFilterTime());
        cJSON_AddNumberToObject(pidObj, "setpoint_weight", pid->GetSetpointWeight());
        cJSON_AddItemToObject(root, "pid", pidObj);

        cJSON_AddNumberToObject(root, "input_filter_ms", controller.GetInputFilterTimeMs());

        cJSON* inputs = cJSON_CreateArray();
        for (int channel : controller.GetInputChannels()) {
            cJSON_AddItemToArray(inputs, cJSON_CreateNumber(channel));
        }
        cJSON_AddItemToObject(root, "inputs", inputs);

        cJSON* relaysObj = cJSON_CreateObject();
        cJSON* pwmRelays = cJSON_CreateArray();
        for (int relay : controller.GetRelaysPWMEnabled()) {
            cJSON_AddItemToArray(pwmRelays, cJSON_CreateNumber(relay));
        }
        cJSON_AddItemToObject(relaysObj, "pwm_relays", pwmRelays);

        cJSON* pwmRelayWeights = cJSON_CreateArray();
        const std::unordered_map<int, double> relayWeights = controller.GetRelaysPWMWeights();
        std::vector<int> sortedRelays;
        sortedRelays.reserve(relayWeights.size());
        for (const auto& entry : relayWeights) {
            sortedRelays.push_back(entry.first);
        }
        std::sort(sortedRelays.begin(), sortedRelays.end());
        for (int relay : sortedRelays) {
            auto it = relayWeights.find(relay);
            if (it == relayWeights.end()) {
                continue;
            }
            cJSON* item = cJSON_CreateObject();
            cJSON_AddNumberToObject(item, "relay", relay);
            cJSON_AddNumberToObject(item, "weight", it->second);
            cJSON_AddItemToArray(pwmRelayWeights, item);
        }
        cJSON_AddItemToObject(relaysObj, "pwm_relay_weights", pwmRelayWeights);

        cJSON* runningRelays = cJSON_CreateArray();
        for (int relay : controller.GetRelaysWhenRunning()) {
            cJSON_AddItemToArray(runningRelays, cJSON_CreateNumber(relay));
        }
        cJSON_AddItemToObject(relaysObj, "running_relays", runningRelays);
        cJSON_AddItemToObject(root, "relays", relaysObj);

        return SendJsonSuccess(req, JsonStringFromObject(root));
    }

    if (path == "/api/v1/settings/time") {
        TimeManager& time = TimeManager::getInstance();
        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "timezone", time.GetTimezone().c_str());
        cJSON_AddBoolToObject(root, "synced", time.IsSynced());
        cJSON_AddNumberToObject(root, "unix_time_ms", static_cast<double>(time.GetCurrentUnixTimeMs()));
        return SendJsonSuccess(req, JsonStringFromObject(root));
    }

    if (path == "/api/v1/settings/wifi/status") {
        const WiFiConnectionStatus status = WiFiManager::getInstance().GetConnectionStatus();
        cJSON* root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "connected", status.connected);
        cJSON_AddStringToObject(root, "ssid", status.ssid.c_str());
        cJSON_AddStringToObject(root, "ip", status.ipAddress.c_str());
        cJSON_AddNumberToObject(root, "rssi", status.rssi);
        return SendJsonSuccess(req, JsonStringFromObject(root));
    }

    if (path == "/api/v1/settings/wifi/networks") {
        std::vector<WiFiNetworkInfo> networks = WiFiManager::getInstance().ScanNetworks();
        cJSON* root = cJSON_CreateObject();
        cJSON* arr = cJSON_CreateArray();
        for (const WiFiNetworkInfo& network : networks) {
            cJSON* item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "ssid", network.ssid.c_str());
            cJSON_AddNumberToObject(item, "rssi", network.rssi);
            cJSON_AddNumberToObject(item, "auth_mode", static_cast<int>(network.authMode));
            cJSON_AddItemToArray(arr, item);
        }
        cJSON_AddItemToObject(root, "networks", arr);
        return SendJsonSuccess(req, JsonStringFromObject(root));
    }

    if (path == "/api/v1/settings/data") {
        DataManager& data = DataManager::getInstance();
        cJSON* root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "logging_enabled", data.IsLogging());
        cJSON_AddNumberToObject(root, "log_interval_ms", data.GetDataLogIntervalMs());
        cJSON_AddNumberToObject(root, "max_time_ms", data.GetMaxTimeSavedMS());
        cJSON_AddNumberToObject(root, "points", static_cast<double>(data.GetDataPointCount()));
        cJSON_AddNumberToObject(root, "bytes_used", static_cast<double>(data.GetStorageBytesUsed()));
        cJSON_AddNumberToObject(root, "max_points", static_cast<double>(data.GetMaxDataPoints()));
        return SendJsonSuccess(req, JsonStringFromObject(root));
    }

    if (path == "/api/v1/data/history") {
        std::size_t limit = 0;
        const std::string query = GetRequestQuery(req);
        if (!query.empty()) {
            char value[16] = {};
            if (httpd_query_key_value(query.c_str(), "limit", value, sizeof(value)) == ESP_OK) {
                limit = static_cast<std::size_t>(std::strtoul(value, nullptr, 10));
            }
        }

        const DataPointStorage points = DataManager::getInstance().GetRecentData(limit);
        return SendHistoryJson(req, points);
    }

    if (path == "/api/v1/data/export.csv") {
        const DataPointStorage points = DataManager::getInstance().GetAllData();
        return SendHistoryCsv(req, points);
    }

    if (path == "/api/v1/system/info") {
        const esp_app_desc_t* app = esp_app_get_description();
        esp_chip_info_t chip = {};
        esp_chip_info(&chip);

        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "project_name", app->project_name);
        cJSON_AddStringToObject(root, "version", app->version);
        cJSON_AddStringToObject(root, "idf_version", app->idf_ver);
        cJSON_AddStringToObject(root, "build_date", app->date);
        cJSON_AddStringToObject(root, "build_time", app->time);
        cJSON_AddNumberToObject(root, "chip_model", chip.model);
        cJSON_AddNumberToObject(root, "chip_cores", chip.cores);
        cJSON_AddNumberToObject(root, "chip_revision", chip.revision);

        return SendJsonSuccess(req, JsonStringFromObject(root));
    }

    if (path == "/api/v1/profiles") {
        ProfileEngine& profiles = ProfileEngine::getInstance();
        cJSON* root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "supports_execution", true);

        cJSON* limits = cJSON_CreateObject();
        cJSON_AddNumberToObject(limits, "max_slots", ProfileEngine::MAX_SLOTS);
        cJSON_AddNumberToObject(limits, "max_steps", ProfileEngine::MAX_STEPS);
        cJSON_AddItemToObject(root, "limits", limits);

        cJSON* uploaded = cJSON_CreateObject();
        const std::optional<ProfileDefinition> uploadedProfile = profiles.GetUploadedProfile();
        cJSON_AddBoolToObject(uploaded, "present", uploadedProfile.has_value());
        if (uploadedProfile.has_value()) {
            cJSON_AddStringToObject(uploaded, "name", uploadedProfile->name.c_str());
            cJSON_AddNumberToObject(uploaded, "step_count", static_cast<double>(uploadedProfile->steps.size()));
        }
        cJSON_AddItemToObject(root, "uploaded", uploaded);

        cJSON* slots = cJSON_CreateArray();
        const auto summaries = profiles.GetSlotSummaries();
        for (const ProfileSlotSummary& summary : summaries) {
            cJSON* slotObj = cJSON_CreateObject();
            cJSON_AddNumberToObject(slotObj, "slot_index", summary.slotIndex);
            cJSON_AddBoolToObject(slotObj, "occupied", summary.occupied);
            cJSON_AddStringToObject(slotObj, "name", summary.name.c_str());
            cJSON_AddNumberToObject(slotObj, "step_count", static_cast<double>(summary.stepCount));
            cJSON_AddItemToArray(slots, slotObj);
        }
        cJSON_AddItemToObject(root, "slots", slots);

        return SendJsonSuccess(req, JsonStringFromObject(root));
    }

    if (path == "/api/v1/profiles/uploaded") {
        const std::optional<ProfileDefinition> uploadedProfile = ProfileEngine::getInstance().GetUploadedProfile();
        if (!uploadedProfile.has_value()) {
            return SendJsonError(req, 404, "PROFILE_NOT_FOUND", "No uploaded profile in memory");
        }

        return SendJsonSuccess(req, ProfileEngine::getInstance().SerializeProfileJson(uploadedProfile.value()));
    }

    int slotIndex = -1;
    if (ParseSlotPath(path, slotIndex)) {
        if (slotIndex < 0 || slotIndex >= ProfileEngine::MAX_SLOTS) {
            return SendJsonError(req, 400, "PROFILE_SLOT_INVALID", "slot index must be in [0,4]");
        }

        ProfileDefinition slotProfile;
        esp_err_t err = ProfileEngine::getInstance().GetSlotProfile(slotIndex, slotProfile);
        if (err == ESP_ERR_NOT_FOUND) {
            return SendJsonError(req, 404, "PROFILE_NOT_FOUND", "Profile slot is empty");
        }
        if (err != ESP_OK) {
            return SendJsonError(req, 500, "PROFILE_LOAD_FAILED", esp_err_to_name(err));
        }

        return SendJsonSuccess(req, ProfileEngine::getInstance().SerializeProfileJson(slotProfile));
    }

    return SendJsonError(req, 404, "NOT_FOUND", "Endpoint not found");
}

esp_err_t WebServerManager::HandleApiPost(httpd_req_t* req, const std::string& path) {
    if (path == "/api/v1/control/start") {
        esp_err_t err = Controller::getInstance().Start();
        if (err != ESP_OK) {
            return SendJsonError(req, 409, "START_FAILED", esp_err_to_name(err));
        }
        return SendJsonSuccess(req, "{}");
    }

    if (path == "/api/v1/control/stop") {
        ProfileEngine& profiles = ProfileEngine::getInstance();
        if (profiles.IsRunning()) {
            esp_err_t err = profiles.CancelRunning(ProfileEndReason::CancelledByUser);
            if (err != ESP_OK) {
                return SendJsonError(req, 409, "STOP_FAILED", esp_err_to_name(err));
            }
            return SendJsonSuccess(req, "{}");
        }

        esp_err_t err = Controller::getInstance().Stop();
        if (err != ESP_OK) {
            return SendJsonError(req, 409, "STOP_FAILED", esp_err_to_name(err));
        }
        return SendJsonSuccess(req, "{}");
    }

    if (path == "/api/v1/control/setpoint") {
        std::string body;
        if (ReadRequestBody(req, body) != ESP_OK) {
            return SendJsonError(req, 400, "BAD_BODY", "Failed to read request body");
        }

        cJSON* json = cJSON_Parse(body.c_str());
        if (json == nullptr) {
            return SendJsonError(req, 400, "BAD_JSON", "Invalid JSON");
        }

        cJSON* setpoint = cJSON_GetObjectItem(json, "setpoint_c");
        if (!cJSON_IsNumber(setpoint)) {
            cJSON_Delete(json);
            return SendJsonError(req, 400, "BAD_SETPOINT", "setpoint_c must be numeric");
        }

        if (Controller::getInstance().IsSetpointLockedByProfile()) {
            cJSON_Delete(json);
            return SendJsonError(req, 409, "PROFILE_SETPOINT_LOCKED", "setpoint is locked while a profile is running");
        }

        esp_err_t err = Controller::getInstance().SetSetPoint(setpoint->valuedouble);
        cJSON_Delete(json);
        if (err != ESP_OK) {
            return SendJsonError(req, 400, "SETPOINT_FAILED", esp_err_to_name(err));
        }

        return SendJsonSuccess(req, "{}");
    }

    if (path == "/api/v1/profiles/uploaded") {
        std::string body;
        if (ReadRequestBody(req, body) != ESP_OK) {
            return SendJsonError(req, 400, "BAD_BODY", "Failed to read request body");
        }

        ProfileDefinition parsedProfile;
        std::vector<ProfileValidationError> errors;
        esp_err_t err = ProfileEngine::getInstance().ParseProfileJson(body, parsedProfile, errors);
        if (err != ESP_OK) {
            return SendJsonError(req, 400, "PROFILE_VALIDATION_FAILED", BuildValidationMessage(errors).c_str());
        }

        err = ProfileEngine::getInstance().SetUploadedProfile(parsedProfile, &errors);
        if (err != ESP_OK) {
            return SendJsonError(req, 400, "PROFILE_VALIDATION_FAILED", BuildValidationMessage(errors).c_str());
        }

        return SendJsonSuccess(req, "{}");
    }

    if (path == "/api/v1/profiles/run") {
        std::string body;
        if (ReadRequestBody(req, body) != ESP_OK) {
            return SendJsonError(req, 400, "BAD_BODY", "Failed to read request body");
        }

        cJSON* json = cJSON_Parse(body.c_str());
        if (json == nullptr) {
            return SendJsonError(req, 400, "BAD_JSON", "Invalid JSON");
        }

        cJSON* source = cJSON_GetObjectItem(json, "source");
        if (!cJSON_IsString(source) || source->valuestring == nullptr) {
            cJSON_Delete(json);
            return SendJsonError(req, 400, "BAD_PROFILE_RUN_ARGS", "source must be 'uploaded' or 'slot'");
        }

        esp_err_t err = ESP_OK;
        const std::string sourceValue = source->valuestring;
        if (sourceValue == "uploaded") {
            err = ProfileEngine::getInstance().StartFromUploaded();
        } else if (sourceValue == "slot") {
            cJSON* slotIndex = cJSON_GetObjectItem(json, "slot_index");
            if (!cJSON_IsNumber(slotIndex)) {
                cJSON_Delete(json);
                return SendJsonError(req, 400, "BAD_PROFILE_RUN_ARGS", "slot_index must be numeric when source is slot");
            }
            if (slotIndex->valueint < 0 || slotIndex->valueint >= ProfileEngine::MAX_SLOTS) {
                cJSON_Delete(json);
                return SendJsonError(req, 400, "PROFILE_SLOT_INVALID", "slot index must be in [0,4]");
            }
            err = ProfileEngine::getInstance().StartFromSlot(slotIndex->valueint);
        } else {
            cJSON_Delete(json);
            return SendJsonError(req, 400, "BAD_PROFILE_RUN_ARGS", "source must be 'uploaded' or 'slot'");
        }

        cJSON_Delete(json);

        if (err == ESP_ERR_INVALID_STATE && ProfileEngine::getInstance().IsRunning()) {
            return SendJsonError(req, 409, "PROFILE_ALREADY_RUNNING", "A profile is already running");
        }
        if (err == ESP_ERR_NOT_FOUND) {
            return SendJsonError(req, 404, "PROFILE_NOT_FOUND", "Requested profile source was not found");
        }
        if (err == ESP_ERR_INVALID_ARG) {
            return SendJsonError(req, 400, "PROFILE_VALIDATION_FAILED", "Profile failed validation");
        }
        if (err != ESP_OK) {
            return SendJsonError(req, 409, "PROFILE_START_FAILED", esp_err_to_name(err));
        }

        return SendJsonSuccess(req, "{}");
    }

    if (path == "/api/v1/settings/wifi/connect") {
        std::string body;
        if (ReadRequestBody(req, body) != ESP_OK) {
            return SendJsonError(req, 400, "BAD_BODY", "Failed to read request body");
        }

        cJSON* json = cJSON_Parse(body.c_str());
        if (json == nullptr) {
            return SendJsonError(req, 400, "BAD_JSON", "Invalid JSON");
        }

        cJSON* ssid = cJSON_GetObjectItem(json, "ssid");
        cJSON* password = cJSON_GetObjectItem(json, "password");
        if (!cJSON_IsString(ssid) || !cJSON_IsString(password)) {
            cJSON_Delete(json);
            return SendJsonError(req, 400, "BAD_WIFI_ARGS", "ssid and password are required");
        }

        esp_err_t err = WiFiManager::getInstance().Connect(ssid->valuestring, password->valuestring);
        cJSON_Delete(json);
        if (err != ESP_OK) {
            return SendJsonError(req, 400, "WIFI_CONNECT_FAILED", esp_err_to_name(err));
        }

        return SendJsonSuccess(req, "{}");
    }

    if (path == "/api/v1/settings/wifi/disconnect") {
        esp_err_t err = WiFiManager::getInstance().Disconnect();
        if (err != ESP_OK) {
            return SendJsonError(req, 400, "WIFI_DISCONNECT_FAILED", esp_err_to_name(err));
        }

        return SendJsonSuccess(req, "{}");
    }

    return SendJsonError(req, 404, "NOT_FOUND", "Endpoint not found");
}
esp_err_t WebServerManager::HandleApiPut(httpd_req_t* req, const std::string& path) {
    std::string body;
    if (ReadRequestBody(req, body) != ESP_OK) {
        return SendJsonError(req, 400, "BAD_BODY", "Failed to read request body");
    }

    cJSON* json = cJSON_Parse(body.c_str());
    if (json == nullptr) {
        return SendJsonError(req, 400, "BAD_JSON", "Invalid JSON");
    }

    int slotIndex = -1;
    if (ParseSlotPath(path, slotIndex)) {
        if (slotIndex < 0 || slotIndex >= ProfileEngine::MAX_SLOTS) {
            cJSON_Delete(json);
            return SendJsonError(req, 400, "PROFILE_SLOT_INVALID", "slot index must be in [0,4]");
        }

        ProfileDefinition parsedProfile;
        std::vector<ProfileValidationError> errors;
        esp_err_t err = ProfileEngine::getInstance().ParseProfileJson(body, parsedProfile, errors);
        if (err != ESP_OK) {
            cJSON_Delete(json);
            const std::string message = BuildValidationMessage(errors);
            return SendJsonError(req, 400, "PROFILE_VALIDATION_FAILED", message.c_str());
        }

        err = ProfileEngine::getInstance().SaveProfileToSlot(slotIndex, parsedProfile);
        cJSON_Delete(json);
        if (err == ESP_ERR_INVALID_STATE) {
            return SendJsonError(req, 409, "SLOT_OCCUPIED", "Slot already occupied; delete it first");
        }
        if (err != ESP_OK) {
            return SendJsonError(req, 500, "PROFILE_SAVE_FAILED", esp_err_to_name(err));
        }

        return SendJsonSuccess(req, "{}");
    }

    if (path == "/api/v1/controller/config/pid") {
        cJSON* kp = cJSON_GetObjectItem(json, "kp");
        cJSON* ki = cJSON_GetObjectItem(json, "ki");
        cJSON* kd = cJSON_GetObjectItem(json, "kd");
        cJSON* derivativeFilter = cJSON_GetObjectItem(json, "derivative_filter_s");
        cJSON* setpointWeight = cJSON_GetObjectItem(json, "setpoint_weight");

        if (!cJSON_IsNumber(kp) || !cJSON_IsNumber(ki) || !cJSON_IsNumber(kd) || !cJSON_IsNumber(derivativeFilter) ||
            (setpointWeight != nullptr && !cJSON_IsNumber(setpointWeight))) {
            cJSON_Delete(json);
            return SendJsonError(req, 400, "BAD_PID_ARGS", "kp, ki, kd, derivative_filter_s are required numeric fields. setpoint_weight must be numeric if provided");
        }

        Controller& controller = Controller::getInstance();
        esp_err_t err = controller.SetPIDGains(kp->valuedouble, ki->valuedouble, kd->valuedouble);
        if (err == ESP_OK) {
            err = controller.SetDerivativeFilterTime(derivativeFilter->valuedouble);
        }
        if (err == ESP_OK && setpointWeight != nullptr) {
            err = controller.SetSetpointWeight(setpointWeight->valuedouble);
        }

        cJSON_Delete(json);
        if (err != ESP_OK) {
            return SendJsonError(req, 400, "PID_UPDATE_FAILED", esp_err_to_name(err));
        }

        return SendJsonSuccess(req, "{}");
    }

    if (path == "/api/v1/controller/config/filter") {
        cJSON* filter = cJSON_GetObjectItem(json, "input_filter_ms");
        if (!cJSON_IsNumber(filter)) {
            cJSON_Delete(json);
            return SendJsonError(req, 400, "BAD_FILTER_ARGS", "input_filter_ms is required numeric field");
        }

        esp_err_t err = Controller::getInstance().SetInputFilterTime(filter->valuedouble);
        cJSON_Delete(json);
        if (err != ESP_OK) {
            return SendJsonError(req, 400, "FILTER_UPDATE_FAILED", esp_err_to_name(err));
        }

        return SendJsonSuccess(req, "{}");
    }

    if (path == "/api/v1/controller/config/inputs") {
        cJSON* channels = cJSON_GetObjectItem(json, "channels");
        std::vector<int> parsed;
        if (!ParseIntArray(channels, parsed)) {
            cJSON_Delete(json);
            return SendJsonError(req, 400, "BAD_INPUTS_ARGS", "channels must be an integer array");
        }

        esp_err_t err = Controller::getInstance().SetInputChannels(parsed);
        cJSON_Delete(json);
        if (err != ESP_OK) {
            return SendJsonError(req, 400, "INPUTS_UPDATE_FAILED", esp_err_to_name(err));
        }

        return SendJsonSuccess(req, "{}");
    }

    if (path == "/api/v1/controller/config/relays") {
        cJSON* pwmRelays = cJSON_GetObjectItem(json, "pwm_relays");
        cJSON* runningRelays = cJSON_GetObjectItem(json, "running_relays");
        cJSON* pwmRelayWeights = cJSON_GetObjectItem(json, "pwm_relay_weights");

        std::vector<int> parsedPwm;
        std::vector<int> parsedRunning;
        if (!ParseIntArray(pwmRelays, parsedPwm) || !ParseIntArray(runningRelays, parsedRunning)) {
            cJSON_Delete(json);
            return SendJsonError(req, 400, "BAD_RELAYS_ARGS", "pwm_relays and running_relays must be integer arrays");
        }

        std::unordered_map<int, double> parsedWeights;
        if (pwmRelayWeights != nullptr && !ParseRelayWeightArray(pwmRelayWeights, parsedWeights)) {
            cJSON_Delete(json);
            return SendJsonError(req, 400, "BAD_RELAYS_ARGS", "pwm_relay_weights must be an array of {relay, weight} entries with weight in [0,1]");
        }

        Controller& controller = Controller::getInstance();
        esp_err_t err = ESP_OK;
        if (pwmRelayWeights != nullptr) {
            std::unordered_map<int, double> mergedWeights;
            for (int relay : parsedPwm) {
                mergedWeights[relay] = 1.0;
            }
            for (const auto& entry : parsedWeights) {
                if (mergedWeights.find(entry.first) == mergedWeights.end()) {
                    cJSON_Delete(json);
                    return SendJsonError(req, 400, "BAD_RELAYS_ARGS", "every pwm_relay_weights relay must also be listed in pwm_relays");
                }
                mergedWeights[entry.first] = entry.second;
            }
            err = controller.SetRelaysPWM(mergedWeights);
        } else {
            err = controller.SetRelayPWMEnabled(parsedPwm);
        }
        if (err == ESP_OK) {
            err = controller.SetRelaysWhenRunning(parsedRunning);
        }

        cJSON_Delete(json);
        if (err != ESP_OK) {
            return SendJsonError(req, 400, "RELAYS_UPDATE_FAILED", esp_err_to_name(err));
        }

        return SendJsonSuccess(req, "{}");
    }

    if (path == "/api/v1/settings/time") {
        cJSON* timezone = cJSON_GetObjectItem(json, "timezone");
        if (!cJSON_IsString(timezone)) {
            cJSON_Delete(json);
            return SendJsonError(req, 400, "BAD_TIME_ARGS", "timezone must be a string");
        }

        esp_err_t err = TimeManager::getInstance().SetTimezone(timezone->valuestring);
        cJSON_Delete(json);
        if (err != ESP_OK) {
            return SendJsonError(req, 400, "TIME_UPDATE_FAILED", esp_err_to_name(err));
        }

        return SendJsonSuccess(req, "{}");
    }

    if (path == "/api/v1/settings/data") {
        cJSON* enabled = cJSON_GetObjectItem(json, "logging_enabled");
        cJSON* interval = cJSON_GetObjectItem(json, "log_interval_ms");
        cJSON* maxTime = cJSON_GetObjectItem(json, "max_time_ms");

        if (!cJSON_IsBool(enabled) || !cJSON_IsNumber(interval) || !cJSON_IsNumber(maxTime)) {
            cJSON_Delete(json);
            return SendJsonError(req, 400, "BAD_DATA_ARGS", "logging_enabled, log_interval_ms, max_time_ms are required");
        }

        DataManager& data = DataManager::getInstance();
        esp_err_t err = data.ChangeDataLogInterval(interval->valueint);
        if (err == ESP_OK) {
            err = data.ChangeMaxTimeSaved(maxTime->valueint);
        }
        if (err == ESP_OK) {
            err = data.SetLoggingEnabled(cJSON_IsTrue(enabled));
        }

        cJSON_Delete(json);
        if (err != ESP_OK) {
            return SendJsonError(req, 400, "DATA_UPDATE_FAILED", esp_err_to_name(err));
        }

        return SendJsonSuccess(req, "{}");
    }

    cJSON_Delete(json);
    return SendJsonError(req, 404, "NOT_FOUND", "Endpoint not found");
}

esp_err_t WebServerManager::HandleApiDelete(httpd_req_t* req, const std::string& path) {
    if (path == "/api/v1/data/history") {
        esp_err_t err = DataManager::getInstance().ClearData();
        if (err != ESP_OK) {
            return SendJsonError(req, 500, "CLEAR_FAILED", esp_err_to_name(err));
        }

        return SendJsonSuccess(req, "{}");
    }

    if (path == "/api/v1/profiles/uploaded") {
        ProfileEngine::getInstance().ClearUploadedProfile();
        return SendJsonSuccess(req, "{}");
    }

    int slotIndex = -1;
    if (ParseSlotPath(path, slotIndex)) {
        if (slotIndex < 0 || slotIndex >= ProfileEngine::MAX_SLOTS) {
            return SendJsonError(req, 400, "PROFILE_SLOT_INVALID", "slot index must be in [0,4]");
        }

        const esp_err_t err = ProfileEngine::getInstance().DeleteSlotProfile(slotIndex);
        if (err != ESP_OK) {
            return SendJsonError(req, 500, "PROFILE_DELETE_FAILED", esp_err_to_name(err));
        }

        return SendJsonSuccess(req, "{}");
    }

    return SendJsonError(req, 404, "NOT_FOUND", "Endpoint not found");
}

esp_err_t WebServerManager::WsHandler(httpd_req_t* req) {
    auto* self = static_cast<WebServerManager*>(req->user_ctx);
    return (self == nullptr) ? ESP_FAIL : self->HandleWebsocketRequest(req);
}

esp_err_t WebServerManager::HandleWebsocketRequest(httpd_req_t* req) {
    if (req == nullptr) {
        return ESP_FAIL;
    }

    if (req->method == HTTP_GET) {
        const int fd = httpd_req_to_sockfd(req);
        AddWsClient(fd);

        const std::string payload = BuildTelemetryEnvelopeJson("hello");
        httpd_ws_frame_t frame = {};
        frame.type = HTTPD_WS_TYPE_TEXT;
        frame.payload = reinterpret_cast<uint8_t*>(const_cast<char*>(payload.c_str()));
        frame.len = payload.size();
        return httpd_ws_send_frame(req, &frame);
    }

    httpd_ws_frame_t frame = {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) {
        return err;
    }

    if (frame.len > 0) {
        std::vector<uint8_t> payload(frame.len + 1, 0);
        frame.payload = payload.data();
        err = httpd_ws_recv_frame(req, &frame, frame.len);
        if (err != ESP_OK) {
            return err;
        }
    }

    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        RemoveWsClient(httpd_req_to_sockfd(req));
    }

    return ESP_OK;
}

esp_err_t WebServerManager::StaticFileHandler(httpd_req_t* req) {
    auto* self = static_cast<WebServerManager*>(req->user_ctx);
    return (self == nullptr) ? ESP_FAIL : self->HandleStaticFileRequest(req);
}

esp_err_t WebServerManager::HandleStaticFileRequest(httpd_req_t* req) {
    if (req == nullptr) {
        return ESP_FAIL;
    }

    const std::string path = GetRequestPath(req);
    if (path.rfind("/api/", 0) == 0 || path == "/ws") {
        return SendJsonError(req, 404, "NOT_FOUND", "Endpoint not found");
    }

    std::string localPath = path;
    if (localPath.empty() || localPath == "/") {
        localPath = "/index.html";
    }

    if (localPath.find("..") != std::string::npos) {
        return SendJsonError(req, 400, "BAD_PATH", "Invalid path");
    }

    std::string filePath = std::string(SPIFFS_BASE_PATH) + localPath;
    struct stat st = {};
    if (stat(filePath.c_str(), &st) != 0) {
        filePath = std::string(SPIFFS_BASE_PATH) + "/index.html";
        if (stat(filePath.c_str(), &st) != 0) {
            return SendJsonError(req, 404, "NOT_FOUND", "Static file not found");
        }
    }

    FILE* file = std::fopen(filePath.c_str(), "rb");
    if (file == nullptr) {
        return SendJsonError(req, 500, "FILE_OPEN_FAILED", "Failed to open static file");
    }

    httpd_resp_set_type(req, ContentTypeForPath(filePath));
    char buffer[1024] = {};
    while (!std::feof(file)) {
        const std::size_t read = std::fread(buffer, 1, sizeof(buffer), file);
        if (read > 0) {
            esp_err_t err = httpd_resp_send_chunk(req, buffer, read);
            if (err != ESP_OK) {
                std::fclose(file);
                return err;
            }
        }
    }

    std::fclose(file);
    return httpd_resp_send_chunk(req, nullptr, 0);
}
