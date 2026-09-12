#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define PASSPORT_LINE_MAX 2048
#define PASSPORT_RECENT 4

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
} passport_snapshot_t;
bool passport_parse(const char *json, size_t length, passport_snapshot_t *out);

typedef struct { int used, elapsed; int speed; } passport_pace_t;
/* speed: -1 slower than the even budget, 0 balanced/early, +1 faster. */
bool passport_pace(const passport_window_t *window, uint64_t now, passport_pace_t *out);
