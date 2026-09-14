#pragma once
#include "protocol.h"

typedef struct {
    bool initialized, sounded;
    uint32_t cursor, latest;
    uint64_t last_sound_ms;
} alert_policy_t;
bool alert_needs_attention(const char *kind);

/* Consume each attention batch once, even if the caller suppresses its output. */
bool alert_policy_consume(alert_policy_t *policy, const passport_snapshot_t *snapshot,
                          uint32_t read_through);

/* One chime per new batch; first synchronization establishes a silent baseline. */
bool alert_policy_update(alert_policy_t *policy, const passport_snapshot_t *snapshot,
                         uint32_t read_through, bool muted, uint64_t now_ms);
