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


/* Only this task owns application state; all LVGL access uses the BSP mutex. */
static QueueHandle_t snapshots, buttons, captures;
static passport_snapshot_t state;
static uint64_t received_ms, interaction_ms;
static bool dimmed;
static control_state_t controls;
typedef struct { int key, event; } key_event_t;

static uint32_t seen_event, read_through;
static int page, battery=-1;
static lv_obj_t *screen, *connection, *battery_label, *amount, *window_label, *reset_label;
static lv_obj_t *quota_ring, *time_ring, *cards[3], *counts[3], *dots[2];
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
    lv_obj_t *o=lv_arc_create(screen);
    lv_obj_set_pos(o,x,y); lv_obj_set_size(o,diameter,diameter);
    lv_arc_set_bg_angles(o,135,45); lv_arc_set_range(o,0,100);
    lv_obj_remove_style(o,NULL,LV_PART_KNOB); lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(o,width,LV_PART_MAIN); lv_obj_set_style_arc_width(o,width,LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(o,lv_color_hex(PANEL),LV_PART_MAIN);
    lv_obj_set_style_arc_color(o,lv_color_hex(color),LV_PART_INDICATOR);
    lv_arc_set_value(o,0); return o;
}
static void create_ui(void)
{
    screen=lv_obj_create(NULL); lv_obj_set_style_bg_color(screen,lv_color_hex(BG),0);
    lv_obj_remove_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
    connection=label(screen,16,13,75,&lv_font_montserrat_14,GREEN);
    battery_label=label(screen,151,13,73,&lv_font_montserrat_14,MUTED);
    lv_obj_set_style_text_align(battery_label,LV_TEXT_ALIGN_RIGHT,0);
    quota_ring=ring(28,40,184,12,GREEN); time_ring=ring(47,59,146,4,MUTED);
    amount=label(screen,28,100,184,&lv_font_montserrat_48,INK);
    lv_obj_set_style_text_align(amount,LV_TEXT_ALIGN_CENTER,0);
    window_label=label(screen,44,156,152,&lv_font_montserrat_14,MUTED);
    lv_obj_set_style_text_align(window_label,LV_TEXT_ALIGN_CENTER,0);
    reset_label=label(screen,40,205,160,&lv_font_montserrat_14,MUTED);
    lv_obj_set_style_text_align(reset_label,LV_TEXT_ALIGN_CENTER,0);
    const char *symbols[3]={LV_SYMBOL_PLAY,LV_SYMBOL_WARNING,LV_SYMBOL_BELL};
    uint32_t colors[3]={BLUE,AMBER,GREEN};
    for(unsigned i=0;i<3;++i) {
        cards[i]=lv_obj_create(screen);lv_obj_set_pos(cards[i],16+i*72,239);lv_obj_set_size(cards[i],64,58);
        lv_obj_set_style_radius(cards[i],20,0);lv_obj_set_style_bg_color(cards[i],lv_color_hex(PANEL),0);
        lv_obj_set_style_border_width(cards[i],0,0);lv_obj_set_style_pad_all(cards[i],0,0);lv_obj_remove_flag(cards[i],LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t *icon=label(cards[i],0,7,64,&lv_font_montserrat_14,colors[i]);lv_label_set_text(icon,symbols[i]);lv_obj_set_style_text_align(icon,LV_TEXT_ALIGN_CENTER,0);
        counts[i]=label(cards[i],0,29,64,&lv_font_montserrat_20,INK);lv_obj_set_style_text_align(counts[i],LV_TEXT_ALIGN_CENTER,0);
    }
    for(unsigned i=0;i<2;++i) {
        dots[i]=lv_obj_create(screen);lv_obj_set_pos(dots[i],110+i*14,309);lv_obj_set_size(dots[i],6,6);
        lv_obj_set_style_radius(dots[i],LV_RADIUS_CIRCLE,0);lv_obj_set_style_border_width(dots[i],0,0);
    }
    lv_screen_load(screen);
}
static void render(uint64_t now)
{
    bool live=received_ms && now-received_ms<30000;
    lv_label_set_text(connection,live?LV_SYMBOL_BLUETOOTH:LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(connection,lv_color_hex(live?GREEN:MUTED),0);
    if(battery<0)lv_label_set_text(battery_label,LV_SYMBOL_BATTERY_EMPTY);
    else lv_label_set_text_fmt(battery_label,"%s %d%%",battery>80?LV_SYMBOL_BATTERY_FULL:battery>60?LV_SYMBOL_BATTERY_3:battery>30?LV_SYMBOL_BATTERY_2:battery>10?LV_SYMBOL_BATTERY_1:LV_SYMBOL_BATTERY_EMPTY,battery);
    unsigned index=(unsigned)page;
    if(!state.windows[index].minutes && state.windows[!index].minutes)index=!index;
    passport_window_t *window=&state.windows[index];
    uint64_t epoch=state.now+(received_ms?(now-received_ms)/1000:0);
    passport_pace_t pace;
    bool available=live && passport_pace(window,epoch,&pace);
    uint32_t hue=available?(pace.speed>0?AMBER:pace.speed<0?BLUE:GREEN):MUTED;
    lv_obj_set_style_arc_color(quota_ring,lv_color_hex(hue),LV_PART_INDICATOR);
    lv_arc_set_value(quota_ring,available?window->remaining:0);lv_arc_set_value(time_ring,available?100-pace.elapsed:0);
    if(available)lv_label_set_text_fmt(amount,"%d%%",window->remaining);else lv_label_set_text(amount,"--");
    if(window->minutes && window->minutes%1440==0)lv_label_set_text_fmt(window_label,"%" PRIu32 "d",window->minutes/1440);
    else if(window->minutes && window->minutes%60==0)lv_label_set_text_fmt(window_label,"%" PRIu32 "h",window->minutes/60);
    else if(window->minutes)lv_label_set_text_fmt(window_label,"%" PRIu32 "m",window->minutes);
    else lv_label_set_text(window_label,"");
    if(available) {
        uint64_t mins=(window->reset-epoch+59)/60;
        if(mins>=1440)lv_label_set_text_fmt(reset_label,LV_SYMBOL_REFRESH "  %" PRIu64 "d %" PRIu64 "h",mins/1440,mins%1440/60);
        else lv_label_set_text_fmt(reset_label,LV_SYMBOL_REFRESH "  %02" PRIu64 ":%02" PRIu64,mins/60,mins%60);
    } else lv_label_set_text(reset_label,LV_SYMBOL_REFRESH "  --:--");
    uint32_t unread=state.latest<=read_through?0:state.unread;
    uint32_t values[3]={live?state.running:0,live?state.waiting:0,unread};
    for(unsigned i=0;i<3;++i) {
        lv_label_set_text_fmt(counts[i],"%" PRIu32,values[i]);
        lv_obj_set_style_border_width(cards[i],i>0&&values[i]>0?1:0,0);
        lv_obj_set_style_border_color(cards[i],lv_color_hex(i==1?AMBER:GREEN),0);
    }
    for(unsigned i=0;i<2;++i) {
        lv_obj_set_style_bg_color(dots[i],lv_color_hex(index==i?GREEN:PANEL),0);
        if(state.windows[i].minutes)lv_obj_remove_flag(dots[i],LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(dots[i],LV_OBJ_FLAG_HIDDEN);
    }
    int pin=passport_ble_passkey();
    lv_obj_set_style_text_font(amount,pin>=0?&lv_font_montserrat_28:&lv_font_montserrat_48,0);
    if(pin>=0){lv_label_set_text_fmt(amount,"%06d",pin);lv_label_set_text(window_label,LV_SYMBOL_BLUETOOTH);interaction_ms=now;}
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
    fputs(data,stdout);
}
static void capture_tile(lv_obj_t *obj)
{
    if(lv_obj_has_flag(obj,LV_OBJ_FLAG_HIDDEN))return;
    lv_obj_update_layout(obj);
    unsigned padding=16;
    if(lv_obj_check_type(obj,&lv_label_class))padding+=lv_font_get_line_height(lv_obj_get_style_text_font(obj,0));
    unsigned w=lv_obj_get_width(obj)+padding,h=lv_obj_get_height(obj)+padding;
    uint32_t stride=lv_draw_buf_width_to_stride(w,LV_COLOR_FORMAT_RGB565);
    size_t size=stride*h+64;
    void *memory=malloc(size);if(!memory)return;
    lv_draw_buf_t buf;
    if(lv_draw_buf_init(&buf,w,h,LV_COLOR_FORMAT_RGB565,stride,memory,size)==LV_RESULT_OK &&
       lv_snapshot_take_to_draw_buf(obj,LV_COLOR_FORMAT_RGB565,&buf)==LV_RESULT_OK) {
        lv_area_t a;lv_obj_get_coords(obj,&a);
        int x=a.x1-((int)buf.header.w-lv_obj_get_width(obj))/2;
        int y=a.y1-((int)buf.header.h-lv_obj_get_height(obj))/2;
        printf("TILE %d %d %u %u %u\n",x,y,(unsigned)buf.header.w,(unsigned)buf.header.h,(unsigned)buf.header.stride);
        fwrite(buf.data,1,buf.header.stride*buf.header.h,stdout);printf("\n");
    }
    free(memory);
}
static void capture(void)
{
    printf("PASSPORT_TILES 240 320\n");
    capture_tile(quota_ring);capture_tile(time_ring);
    capture_tile(connection);capture_tile(battery_label);capture_tile(amount);capture_tile(window_label);capture_tile(reset_label);
    for(unsigned i=0;i<3;++i)capture_tile(cards[i]);
    for(unsigned i=0;i<2;++i)capture_tile(dots[i]);
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
    ESP_ERROR_CHECK(bsp_button_init(on_button,NULL));
    if(xTaskCreate(usb_task,"passport_usb",8192,NULL,4,NULL)!=pdPASS) return;
    uint64_t last_battery=0, last_ui=0;
    int last_pin=-2;
    for(;;) {
        uint64_t now=millis();
        passport_ble_tick();
        bool updated=xQueueReceive(snapshots,&state,0)==pdTRUE;
        if(updated) {
            received_ms=now;
            if(state.event.id>seen_event) { seen_event=state.event.id; interaction_ms=now; page=0; }
        }
        key_event_t key;
        while(xQueueReceive(buttons,&key,0)==pdTRUE) {
            control_action_t action=control_event(&controls,key.key,key.event,dimmed);
            if(action!=CONTROL_NONE)interaction_ms=now;
            if(action==CONTROL_READ) { read_through=state.latest; char reply[96]; snprintf(reply,sizeof(reply),"{\"v\":1,\"read\":%" PRIu32 "}\n",read_through); respond(reply); }
            else if(action==CONTROL_PAGE)page=!page;
            else if(action==CONTROL_RECONNECT)passport_ble_reconnect();
        }
        if(now-last_battery>10000 || battery<0) { battery=bsp_battery_soc(); last_battery=now; }
        int pin=passport_ble_passkey();
        if((updated || now-last_ui>=5000 || now-interaction_ms<200 || pin!=last_pin || uxQueueMessagesWaiting(captures)) && bsp_lvgl_lock(1000)) {
            last_ui=now; last_pin=pin;
            render(now);
            int requested;
            if(xQueueReceive(captures,&requested,0)==pdTRUE) capture();
            bsp_lvgl_unlock();
        }
        if(updated) { char reply[160]; snprintf(reply,sizeof(reply),"{\"v\":1,\"ack\":%" PRIu32 ",\"event\":%" PRIu32 ",\"read\":%" PRIu32 ",\"heap\":%" PRIu32 "}\n",state.seq,state.event.id,read_through,esp_get_free_heap_size()); respond(reply); }
        bool idle=now-interaction_ms>60000;
        if(idle!=dimmed) { dimmed=idle; bsp_display_backlight(idle?0:55); }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
