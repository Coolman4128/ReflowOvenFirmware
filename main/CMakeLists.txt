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

set(WEBUI_DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/webui")
spiffs_create_partition_image(spiffs "${WEBUI_DIST_DIR}" FLASH_IN_PROJECT)
