#pragma once
#include <stdbool.h>

/* Call after shared I2C and NVS initialization. Playback never blocks the UI. */
void passport_sound_init(void);
void passport_sound_notify(void);
bool passport_sound_muted(void);
void passport_sound_toggle(void);
unsigned passport_sound_volume(void);
bool passport_sound_configure(bool muted, unsigned volume);
