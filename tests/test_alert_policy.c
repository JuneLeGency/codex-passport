#include <assert.h>
#include <string.h>
#include "../firmware/main/alert_policy.h"
int main(void)
{
    alert_policy_t p={0};
    passport_snapshot_t s={.latest=5,.unread=3,.event={.id=3,.kind="input"}};
    assert(!alert_policy_update(&p,&s,0,false,100)); /* silent boot baseline */
    s.event.id=5;assert(!alert_policy_update(&p,&s,0,false,1000));
    s.latest=s.event.id=6;assert(alert_policy_update(&p,&s,0,false,2000));
    assert(!alert_policy_update(&p,&s,0,false,200000)); /* reconnect / replay */
    s.latest=9;s.event.id=7;
    assert(alert_policy_update(&p,&s,0,false,210000));
    s.event.id=8;assert(!alert_policy_update(&p,&s,0,false,400000));
    s.event.id=9;assert(!alert_policy_update(&p,&s,0,false,500000)); /* same batch */
    s.latest=s.event.id=10;assert(alert_policy_update(&p,&s,0,false,510000));
    s.latest=s.event.id=11;
    assert(!alert_policy_update(&p,&s,0,false,629999)); /* < two minutes */
    assert(!alert_policy_update(&p,&s,0,false,640000)); /* never deferred */
    s.latest=s.event.id=12;assert(!alert_policy_update(&p,&s,12,false,650000));
    s.latest=s.event.id=13;s.unread=0;assert(!alert_policy_update(&p,&s,0,false,660000));
    s.latest=s.event.id=14;s.unread=1;
    assert(!alert_policy_update(&p,&s,0,true,670000));
    assert(!alert_policy_update(&p,&s,0,false,680000)); /* unmute no replay */
    const char *quiet[]={"started","completed","failed","interrupted"};
    for(unsigned i=0;i<4;++i){s.latest=s.event.id=15+i;strcpy(s.event.kind,quiet[i]);assert(!alert_policy_update(&p,&s,0,false,700000+i*130000));}
    s.latest=20;s.event.id=19;strcpy(s.event.kind,"completed");
    assert(!alert_policy_update(&p,&s,0,false,1300000));
    s.event.id=20;strcpy(s.event.kind,"approval");
    assert(alert_policy_update(&p,&s,0,false,1301000)); /* quiet event did not hide input */
    s.latest=s.event.id=21;assert(alert_policy_update(&p,&s,0,false,1421000));
    s.latest=s.event.id=1;assert(!alert_policy_update(&p,&s,0,false,1600000)); /* new collector */
    s.latest=s.event.id=2;assert(alert_policy_update(&p,&s,0,false,1700000));
    return 0;
}
