#pragma once
#include <stdbool.h>
void passport_preferences_init(void);
bool passport_preferences_set(unsigned brightness,unsigned idle);
unsigned passport_brightness(int battery);
unsigned passport_idle_seconds(void);
unsigned passport_brightness_setting(void);
