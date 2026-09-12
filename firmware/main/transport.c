#include "transport.h"
#include <stdatomic.h>
#include <string.h>
#include <stdio.h>
#include "esp_random.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "host/ble_gap.h"
#include "host/ble_sm.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "esp_mac.h"
#include "esp_log.h"

static const ble_uuid128_t service=BLE_UUID128_INIT(0x01,0x74,0x72,0x6f,0x70,0x73,0x73,0x61,0x70,0x2d,0x78,0x65,0x64,0x6f,0xc0,0xfa);
static const ble_uuid128_t rx_uuid=BLE_UUID128_INIT(0x02,0x74,0x72,0x6f,0x70,0x73,0x73,0x61,0x70,0x2d,0x78,0x65,0x64,0x6f,0xc0,0xfa);
static const ble_uuid128_t tx_uuid=BLE_UUID128_INIT(0x03,0x74,0x72,0x6f,0x70,0x73,0x73,0x61,0x70,0x2d,0x78,0x65,0x64,0x6f,0xc0,0xfa);
static passport_rx_fn receive;
static uint16_t connection=BLE_HS_CONN_HANDLE_NONE, tx_handle;
static atomic_bool secured;
static int64_t connected_at;
static atomic_int passkey=-1;
static uint8_t own_address;
static char name[24];
extern void ble_store_config_init(void);

static int access_rx(uint16_t handle,uint16_t attr,struct ble_gatt_access_ctxt *ctxt,void *arg)
{
    (void)handle; (void)attr; (void)arg;
    if(!atomic_load(&secured)) return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    uint16_t len=OS_MBUF_PKTLEN(ctxt->om);
    uint8_t chunk[512];
    if(len>sizeof(chunk)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    if(ble_hs_mbuf_to_flat(ctxt->om,chunk,sizeof(chunk),&len)) return BLE_ATT_ERR_UNLIKELY;
    receive(chunk,len);
    return 0;
}
static const struct ble_gatt_svc_def services[]={
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&service.u,
     .characteristics=(struct ble_gatt_chr_def[]){
        {.uuid=&rx_uuid.u,.access_cb=access_rx,.flags=BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_WRITE_ENC|BLE_GATT_CHR_F_WRITE_AUTHEN},
        {.uuid=&tx_uuid.u,.access_cb=access_rx,.val_handle=&tx_handle,.flags=BLE_GATT_CHR_F_NOTIFY},
        {0}}},
    {0}
};
static void advertise(void);
static int gap_event(struct ble_gap_event *event,void *arg)
{
    (void)arg;
    switch(event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if(event->connect.status) { advertise(); break; }
        ESP_LOGI("passport","BLE connected");
        connection=event->connect.conn_handle;
        connected_at=esp_timer_get_time();
        atomic_store(&secured,false);
        ble_gap_security_initiate(connection);
        {
            struct ble_gap_upd_params p={.itvl_min=72,.itvl_max=144,.latency=4,.supervision_timeout=600};
            ble_gap_update_params(connection,&p);
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        connection=BLE_HS_CONN_HANDLE_NONE;
        atomic_store(&secured,false); atomic_store(&passkey,-1);
        receive(NULL,0); /* Discard a partial frame at the connection boundary. */
        advertise();
        break;
    case BLE_GAP_EVENT_ENC_CHANGE: {
        ESP_LOGI("passport","Encryption result: %d",event->enc_change.status);
        struct ble_gap_conn_desc d;
        if(!ble_gap_conn_find(event->enc_change.conn_handle,&d)) {
            atomic_store(&secured,event->enc_change.status==0 && d.sec_state.encrypted && d.sec_state.authenticated);
        }
        atomic_store(&passkey,-1);
        if(!atomic_load(&secured)) ble_gap_terminate(event->enc_change.conn_handle,BLE_ERR_AUTH_FAIL);
        break;
    }
    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        ESP_LOGI("passport","Pairing action: %d",event->passkey.params.action);
        if(event->passkey.params.action==BLE_SM_IOACT_DISP) {
            struct ble_sm_io io={.action=BLE_SM_IOACT_DISP,.passkey=esp_random()%1000000};
            atomic_store(&passkey,(int)io.passkey);
            ble_sm_inject_io(event->passkey.conn_handle,&io);
        }
        break;
    }
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* Never silently delete a stored bond from a remote request. */
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    default: break;
    }
    return 0;
}
static void advertise(void)
{
    struct ble_hs_adv_fields f={0};
    f.flags=BLE_HS_ADV_F_DISC_GEN|BLE_HS_ADV_F_BREDR_UNSUP;
    f.uuids128=(ble_uuid128_t *)&service; f.num_uuids128=1; f.uuids128_is_complete=1;
    int rc=ble_gap_adv_set_fields(&f);
    if(rc) { ESP_LOGE("passport","adv fields: %d",rc); return; }
    struct ble_hs_adv_fields scan={0};
    scan.name=(uint8_t *)name; scan.name_len=strlen(name); scan.name_is_complete=1;
    ble_gap_adv_rsp_set_fields(&scan);
    struct ble_gap_adv_params p={.conn_mode=BLE_GAP_CONN_MODE_UND,.disc_mode=BLE_GAP_DISC_MODE_GEN,
                                .itvl_min=800,.itvl_max=1200};
    rc=ble_gap_adv_start(own_address,NULL,BLE_HS_FOREVER,&p,gap_event,NULL);
    ESP_LOGI("passport","BLE advertising %s: %d",name,rc);
}
static void sync_host(void)
{
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0,&own_address);
    advertise();
}
static void host_task(void *arg)
{
    (void)arg; nimble_port_run(); nimble_port_freertos_deinit();
}
void passport_ble_start(passport_rx_fn callback)
{
    receive=callback;
    if(nvs_flash_init()!=ESP_OK) { ESP_LOGE("passport","NVS unavailable; BLE disabled"); return; }
    if(nimble_port_init()!=ESP_OK) return;
    uint8_t mac[6]={0}; esp_read_mac(mac,ESP_MAC_BT);
    snprintf(name,sizeof(name),"Passport-%02X%02X%02X",mac[3],mac[4],mac[5]);
    ble_svc_gap_init(); ble_svc_gatt_init();
    ble_svc_gap_device_name_set(name);
    ble_hs_cfg.sync_cb=sync_host;
    ble_hs_cfg.sm_io_cap=BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_bonding=1; ble_hs_cfg.sm_mitm=1; ble_hs_cfg.sm_sc=1;
    ble_hs_cfg.sm_our_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_store_config_init();
    int rc=ble_gatts_count_cfg(services);
    if(!rc) rc=ble_gatts_add_svcs(services);
    if(rc) { ESP_LOGE("passport","GATT registration failed: %d",rc); return; }
    nimble_port_freertos_init(host_task);
}
void passport_ble_send(const char *data)
{
    if(!atomic_load(&secured) || connection==BLE_HS_CONN_HANDLE_NONE) return;
    size_t len=strlen(data);
    uint16_t mtu=ble_att_mtu(connection);
    size_t chunk=mtu>3 ? mtu-3 : 20;
    for(size_t off=0;off<len;off+=chunk) {
        size_t n=len-off<chunk ? len-off : chunk;
        struct os_mbuf *m=ble_hs_mbuf_from_flat(data+off,n);
        if(!m || ble_gatts_notify_custom(connection,tx_handle,m)) break;
    }
}
bool passport_ble_connected(void) { return atomic_load(&secured); }
int passport_ble_passkey(void) { return atomic_load(&passkey); }

void passport_ble_tick(void)
{
    if(connection!=BLE_HS_CONN_HANDLE_NONE && !atomic_load(&secured) &&
       esp_timer_get_time()-connected_at>120000000) {
        ble_gap_terminate(connection,BLE_ERR_AUTH_FAIL);
    }
}
void passport_ble_reconnect(void)
{
    if(connection!=BLE_HS_CONN_HANDLE_NONE)ble_gap_terminate(connection,BLE_ERR_REM_USER_CONN_TERM);
}
