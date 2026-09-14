#include "screen_policy.h"

void screen_policy_touch(screen_policy_t *p, uint64_t now_ms)
{
    p->interaction_ms=now_ms;
    p->notice_active=false;
}

bool screen_policy_awake(const screen_policy_t *p, uint64_t now_ms, unsigned idle_seconds)
{
    return now_ms-p->interaction_ms<idle_seconds*1000ULL ||
           (p->notice_active && now_ms-p->notice_ms<PASSPORT_NOTICE_SCREEN_MS);
}

bool screen_policy_update(screen_policy_t *p, const passport_snapshot_t *s,
                          uint32_t read_through, uint64_t now_ms, unsigned idle_seconds)
{
    if(!alert_policy_consume(&p->events,s,read_through))return false;
    /* Never prolong viewing or queue a suppressed notification for later wakeup. */
    if(screen_policy_awake(p,now_ms,idle_seconds) ||
       (p->notified && now_ms-p->notice_ms<PASSPORT_NOTICE_COOLDOWN_MS))return false;
    p->notified=p->notice_active=true;
    p->notice_ms=now_ms;
    return true;
}
