#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define PASSPORT_LINE_MAX 2048
#define PASSPORT_RECENT 4

typedef enum { COMMAND_NONE, COMMAND_SETTINGS, COMMAND_IDENTIFY, COMMAND_WIFI, COMMAND_FORGET_WIFI } passport_command_kind_t;
typedef struct {
    uint32_t id;
    passport_command_kind_t kind;
    uint8_t brightness, idle, volume;
    bool muted, enabled, saved;
    char ssid[33], password[64], endpoint[97], token[129];
} passport_command_t;

typedef struct {
    uint32_t id;
    char kind[20];
    char project[33];
    char title[61];
    char body[121];
    uint64_t time;
} passport_notice_t;
typedef struct {
    int remaining;
    uint32_t minutes;
    uint64_t reset;
} passport_window_t;
typedef struct {
    uint32_t seq, running, waiting, unread, latest;
    uint64_t now;
    int64_t tokens, today;
    passport_window_t windows[2];
    passport_notice_t event, recent[PASSPORT_RECENT];
    unsigned recent_count;
    uint32_t generation;
    passport_command_t command;
    bool via_wifi;
} passport_snapshot_t;
bool passport_parse(const char *json, size_t length, passport_snapshot_t *out);
bool passport_private_endpoint(const char *url);

typedef struct { int used, elapsed; int speed; } passport_pace_t;
/* speed: -1 slower than the even budget, 0 balanced/early, +1 faster. */
bool passport_pace(const passport_window_t *window, uint64_t now, passport_pace_t *out);
