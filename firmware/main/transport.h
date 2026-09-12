#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef void (*passport_rx_fn)(const uint8_t *,size_t);
void passport_ble_start(passport_rx_fn callback);
void passport_ble_send(const char *data);
bool passport_ble_connected(void);
int passport_ble_passkey(void);

void passport_ble_tick(void);
void passport_ble_reconnect(void);
