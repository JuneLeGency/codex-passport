if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/../generated/passport_cjk16.c")
    message(FATAL_ERROR "Run python scripts/build_font.py before building firmware")
endif()
idf_component_register(SRCS "main.c" "protocol.c" "transport.c" "controls.c" "../generated/passport_cjk16.c" INCLUDE_DIRS "."
    REQUIRES bsp json bt nvs_flash esp_timer esp_driver_usb_serial_jtag)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
