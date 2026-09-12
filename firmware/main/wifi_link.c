/* Optional LAN transport. Provision only through the existing authenticated path. */
#include "wifi_link.h"
#include "transport.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs.h"
#include "lwip/inet.h"

#define REPLY_MAX 640
static const char *TAG="passport_wifi";
typedef struct {
    uint32_t version;
    bool enabled;
    char ssid[33],password[64],endpoint[97],token[129];
} wifi_preferences_t;
static wifi_preferences_t desired;
static unsigned revision;
static SemaphoreHandle_t lock;
static QueueHandle_t acknowledgements;
static TaskHandle_t task;
static passport_wifi_rx_fn receive_frame;
static atomic_uint link_state; /* 0 off, 1 connecting, 2 syncing, 3 retrying, 4 fallback */
static atomic_bool configured;
static uint64_t millis(void){return esp_timer_get_time()/1000ULL;}

/* Avoid DNS/rebinding and public cleartext destinations. Wi-Fi is a LAN option. */
static bool save(const wifi_preferences_t *p)
{
    nvs_handle_t nvs;
    if(nvs_open("passport",NVS_READWRITE,&nvs)!=ESP_OK)return false;
    esp_err_t error;
    if(!p->ssid[0]) {
        error=nvs_erase_key(nvs,"wifi");
        if(error==ESP_ERR_NVS_NOT_FOUND)error=ESP_OK;
    } else error=nvs_set_blob(nvs,"wifi",p,sizeof(*p));
    if(error==ESP_OK)error=nvs_commit(nvs);
    nvs_close(nvs);return error==ESP_OK;
}
static bool current(wifi_preferences_t *p,unsigned *version)
{
    xSemaphoreTake(lock,portMAX_DELAY);*p=desired;*version=revision;xSemaphoreGive(lock);
    return p->enabled && p->ssid[0];
}
static bool changed(unsigned version)
{
    xSemaphoreTake(lock,portMAX_DELAY);bool result=version!=revision;xSemaphoreGive(lock);return result;
}
static void fallback(unsigned version)
{
    xSemaphoreTake(lock,portMAX_DELAY);
    if(version==revision){desired.enabled=false;(void)save(&desired);atomic_store(&link_state,4);}
    xSemaphoreGive(lock);
}
static int http(const wifi_preferences_t *prefs,const char *path,const char *body,char *output,size_t capacity)
{
    char url[144],auth[140];
    snprintf(url,sizeof(url),"%s%s",prefs->endpoint,path);
    snprintf(auth,sizeof(auth),"Bearer %s",prefs->token);
    esp_http_client_config_t config={.url=url,.timeout_ms=4500,.disable_auto_redirect=true,
        .buffer_size=1024,.transport_type=HTTP_TRANSPORT_OVER_TCP};
    esp_http_client_handle_t client=esp_http_client_init(&config);
    if(!client)return -1;
    int result=-1;
    esp_http_client_set_header(client,"Authorization",auth);
    if(body){esp_http_client_set_method(client,HTTP_METHOD_POST);esp_http_client_set_header(client,"Content-Type","application/json");}
    size_t length=body?strlen(body):0;
    if(esp_http_client_open(client,length)!=ESP_OK)goto done;
    if(body && esp_http_client_write(client,body,length)!=(int)length)goto done;
    if(esp_http_client_fetch_headers(client)<0)goto done;
    result=esp_http_client_get_status_code(client);
    if(result==200 && output) {
        size_t used=0;
        for(;;) {
            int count=esp_http_client_read(client,output+used,capacity-1-used);
            if(count<0){result=-1;break;}
            if(!count)break;
            used+=count;
            if(used>=capacity-1){result=-1;break;}
        }
        output[used]='\0';
        if(!esp_http_client_is_complete_data_received(client))result=-1;
    }
 done:
    esp_http_client_close(client);esp_http_client_cleanup(client);
    memset(auth,0,sizeof(auth));return result;
}
static void worker(void *context)
{
    (void)context;
    for(;;) {
        wifi_preferences_t prefs;unsigned version;
        if(!current(&prefs,&version)){ulTaskNotifyTake(pdTRUE,portMAX_DELAY);continue;}
        atomic_store(&link_state,1);
        esp_netif_t *netif=NULL;
        bool initialized=false,started=false,event_loop=false,failed=false;
        /* Let the encrypted provisioning receipt finish before handing over radio memory. */
        vTaskDelay(pdMS_TO_TICKS(1500));
        if(!passport_ble_pause()){failed=true;goto done;}
        if(esp_netif_init()!=ESP_OK){failed=true;goto done;}
        if(esp_event_loop_create_default()!=ESP_OK){failed=true;goto done;}event_loop=true;
        esp_netif_config_t netif_config=ESP_NETIF_DEFAULT_WIFI_STA();
        netif=esp_netif_new(&netif_config);if(!netif){failed=true;goto done;}
        if(esp_netif_attach_wifi_station(netif)!=ESP_OK || esp_wifi_set_default_wifi_sta_handlers()!=ESP_OK){failed=true;goto done;}
        wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
        init.static_rx_buf_num=4;init.dynamic_rx_buf_num=8;init.dynamic_tx_buf_num=8;
        if(esp_wifi_init(&init)!=ESP_OK){failed=true;goto done;}initialized=true;
        wifi_config_t station={0};
        memcpy(station.sta.ssid,prefs.ssid,strlen(prefs.ssid));
        memcpy(station.sta.password,prefs.password,strlen(prefs.password));
        station.sta.threshold.authmode=WIFI_AUTH_WPA2_PSK;
        esp_wifi_set_storage(WIFI_STORAGE_RAM);
        esp_wifi_set_mode(WIFI_MODE_STA);
        esp_err_t setup=esp_wifi_set_config(WIFI_IF_STA,&station);
        memset(&station,0,sizeof(station));
        if(setup!=ESP_OK || esp_wifi_start()!=ESP_OK){failed=true;goto done;}started=true;
        esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        esp_wifi_connect();
        uint64_t start=millis(),last_success=start,last_connect=start;
        bool delivered=false;
        while(!changed(version)) {
            esp_netif_ip_info_t ip={0};
            if(esp_netif_get_ip_info(netif,&ip)!=ESP_OK || !ip.ip.addr) {
                if(millis()-last_success>25000){failed=true;break;}
                if(millis()-last_connect>7000){esp_wifi_connect();last_connect=millis();}
                vTaskDelay(pdMS_TO_TICKS(250));continue;
            }
            char frame[PASSPORT_LINE_MAX+1];
            int status=http(&prefs,"/v1/device/snapshot",NULL,frame,sizeof(frame));
            if(status==409 && (delivered || millis()-start>10000)){failed=true;break;}
            if(status==200 && receive_frame(frame,strlen(frame))) {
                char reply[REPLY_MAX];
                /* Report only a frame that the UI task actually accepted and applied. */
                if(xQueueReceive(acknowledgements,reply,pdMS_TO_TICKS(3000))==pdTRUE &&
                   http(&prefs,"/v1/device/ack",reply,NULL,0)==200) {
                    last_success=millis();delivered=true;atomic_store(&link_state,2);
                }
                memset(reply,0,sizeof(reply));
            } else atomic_store(&link_state,3);
            memset(frame,0,sizeof(frame));
            if(millis()-last_success>25000){failed=true;break;}
            /* Wait without holding a power lock; configuration changes wake early. */
            ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(3000));
        }
        if(!failed) {
            wifi_preferences_t next;unsigned next_version;
            if(!current(&next,&next_version))(void)http(&prefs,"/v1/route","{\"owner\":\"phone\"}",NULL,0);
            memset(&next,0,sizeof(next));
        }
 done:
        if(started)esp_wifi_stop();
        if(initialized)esp_wifi_deinit();
        if(netif)esp_netif_destroy_default_wifi(netif);
        if(event_loop)esp_event_loop_delete_default();
        xQueueReset(acknowledgements);
        memset(&prefs,0,sizeof(prefs));
        if(failed){fallback(version);ESP_LOGI(TAG,"Wi-Fi ended; BLE remains available");}
        else atomic_store(&link_state,0);
        passport_ble_resume();
    }
}
void passport_wifi_init(passport_wifi_rx_fn callback)
{
    receive_frame=callback;lock=xSemaphoreCreateMutex();acknowledgements=xQueueCreate(2,REPLY_MAX);
    if(!lock || !acknowledgements)return;
    nvs_handle_t nvs;size_t size=sizeof(desired);
    if(nvs_open("passport",NVS_READONLY,&nvs)==ESP_OK) {
        if(nvs_get_blob(nvs,"wifi",&desired,&size)!=ESP_OK || size!=sizeof(desired) || desired.version!=1 ||
           !memchr(desired.endpoint,0,sizeof(desired.endpoint)) || !memchr(desired.ssid,0,sizeof(desired.ssid)) ||
           !memchr(desired.password,0,sizeof(desired.password)) || !memchr(desired.token,0,sizeof(desired.token)) ||
           !passport_private_endpoint(desired.endpoint))memset(&desired,0,sizeof(desired));
        nvs_close(nvs);
    }
    atomic_store(&configured,desired.ssid[0]!=0);
    if(xTaskCreate(worker,"passport_wifi",8192,NULL,3,&task)!=pdPASS)task=NULL;
}
bool passport_wifi_command(const passport_command_t *command)
{
    if(!lock || !task)return false;
    xSemaphoreTake(lock,portMAX_DELAY);
    wifi_preferences_t next=desired;
    if(command->kind==COMMAND_FORGET_WIFI)memset(&next,0,sizeof(next));
    else if(command->kind==COMMAND_WIFI) {
        if(command->enabled && !command->saved) {
            memset(&next,0,sizeof(next));next.version=1;
            memcpy(next.ssid,command->ssid,sizeof(next.ssid));memcpy(next.password,command->password,sizeof(next.password));
            memcpy(next.endpoint,command->endpoint,sizeof(next.endpoint));memcpy(next.token,command->token,sizeof(next.token));
        }
        next.enabled=command->enabled;
    }
    bool ok=(!next.enabled || (next.ssid[0] && strlen(next.password)>=8 && strlen(next.token)>=20 && passport_private_endpoint(next.endpoint))) && save(&next);
    if(ok){desired=next;++revision;atomic_store(&configured,desired.ssid[0]!=0);}
    memset(&next,0,sizeof(next));xSemaphoreGive(lock);
    if(ok)xTaskNotifyGive(task);
    return ok;
}
void passport_wifi_ack(const char *reply)
{
    if(!acknowledgements || strlen(reply)>=REPLY_MAX)return;
    char copy[REPLY_MAX]={0};strcpy(copy,reply);(void)xQueueSend(acknowledgements,copy,0);
}
unsigned passport_wifi_state(void){return atomic_load(&link_state);}
bool passport_wifi_configured(void){return atomic_load(&configured);}
void passport_wifi_cancel(void)
{
    passport_command_t command={.kind=COMMAND_WIFI,.enabled=false};
    (void)passport_wifi_command(&command);
}
