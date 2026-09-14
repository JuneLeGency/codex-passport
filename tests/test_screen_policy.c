#include <assert.h>
#include <string.h>
#include "../firmware/main/screen_policy.h"
#include "../firmware/main/controls.h"

static void next(passport_snapshot_t *s, unsigned id, const char *kind)
{
    s->latest=s->event.id=id;
    strcpy(s->event.kind,kind);
}

int main(void)
{
    screen_policy_t p={0};
    passport_snapshot_t s={.latest=5,.unread=3,.event={.id=3,.kind="input"}};
    screen_policy_touch(&p,100);
    assert(screen_policy_awake(&p,30099,30));
    assert(!screen_policy_awake(&p,30100,30)); /* exact timeout */
    assert(!screen_policy_update(&p,&s,0,40000,30)); /* first sync after sleep */
    s.event.id=5;assert(!screen_policy_update(&p,&s,0,50000,30));
    next(&s,6,"input");
    assert(screen_policy_update(&p,&s,0,60000,30));
    assert(screen_policy_awake(&p,74999,30));
    next(&s,7,"approval");
    assert(!screen_policy_update(&p,&s,0,74000,30));
    assert(!screen_policy_awake(&p,75000,30)); /* burst did not extend 15 s */
    next(&s,8,"input");
    assert(!screen_policy_update(&p,&s,0,359999,30));
    assert(!screen_policy_update(&p,&s,0,360000,30)); /* cooldown events never deferred */
    next(&s,9,"approval");
    assert(screen_policy_update(&p,&s,0,360000,30)); /* exactly 5 minutes */
    assert(!screen_policy_update(&p,&s,0,700000,30)); /* replay */

    screen_policy_touch(&p,710000);
    next(&s,10,"input");
    assert(!screen_policy_update(&p,&s,0,739999,30)); /* do not prolong manual viewing */
    assert(!screen_policy_awake(&p,740000,30));
    assert(!screen_policy_update(&p,&s,0,740001,30));
    next(&s,11,"input");assert(!screen_policy_update(&p,&s,11,750000,30));
    next(&s,12,"approval");s.unread=0;assert(!screen_policy_update(&p,&s,0,760000,30));
    s.unread=1;next(&s,13,"completed");
    assert(!screen_policy_update(&p,&s,0,770000,30));
    next(&s,14,"started");assert(!screen_policy_update(&p,&s,0,780000,30));

    next(&s,15,"completed");s.latest=18;
    assert(!screen_policy_update(&p,&s,0,790000,30));
    s.event.id=16;strcpy(s.event.kind,"input");
    assert(screen_policy_update(&p,&s,0,800000,30)); /* quiet event does not hide later request */
    s.event.id=17;assert(!screen_policy_update(&p,&s,0,1200000,30));
    s.event.id=18;assert(!screen_policy_update(&p,&s,0,1300000,30)); /* same batch */
    next(&s,1,"input");assert(!screen_policy_update(&p,&s,0,1400000,30)); /* new collector baseline */
    next(&s,2,"input");assert(screen_policy_update(&p,&s,0,1500000,30));

    /* First OK wakes only; neither short/double/long completion clears unread. */
    for(int finish=1;finish<=3;++finish) {
        control_state_t buttons={0};
        assert(!screen_policy_awake(&p,1600000+finish*200000,30));
        assert(control_event(&buttons,2,0,true)==CONTROL_WAKE);
        screen_policy_touch(&p,1600000+finish*200000);
        assert(control_event(&buttons,2,finish,false)==CONTROL_NONE);
        assert(s.unread==1);
    }
    /* Button/identify/pairing viewing may use a longer saved preference. */
    screen_policy_touch(&p,2400000);
    assert(screen_policy_awake(&p,2490000,120));
    assert(!screen_policy_awake(&p,2490000,30)); /* low battery cap is effective */
    screen_policy_touch(&p,2490000); /* repeated valid pairing PIN */
    assert(screen_policy_awake(&p,2519999,30));
    assert(!screen_policy_awake(&p,2520000,30));

    /* Audio mute consumes its own cursor; it must not consume the visual reminder. */
    alert_policy_t sound={0};screen_policy_t visual={0};
    next(&s,3,"input");
    assert(!alert_policy_update(&sound,&s,0,true,1));
    assert(!screen_policy_update(&visual,&s,0,1,30));
    next(&s,4,"input");
    assert(!alert_policy_update(&sound,&s,0,true,31000));
    assert(screen_policy_update(&visual,&s,0,31000,30));
    return 0;
}
