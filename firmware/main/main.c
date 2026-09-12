#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "lvgl.h"
#include "bsp_display.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "protocol.h"
#include "transport.h"
#include "controls.h"
#include "alert_policy.h"
#include "sound.h"
#include "preferences.h"
#include "wifi_link.h"
#include "esp_app_desc.h"


/* Only this task owns application state; all LVGL access uses the BSP mutex. */
static QueueHandle_t snapshots, buttons, captures;
static passport_snapshot_t state;
static uint64_t received_ms, interaction_ms;
static bool dimmed;
static control_state_t controls;
static alert_policy_t alerts;
typedef struct { int key, event; } key_event_t;

static uint32_t seen_event, read_through;
static uint32_t last_command;
static bool command_ok;
static int page, window_index, battery=-1;
typedef struct {
    lv_obj_t *screen, *connection, *battery_label, *amount, *window_label, *reset_label;
    lv_obj_t *quota_ring, *time_ring, *cards[3], *counts[3], *dots[2];
    lv_obj_t *notice_titles[3], *notice_bodies[3];
} view_t;
static view_t views[2], *ui;
LV_FONT_DECLARE(passport_cjk16);
#define INK 0xEDF4E9
#define MUTED 0x95A491
#define GREEN 0xBCE6A4
#define BLUE 0xACC9F8
#define AMBER 0xFFB783
#define BG 0x111A13
#define PANEL 0x253124

static uint64_t millis(void) { return esp_timer_get_time()/1000ULL; }
static lv_obj_t *label(lv_obj_t *parent,int x,int y,int width,const lv_font_t *font,uint32_t color)
{
    lv_obj_t *o=lv_label_create(parent);
    lv_obj_set_pos(o,x,y); lv_obj_set_width(o,width);
    lv_obj_set_style_text_font(o,font,0); lv_obj_set_style_text_color(o,lv_color_hex(color),0);
    lv_label_set_long_mode(o,LV_LABEL_LONG_DOT);
    return o;
}
static lv_obj_t *ring(int x,int y,int diameter,int width,uint32_t color)
{
    lv_obj_t *o=lv_arc_create(ui->screen);
    lv_obj_set_pos(o,x,y); lv_obj_set_size(o,diameter,diameter);
    lv_arc_set_bg_angles(o,135,45); lv_arc_set_range(o,0,100);
    lv_obj_remove_style(o,NULL,LV_PART_KNOB); lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(o,width,LV_PART_MAIN); lv_obj_set_style_arc_width(o,width,LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(o,lv_color_hex(PANEL),LV_PART_MAIN);
    lv_obj_set_style_arc_color(o,lv_color_hex(color),LV_PART_INDICATOR);
    lv_arc_set_value(o,0); return o;
}
static void create_dashboard(void)
{
    ui->screen=lv_obj_create(NULL); lv_obj_set_style_bg_color(ui->screen,lv_color_hex(BG),0);
    lv_obj_remove_flag(ui->screen,LV_OBJ_FLAG_SCROLLABLE);
    ui->connection=label(ui->screen,16,13,75,&lv_font_montserrat_14,GREEN);
    ui->battery_label=label(ui->screen,151,13,73,&lv_font_montserrat_14,MUTED);
    lv_obj_set_style_text_align(ui->battery_label,LV_TEXT_ALIGN_RIGHT,0);
    ui->quota_ring=ring(28,40,184,12,GREEN); ui->time_ring=ring(47,59,146,4,MUTED);
    ui->amount=label(ui->screen,28,100,184,&lv_font_montserrat_48,INK);
    lv_obj_set_style_text_align(ui->amount,LV_TEXT_ALIGN_CENTER,0);
    ui->window_label=label(ui->screen,44,156,152,&lv_font_montserrat_14,MUTED);
    lv_obj_set_style_text_align(ui->window_label,LV_TEXT_ALIGN_CENTER,0);
    ui->reset_label=label(ui->screen,40,205,160,&lv_font_montserrat_14,MUTED);
    lv_obj_set_style_text_align(ui->reset_label,LV_TEXT_ALIGN_CENTER,0);
    const char *symbols[3]={LV_SYMBOL_PLAY,LV_SYMBOL_WARNING,LV_SYMBOL_BELL};
    uint32_t colors[3]={BLUE,AMBER,GREEN};
    for(unsigned i=0;i<3;++i) {
        ui->cards[i]=lv_obj_create(ui->screen);lv_obj_set_pos(ui->cards[i],16+i*72,239);lv_obj_set_size(ui->cards[i],64,58);
        lv_obj_set_style_radius(ui->cards[i],20,0);lv_obj_set_style_bg_color(ui->cards[i],lv_color_hex(PANEL),0);
        lv_obj_set_style_border_width(ui->cards[i],0,0);lv_obj_set_style_pad_all(ui->cards[i],0,0);lv_obj_remove_flag(ui->cards[i],LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *icon=label(ui->cards[i],0,7,64,&lv_font_montserrat_14,colors[i]);lv_label_set_text(icon,symbols[i]);lv_obj_set_style_text_align(icon,LV_TEXT_ALIGN_CENTER,0);
        ui->counts[i]=label(ui->cards[i],0,29,64,&lv_font_montserrat_20,INK);lv_obj_set_style_text_align(ui->counts[i],LV_TEXT_ALIGN_CENTER,0);
    }
    for(unsigned i=0;i<2;++i) {
        ui->dots[i]=lv_obj_create(ui->screen);lv_obj_set_pos(ui->dots[i],110+i*14,309);lv_obj_set_size(ui->dots[i],6,6);
        lv_obj_set_style_radius(ui->dots[i],LV_RADIUS_CIRCLE,0);lv_obj_set_style_border_width(ui->dots[i],0,0);
    }
    lv_screen_load(ui->screen);
}
static void create_progress(void)
{
    ui->screen=lv_obj_create(NULL); lv_obj_set_style_bg_color(ui->screen,lv_color_hex(BG),0);
    lv_obj_remove_flag(ui->screen,LV_OBJ_FLAG_SCROLLABLE);
    ui->connection=label(ui->screen,14,9,75,&lv_font_montserrat_14,GREEN);
    ui->battery_label=label(ui->screen,151,9,75,&lv_font_montserrat_14,MUTED);
    lv_obj_set_style_text_align(ui->battery_label,LV_TEXT_ALIGN_RIGHT,0);
    ui->quota_ring=ring(12,32,86,7,GREEN); ui->time_ring=ring(23,43,64,3,MUTED);
    ui->amount=label(ui->screen,12,57,86,&lv_font_montserrat_20,INK);
    lv_obj_set_style_text_align(ui->amount,LV_TEXT_ALIGN_CENTER,0);
    ui->window_label=label(ui->screen,115,37,110,&lv_font_montserrat_20,INK);
    ui->reset_label=label(ui->screen,115,66,110,&lv_font_montserrat_14,MUTED);
    for(unsigned i=0;i<3;++i) {
        ui->counts[i]=label(ui->screen,115+i*37,91,38,&lv_font_montserrat_14,i==1?AMBER:MUTED);
        ui->cards[i]=lv_obj_create(ui->screen);lv_obj_set_pos(ui->cards[i],12,124+i*64);lv_obj_set_size(ui->cards[i],216,58);
        lv_obj_set_style_radius(ui->cards[i],13,0);lv_obj_set_style_bg_color(ui->cards[i],lv_color_hex(PANEL),0);
        lv_obj_set_style_border_width(ui->cards[i],0,0);lv_obj_set_style_pad_all(ui->cards[i],0,0);
        lv_obj_remove_flag(ui->cards[i],LV_OBJ_FLAG_SCROLLABLE);lv_obj_remove_flag(ui->cards[i],LV_OBJ_FLAG_CLICKABLE);
        ui->notice_titles[i]=label(ui->cards[i],12,3,192,&passport_cjk16,INK);
        ui->notice_bodies[i]=label(ui->cards[i],12,29,192,&passport_cjk16,MUTED);
        lv_obj_set_height(ui->notice_titles[i],24);lv_obj_set_height(ui->notice_bodies[i],24);
    }
    for(unsigned i=0;i<2;++i) {
        ui->dots[i]=lv_obj_create(ui->screen);lv_obj_set_pos(ui->dots[i],48+i*10,112);lv_obj_set_size(ui->dots[i],4,4);
        lv_obj_set_style_radius(ui->dots[i],LV_RADIUS_CIRCLE,0);lv_obj_set_style_border_width(ui->dots[i],0,0);
    }
    lv_screen_load(ui->screen);
}
static void create_ui(void)
{
    ui=&views[0];create_dashboard();
    ui=&views[1];create_progress();
    ui=&views[0];lv_screen_load(ui->screen);
}
/* Never send unsupported emoji or rare glyphs to LVGL's missing-glyph box. */
static void readable(const char *input,char *output,size_t capacity)
{
    size_t used=0;
    const unsigned char *p=(const unsigned char *)input;
    while(*p && used+1<capacity) {
        unsigned n=*p<0x80?1:(*p&0xe0)==0xc0?2:(*p&0xf0)==0xe0?3:4;
        uint32_t cp=*p & (n==1?0x7f:n==2?0x1f:n==3?0x0f:0x07);
        bool valid=true;
        for(unsigned i=1;i<n;++i){if(!p[i] || (p[i]&0xc0)!=0x80){valid=false;break;}cp=(cp<<6)|(p[i]&0x3f);}
        if(!valid)break;
        lv_font_glyph_dsc_t glyph={0};
        if(cp==32 || passport_cjk16.get_glyph_dsc(&passport_cjk16,&glyph,cp,0)) {
            if(used+n>=capacity)break;
            memcpy(output+used,p,n);used+=n;
        } else if(used && output[used-1]!=' ')output[used++]=' ';
        p+=n;
    }
    while(used && output[used-1]==' ')--used;
    output[used]='\0';
}
static void render_notices(bool live)
{
    for(unsigned i=0;i<3;++i) {
        if(i>=state.recent_count) {
            if(i==0){lv_obj_remove_flag(ui->cards[i],LV_OBJ_FLAG_HIDDEN);lv_label_set_text(ui->notice_titles[i],"等待会话进展");lv_label_set_text(ui->notice_bodies[i],"新进展会自动出现在这里");}
            else lv_obj_add_flag(ui->cards[i],LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(ui->cards[i],LV_OBJ_FLAG_HIDDEN);
        passport_notice_t *notice=&state.recent[i];
        const char *status="进行中";uint32_t hue=BLUE;
        if(!strcmp(notice->kind,"completed")){status="已完成";hue=GREEN;}
        else if(!strcmp(notice->kind,"approval")){status="待批准";hue=AMBER;}
        else if(!strcmp(notice->kind,"input")){status="待回复";hue=AMBER;}
        else if(!strcmp(notice->kind,"failed")){status="遇到问题";hue=AMBER;}
        else if(!strcmp(notice->kind,"interrupted")){status="已中断";hue=MUTED;}
        else if(!strcmp(notice->kind,"compacting")){status="整理上下文";}
        char title[96],body[256],clean[160];
        readable(notice->title[0]?notice->title:notice->project,title,sizeof(title));
        readable(notice->body,clean,sizeof(clean));
        snprintf(body,sizeof(body),"%s%s%s",live?status:"离线",clean[0]?" · ":"",clean);
        lv_label_set_text(ui->notice_titles[i],title[0]?title:"会话");
        lv_label_set_text(ui->notice_bodies[i],body);
        lv_obj_set_style_text_color(ui->notice_bodies[i],lv_color_hex(live?hue:MUTED),0);
    }
}
static void render(uint64_t now)
{
    ui=&views[page];
    if(lv_screen_active()!=ui->screen)lv_screen_load(ui->screen);
    bool live=received_ms && now-received_ms<30000;
    lv_label_set_text_fmt(ui->connection,"%s  %s",live?(state.via_wifi?LV_SYMBOL_WIFI:LV_SYMBOL_BLUETOOTH):LV_SYMBOL_CLOSE,
                          passport_sound_muted()?LV_SYMBOL_MUTE:LV_SYMBOL_VOLUME_MID);
    lv_obj_set_style_text_color(ui->connection,lv_color_hex(live?GREEN:MUTED),0);
    if(battery<0)lv_label_set_text(ui->battery_label,LV_SYMBOL_BATTERY_EMPTY);
    else lv_label_set_text_fmt(ui->battery_label,"%s %d%%",battery>80?LV_SYMBOL_BATTERY_FULL:battery>60?LV_SYMBOL_BATTERY_3:battery>30?LV_SYMBOL_BATTERY_2:battery>10?LV_SYMBOL_BATTERY_1:LV_SYMBOL_BATTERY_EMPTY,battery);
    unsigned index=(unsigned)window_index;
    if(!state.windows[index].minutes && state.windows[!index].minutes)index=!index;
    passport_window_t *window=&state.windows[index];
    uint64_t epoch=state.now+(received_ms?(now-received_ms)/1000:0);
    passport_pace_t pace;
    bool available=live && passport_pace(window,epoch,&pace);
    uint32_t hue=available?(pace.speed>0?AMBER:pace.speed<0?BLUE:GREEN):MUTED;
    lv_obj_set_style_arc_color(ui->quota_ring,lv_color_hex(hue),LV_PART_INDICATOR);
    lv_arc_set_value(ui->quota_ring,available?window->remaining:0);lv_arc_set_value(ui->time_ring,available?100-pace.elapsed:0);
    if(available)lv_label_set_text_fmt(ui->amount,"%d%%",window->remaining);else lv_label_set_text(ui->amount,"--");
    if(window->minutes && window->minutes%1440==0)lv_label_set_text_fmt(ui->window_label,"%" PRIu32 "d",window->minutes/1440);
    else if(window->minutes && window->minutes%60==0)lv_label_set_text_fmt(ui->window_label,"%" PRIu32 "h",window->minutes/60);
    else if(window->minutes)lv_label_set_text_fmt(ui->window_label,"%" PRIu32 "m",window->minutes);
    else lv_label_set_text(ui->window_label,"");
    if(available) {
        uint64_t mins=(window->reset-epoch+59)/60;
        if(mins>=1440)lv_label_set_text_fmt(ui->reset_label,LV_SYMBOL_REFRESH "  %" PRIu64 "d %" PRIu64 "h",mins/1440,mins%1440/60);
        else lv_label_set_text_fmt(ui->reset_label,LV_SYMBOL_REFRESH "  %02" PRIu64 ":%02" PRIu64,mins/60,mins%60);
    } else lv_label_set_text(ui->reset_label,LV_SYMBOL_REFRESH "  --:--");
    uint32_t unread=state.latest<=read_through?0:state.unread;
    uint32_t values[3]={live?state.running:0,live?state.waiting:0,unread};
    for(unsigned i=0;i<3;++i) {
        const char *symbols[3]={LV_SYMBOL_PLAY,LV_SYMBOL_WARNING,LV_SYMBOL_BELL};
        if(page==1)lv_label_set_text_fmt(ui->counts[i],"%s%" PRIu32,symbols[i],values[i]);
        else {
            lv_label_set_text_fmt(ui->counts[i],"%" PRIu32,values[i]);
            lv_obj_set_style_border_width(ui->cards[i],i>0&&values[i]>0?1:0,0);
            lv_obj_set_style_border_color(ui->cards[i],lv_color_hex(i==1?AMBER:GREEN),0);
        }
    }
    for(unsigned i=0;i<2;++i) {
        lv_obj_set_style_bg_color(ui->dots[i],lv_color_hex((unsigned)page==i?GREEN:PANEL),0);

    }
    if(page==1)render_notices(live);
    int pin=passport_ble_passkey();
    lv_obj_set_style_text_font(ui->amount,page==1?&lv_font_montserrat_20:pin>=0?&lv_font_montserrat_28:&lv_font_montserrat_48,0);
    if(pin>=0){
        if(page==0){lv_label_set_text_fmt(ui->amount,"%06d",pin);lv_label_set_text(ui->window_label,LV_SYMBOL_BLUETOOTH);}
        else {lv_label_set_text(ui->amount,LV_SYMBOL_BLUETOOTH);lv_label_set_text_fmt(ui->window_label,"%06d",pin);}
        lv_label_set_text(ui->reset_label,"PAIR");interaction_ms=now;
    }
}
static void on_button(bsp_btn_t button,bsp_btn_ev_t event,void *context)
{
    (void)context;
    key_event_t key={button,event}; (void)xQueueSend(buttons,&key,0);
}
static void ble_receive(const uint8_t *bytes,size_t size)
{
    static char line[PASSPORT_LINE_MAX+1];
    static size_t length;
    static bool overflow;
    static passport_snapshot_t next;
    if(!bytes) { length=0; overflow=false; return; }
    for(size_t i=0;i<size;++i) {
        if(bytes[i]=='\n') {
            line[length]='\0';
            if(!overflow && passport_parse(line,length,&next)) (void)xQueueSend(snapshots,&next,0);
            length=0; overflow=false;
        } else if(length<PASSPORT_LINE_MAX) line[length++]=bytes[i];
        else overflow=true;
    }
}
static void respond(const char *data)
{
    passport_ble_send(data);
    if(state.via_wifi)passport_wifi_ack(data);
    fputs(data,stdout);
}
static void receipt(bool frame)
{
    char reply[640];
    snprintf(reply,sizeof(reply),"{\"v\":1,\"ack\":%" PRIu32 ",\"event\":%" PRIu32 ",\"read\":%" PRIu32
        ",\"heap\":%" PRIu32 ",\"generation\":%" PRIu32 ",\"cmd\":%" PRIu32 ",\"cmd_ok\":%s,"
        "\"device\":{\"fw\":\"%s\",\"battery\":%d,\"brightness\":%u,\"idle\":%u,\"volume\":%u,\"muted\":%s,\"wifi\":%u,\"configured\":%s}}\n",
        frame?state.seq:0,frame?state.event.id:0,read_through,esp_get_free_heap_size(),state.generation,last_command,
        command_ok?"true":"false",esp_app_get_description()->version,battery,passport_brightness_setting(),
        passport_idle_seconds(),passport_sound_volume(),passport_sound_muted()?"true":"false",passport_wifi_state(),
        passport_wifi_configured()?"true":"false");
    respond(reply);
}
static bool wifi_receive(const char *json,size_t length)
{
    static passport_snapshot_t next;
    if(!passport_parse(json,length,&next))return false;
    next.via_wifi=true;
    return xQueueSend(snapshots,&next,pdMS_TO_TICKS(100))==pdTRUE;
}
static void apply_command(uint64_t now)
{
    passport_command_t *cmd=&state.command;
    if(cmd->id && cmd->id!=last_command) {
        last_command=cmd->id;command_ok=false;
        if(cmd->kind==COMMAND_SETTINGS) {
            command_ok=passport_preferences_set(cmd->brightness,cmd->idle) && passport_sound_configure(cmd->muted,cmd->volume);
        } else if(cmd->kind==COMMAND_IDENTIFY) {
            interaction_ms=now;passport_sound_notify();command_ok=true;
        } else if(cmd->kind==COMMAND_WIFI || cmd->kind==COMMAND_FORGET_WIFI)command_ok=passport_wifi_command(cmd);
    }
    /* Never retain provisioning credentials in render state or diagnostic output. */
    memset(cmd,0,sizeof(*cmd));
}
/* Stream the display's existing small draw tiles before the panel driver swaps bytes.
 * Capturing the actual full refresh needs no second framebuffer or radio interruption. */
static void capture_flush(lv_event_t *event)
{
    lv_display_t *display=lv_event_get_user_data(event);
    const lv_area_t *area=lv_event_get_param(event);
    lv_draw_buf_t *buffer=lv_display_get_buf_active(display);
    unsigned width=lv_area_get_width(area),height=lv_area_get_height(area);
    if(!buffer || buffer->header.cf!=LV_COLOR_FORMAT_RGB565 ||
       buffer->header.stride*height>buffer->data_size) {
        printf("PASSPORT_CAPTURE_ERROR buffer\n");return;
    }
    printf("TILE %d %d %u %u %u\n",(int)area->x1,(int)area->y1,width,height,(unsigned)buffer->header.stride);
    fwrite(buffer->data,1,buffer->header.stride*height,stdout);printf("\n");
}
static void capture(void)
{
    lv_display_t *display=lv_display_get_default();
    printf("PASSPORT_TILES 240 320\n");
    if(lv_display_get_render_mode(display)!=LV_DISPLAY_RENDER_MODE_PARTIAL) {
        printf("PASSPORT_CAPTURE_ERROR mode\n");
    } else {
        lv_display_add_event_cb(display,capture_flush,LV_EVENT_FLUSH_START,display);
        lv_obj_invalidate(lv_display_get_screen_active(display));
        lv_refr_now(display);
        lv_display_remove_event_cb_with_user_data(display,capture_flush,display);
    }
    printf("PASSPORT_CAPTURE_END\n");
}
static void usb_task(void *context)
{
    (void)context;
    static char line[PASSPORT_LINE_MAX+1];
    static passport_snapshot_t next;
    size_t length=0; bool overflow=false;
    setvbuf(stdin,NULL,_IONBF,0); setvbuf(stdout,NULL,_IONBF,0);
    usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_LF);
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_LF);
    for(;;) {
        int c=getchar();
        if(c==EOF) { clearerr(stdin); vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        if(c=='\n') {
            line[length]='\0';
            if(!overflow && !strcmp(line,"CAPTURE")) { int yes=1; xQueueOverwrite(captures,&yes); }
            else if(!overflow && (!strcmp(line,"CAPTURE0") || !strcmp(line,"CAPTURE1"))) {
                int target=2+line[7]-'0';xQueueOverwrite(captures,&target);
            }
            else if(!overflow && passport_parse(line,length,&next)) {
                if(xQueueSend(snapshots,&next,pdMS_TO_TICKS(100))!=pdTRUE) printf("{\"v\":1,\"busy\":true}\n");
            } else printf("{\"v\":1,\"invalid\":true}\n");
            length=0; overflow=false;
        } else if(length<PASSPORT_LINE_MAX) line[length++]=(char)c;
        else overflow=true;
    }
}
void app_main(void)
{
    ESP_ERROR_CHECK(bsp_display_init());
    if(!bsp_lvgl_init()) return;
    (void)bsp_battery_init();
    snapshots=xQueueCreate(2,sizeof(passport_snapshot_t)); buttons=xQueueCreate(8,sizeof(key_event_t)); captures=xQueueCreate(1,sizeof(int));
    if(!snapshots || !buttons || !captures) return;
    if(bsp_lvgl_lock(1000)) { create_ui(); bsp_lvgl_unlock(); }
    bsp_display_backlight(55);
    interaction_ms=millis();
    passport_ble_start(ble_receive);
    passport_sound_init();
    passport_preferences_init();
    passport_wifi_init(wifi_receive);
    ESP_ERROR_CHECK(bsp_button_init(on_button,NULL));
    if(xTaskCreate(usb_task,"passport_usb",8192,NULL,4,NULL)!=pdPASS) return;
    uint64_t last_battery=0, last_ui=0;
    int last_pin=-2,last_brightness=-1;
    for(;;) {
        uint64_t now=millis();
        passport_ble_tick();
        bool updated=xQueueReceive(snapshots,&state,0)==pdTRUE;
        if(updated) {
            received_ms=now;
            if(state.event.id>seen_event) {
                seen_event=state.event.id;
                if(alert_needs_attention(state.event.kind))interaction_ms=now;
            }
            apply_command(now);
        }
        key_event_t key;
        while(xQueueReceive(buttons,&key,0)==pdTRUE) {
            control_action_t action=control_event(&controls,key.key,key.event,dimmed);
            if(action!=CONTROL_NONE)interaction_ms=now;
            if(action==CONTROL_READ) { read_through=state.latest;receipt(false); }
            else if(action==CONTROL_PAGE)page=!page;
            else if(action==CONTROL_WINDOW)window_index=!window_index;
            else if(action==CONTROL_RECONNECT) {
                if(passport_wifi_state()>0 && passport_wifi_state()<4)passport_wifi_cancel();
                else passport_ble_reconnect();
            }
            else if(action==CONTROL_MUTE){passport_sound_toggle();receipt(false);}
        }
        if(updated && alert_policy_update(&alerts,&state,read_through,passport_sound_muted(),now))
            passport_sound_notify();
        if(now-last_battery>10000 || battery<0) { battery=bsp_battery_soc(); last_battery=now; }
        int pin=passport_ble_passkey();
        if((updated || now-last_ui>=5000 || now-interaction_ms<200 || pin!=last_pin || uxQueueMessagesWaiting(captures)) && bsp_lvgl_lock(1000)) {
            last_ui=now; last_pin=pin;
            render(now);
            int requested;
            if(xQueueReceive(captures,&requested,0)==pdTRUE) {
                int previous=page;
                if(requested>=2){page=requested-2;render(now);}
                capture();
                if(page!=previous){page=previous;render(now);}
            }
            bsp_lvgl_unlock();
        }
        if(updated)receipt(true);
        unsigned idle_seconds=passport_idle_seconds();
        if(battery>=0 && battery<=10 && idle_seconds>30)idle_seconds=30;
        dimmed=now-interaction_ms>idle_seconds*1000ULL;
        int brightness=dimmed?0:(int)passport_brightness(battery);
        if(brightness!=last_brightness){bsp_display_backlight(brightness);last_brightness=brightness;}
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
