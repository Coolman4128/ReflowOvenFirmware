idf_component_register(
    SRCS
        "src/main.cpp"
        "src/HardwareManager.cpp"
        "src/PWM.cpp"
        "src/PID.cpp"
        "src/Controller.cpp"
        "src/app.cpp"
        "src/SettingsManager.cpp"
        "src/DataManager.cpp"
        "src/WiFiManager.cpp"
        "src/TimeManager.cpp"
        "src/WebServerManager.cpp"
    INCLUDE_DIRS
        "include"
    REQUIRES
        esp_http_server
        esp_event
        esp_netif
        esp_timer
        esp_wifi
        esp_driver_spi
        esp_driver_gpio
        esp_driver_mcpwm
        nvs_flash
        spiffs
        json
        app_update
)

set(WEBUI_FIRMWARE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/webui")
set(WEBUI_FRONTEND_DIST_DIR "${CMAKE_SOURCE_DIR}/frontend/dist")

if(EXISTS "${WEBUI_FIRMWARE_DIR}")
    set(WEBUI_ASSETS_DIR "${WEBUI_FIRMWARE_DIR}")
elseif(EXISTS "${WEBUI_FRONTEND_DIST_DIR}")
    message(STATUS "Using frontend/dist as SPIFFS source (main/webui not found)")
    set(WEBUI_ASSETS_DIR "${WEBUI_FRONTEND_DIST_DIR}")
else()
    message(FATAL_ERROR
        "Web UI assets not found. Run:\n"
        "  cd frontend && npm ci && npm run build:firmware\n"
        "or generate frontend/dist before building firmware.")
endif()

spiffs_create_partition_image(spiffs "${WEBUI_ASSETS_DIR}" FLASH_IN_PROJECT)
