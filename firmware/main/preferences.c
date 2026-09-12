#include "preferences.h"
#include "nvs.h"
static unsigned brightness=55,idle=60;
void passport_preferences_init(void)
{
    nvs_handle_t prefs;
    if(nvs_open("passport",NVS_READONLY,&prefs)==ESP_OK) {
        uint8_t value;
        if(nvs_get_u8(prefs,"brightness",&value)==ESP_OK && value>=10 && value<=80)brightness=value;
        if(nvs_get_u8(prefs,"idle",&value)==ESP_OK && value>=15 && value<=120)idle=value;
        nvs_close(prefs);
    }
}
bool passport_preferences_set(unsigned level,unsigned seconds)
{
    if(level<10 || level>80 || seconds<15 || seconds>120)return false;
    if(level==brightness && seconds==idle)return true;
    nvs_handle_t prefs;
    esp_err_t error=nvs_open("passport",NVS_READWRITE,&prefs);
    if(error!=ESP_OK)return false;
    error=nvs_set_u8(prefs,"brightness",level);
    if(error==ESP_OK)error=nvs_set_u8(prefs,"idle",seconds);
    if(error==ESP_OK)error=nvs_commit(prefs);
    nvs_close(prefs);
    if(error==ESP_OK){brightness=level;idle=seconds;}
    return error==ESP_OK;
}
unsigned passport_brightness(int battery){return battery>=0 && battery<=10 && brightness>20?20:brightness;}
unsigned passport_brightness_setting(void){return brightness;}
unsigned passport_idle_seconds(void){return idle;}
