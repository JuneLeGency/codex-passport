#include "sound.h"
#include <math.h>
#include <stdint.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "nvs.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"

static const char *TAG="passport_sound";
static QueueHandle_t requests;
static atomic_bool muted;
static atomic_uint volume=65;

/* Original, synthesized two-note chime. No recordings, microphone or RX channel. */
static int16_t sample(unsigned index)
{
    float t=(float)index/16000.0f, start, duration, frequency;
    if(t<0.100f){start=0;duration=0.100f;frequency=880;}
    else if(t>=0.140f && t<0.260f){start=0.140f;duration=0.120f;frequency=1174.66f;}
    else return 0;
    float local=t-start;
    float envelope=fminf(1.0f,fminf(local/0.012f,(duration-local)/0.025f));
    return (int16_t)(4500.0f*envelope*sinf(6.283185307f*frequency*local));
}

static bool play(void)
{
    i2s_chan_handle_t tx=NULL;
    const audio_codec_data_if_t *data=NULL;
    const audio_codec_ctrl_if_t *ctrl=NULL;
    const audio_codec_gpio_if_t *gpio=NULL;
    const audio_codec_if_t *codec=NULL;
    esp_codec_dev_handle_t dev=NULL;
    bool success=false;
    i2s_chan_config_t channel=I2S_CHANNEL_DEFAULT_CONFIG(BSP_I2S_PORT,I2S_ROLE_MASTER);
    channel.dma_desc_num=4;
    channel.dma_frame_num=160;
    channel.auto_clear_after_cb=true;
    if(i2s_new_channel(&channel,&tx,NULL)!=ESP_OK)goto done;
    i2s_std_config_t format={
        .clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO),
        .gpio_cfg={.mclk=BSP_I2S_MCLK,.bclk=BSP_I2S_BCLK,.ws=BSP_I2S_WS,
                   .dout=BSP_I2S_DOUT,.din=I2S_GPIO_UNUSED},
    };
    if(i2s_channel_init_std_mode(tx,&format)!=ESP_OK)goto done;
    if(i2s_channel_enable(tx)!=ESP_OK)goto done;
    /* Codec format negotiation first disables this channel. */
    ctrl=audio_codec_new_i2c_ctrl(&(audio_codec_i2c_cfg_t){
        .port=BSP_I2C_PORT,.addr=BSP_I2C_ES8311_ADDR<<1,.bus_handle=bsp_i2c_bus()});
    data=audio_codec_new_i2s_data(&(audio_codec_i2s_cfg_t){.port=BSP_I2S_PORT,.tx_handle=tx});
    gpio=audio_codec_new_gpio();
    if(!ctrl || !data || !gpio)goto done;
    codec=es8311_codec_new(&(es8311_codec_cfg_t){
        .ctrl_if=ctrl,.gpio_if=gpio,.codec_mode=ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin=BSP_I2S_PA_CTRL,.use_mclk=true,
        .hw_gain={.pa_voltage=5.0f,.codec_dac_voltage=3.3f}});
    if(!codec)goto done;
    dev=esp_codec_dev_new(&(esp_codec_dev_cfg_t){
        .dev_type=ESP_CODEC_DEV_TYPE_OUT,.codec_if=codec,.data_if=data});
    if(!dev)goto done;
    if(esp_codec_dev_set_out_vol(dev,atomic_load(&volume))!=ESP_CODEC_DEV_OK)goto done;
    esp_codec_dev_sample_info_t info={.sample_rate=16000,.bits_per_sample=16,.channel=1};
    if(esp_codec_dev_open(dev,&info)!=ESP_CODEC_DEV_OK)goto done;
    int16_t pcm[160];
    /* 260 ms melody + 80 ms zero samples drain the DMA tail before closing. */
    for(unsigned block=0;block<34;++block) {
        if(atomic_load(&muted))goto done;
        for(unsigned i=0;i<160;++i)pcm[i]=sample(block*160+i);
        if(esp_codec_dev_write(dev,pcm,sizeof(pcm))!=ESP_CODEC_DEV_OK)goto done;
    }
    success=true;
done:
    /* Close disables the DAC and I2S; delete all interfaces we own, not shared I2C.
       The board's PA enable is not connected to the MCU: this cannot cut PA power. */
    if(dev)esp_codec_dev_delete(dev);
    if(codec)audio_codec_delete_codec_if(codec);
    if(data)audio_codec_delete_data_if(data);
    if(ctrl)audio_codec_delete_ctrl_if(ctrl);
    if(gpio)audio_codec_delete_gpio_if(gpio);
    if(tx && i2s_del_channel(tx)!=ESP_OK) {
        /* A failed open can leave the initially enabled channel running. */
        (void)i2s_channel_disable(tx);
        if(i2s_del_channel(tx)!=ESP_OK)ESP_LOGE(TAG,"audio channel cleanup failed");
    }
    return success;
}

static void worker(void *context)
{
    (void)context;
    bool request;
    for(;;) {
        if(xQueueReceive(requests,&request,portMAX_DELAY)!=pdTRUE || atomic_load(&muted))continue;
        bool ok=play();
        ESP_LOGI(TAG,"chime %s; audio released",ok?"played":"stopped/failed");
    }
}

void passport_sound_init(void)
{
    nvs_handle_t prefs;
    if(nvs_open("passport",NVS_READONLY,&prefs)==ESP_OK) {
        uint8_t value=0;
        if(nvs_get_u8(prefs,"muted",&value)==ESP_OK)atomic_store(&muted,value!=0);
        if(nvs_get_u8(prefs,"volume",&value)==ESP_OK && value>=20 && value<=80)atomic_store(&volume,value);
        nvs_close(prefs);
    }
    if(bsp_i2c_init()!=ESP_OK)return;
    requests=xQueueCreate(1,sizeof(bool));
    if(requests && xTaskCreate(worker,"passport_sound",4096,NULL,2,NULL)!=pdPASS) {
        vQueueDelete(requests);requests=NULL;
    }
    if(!requests)ESP_LOGW(TAG,"audio unavailable; visual notifications remain active");
}

void passport_sound_notify(void)
{
    bool request=true;
    if(requests && !atomic_load(&muted))(void)xQueueSend(requests,&request,0);
}

bool passport_sound_muted(void){return atomic_load(&muted);}

void passport_sound_toggle(void)
{
    (void)passport_sound_configure(!atomic_load(&muted),atomic_load(&volume));
}

unsigned passport_sound_volume(void){return atomic_load(&volume);}

bool passport_sound_configure(bool value,unsigned level)
{
    if(level<20 || level>80)return false;
    if(value==atomic_load(&muted) && level==atomic_load(&volume))return true;
    nvs_handle_t prefs;
    esp_err_t error=nvs_open("passport",NVS_READWRITE,&prefs);
    if(error==ESP_OK) {
        error=nvs_set_u8(prefs,"muted",value);
        if(error==ESP_OK)error=nvs_set_u8(prefs,"volume",level);
        if(error==ESP_OK)error=nvs_commit(prefs);
        nvs_close(prefs);
    }
    if(error!=ESP_OK)ESP_LOGW(TAG,"mute preference not saved: %s",esp_err_to_name(error));
    else {atomic_store(&muted,value);atomic_store(&volume,level);}
    return error==ESP_OK;
}
