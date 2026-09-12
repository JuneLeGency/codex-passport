#pragma once
#include "protocol.h"
typedef bool (*passport_wifi_rx_fn)(const char *,size_t);
void passport_wifi_init(passport_wifi_rx_fn receive);
bool passport_wifi_command(const passport_command_t *command);
void passport_wifi_ack(const char *reply);
unsigned passport_wifi_state(void);
bool passport_wifi_configured(void);
void passport_wifi_cancel(void);
