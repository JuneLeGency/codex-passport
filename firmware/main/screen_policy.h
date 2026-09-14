#pragma once
#include "alert_policy.h"

#define PASSPORT_NOTICE_SCREEN_MS 15000ULL
#define PASSPORT_NOTICE_COOLDOWN_MS 300000ULL

typedef struct {
    alert_policy_t events;
    uint64_t interaction_ms, notice_ms;
    bool notified, notice_active;
} screen_policy_t;

/* Boot, buttons, pairing and explicit identify use the configured viewing time. */
void screen_policy_touch(screen_policy_t *policy, uint64_t now_ms);
bool screen_policy_awake(const screen_policy_t *policy, uint64_t now_ms, unsigned idle_seconds);
/* Call on every accepted snapshot, including while already awake. Mute is audio-only. */
bool screen_policy_update(screen_policy_t *policy, const passport_snapshot_t *snapshot,
                          uint32_t read_through, uint64_t now_ms, unsigned idle_seconds);
