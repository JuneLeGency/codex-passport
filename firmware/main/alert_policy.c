#include "alert_policy.h"
#include <string.h>

bool alert_needs_attention(const char *kind)
{
    return !strcmp(kind,"approval") || !strcmp(kind,"input");
}

bool alert_policy_consume(alert_policy_t *p, const passport_snapshot_t *s,
                          uint32_t read_through)
{
    if(!p->initialized || s->latest < p->latest) {
        /* Also recover silently if a different/new collector has lower event IDs. */
        p->initialized=true;
        p->cursor=s->latest;
        p->latest=s->latest;
        return false;
    }
    p->latest=s->latest;
    if(!s->event.id || s->event.id<=p->cursor)return false;
    bool attention=alert_needs_attention(s->event.kind);
    /* Quiet completion must not consume a later request for input in the batch. */
    p->cursor=attention && s->latest>s->event.id?s->latest:s->event.id;
    return attention && s->unread && s->event.id>read_through;
}

bool alert_policy_update(alert_policy_t *p, const passport_snapshot_t *s,
                         uint32_t read_through, bool muted, uint64_t now_ms)
{
    if(!alert_policy_consume(p,s,read_through) || muted)return false;
    if(p->sounded && now_ms-p->last_sound_ms<120000)return false;
    p->sounded=true;
    p->last_sound_ms=now_ms;
    return true;
}
