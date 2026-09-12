idf_component_register(SRCS "main.c" "protocol.c" "transport.c" "controls.c" INCLUDE_DIRS "."
    REQUIRES bsp json bt nvs_flash esp_timer esp_driver_usb_serial_jtag)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
