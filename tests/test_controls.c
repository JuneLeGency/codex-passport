#include <assert.h>
#include "../firmware/main/controls.h"
int main(void) {
    control_state_t s={0};
    assert(control_event(&s,2,0,true)==CONTROL_WAKE);
    assert(control_event(&s,2,1,false)==CONTROL_NONE);
    assert(control_event(&s,2,0,false)==CONTROL_NONE);
    assert(control_event(&s,2,1,false)==CONTROL_READ);
    assert(control_event(&s,0,1,false)==CONTROL_PAGE);
    assert(control_event(&s,1,1,false)==CONTROL_PAGE);
    assert(control_event(&s,2,2,false)==CONTROL_NONE);
    assert(control_event(&s,2,3,false)==CONTROL_RECONNECT);
    assert(control_event(&s,2,0,true)==CONTROL_WAKE);
    assert(control_event(&s,2,3,false)==CONTROL_NONE);
    assert(control_event(&s,9,1,false)==CONTROL_NONE);
    return 0;
}
