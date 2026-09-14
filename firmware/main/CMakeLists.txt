if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/../generated/passport_cjk16.c")
    message(FATAL_ERROR "Run python scripts/build_font.py before building firmware")
endif()
idf_component_register(SRCS "main.c" "protocol.c" "transport.c" "controls.c" "alert_policy.c" "screen_policy.c" "sound.c" "preferences.c" "wifi_link.c" "../generated/passport_cjk16.c" INCLUDE_DIRS "."
    REQUIRES bsp json bt nvs_flash esp_timer esp_driver_usb_serial_jtag esp_driver_ledc esp_codec_dev esp_driver_i2s esp_wifi esp_netif esp_event esp_http_client esp_app_format)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
