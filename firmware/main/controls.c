#include "controls.h"
control_action_t control_event(control_state_t *state, int key, int event, bool asleep)
{
    if(key<0 || key>2 || event<0 || event>3)return CONTROL_NONE;
    if(event==0) {
        state->suppress=state->suppress || asleep;
        return asleep?CONTROL_WAKE:CONTROL_NONE;
    }
    if(state->suppress){state->suppress=false;return CONTROL_NONE;}
    if(asleep)return CONTROL_WAKE;
    if(event==3)return key==2?CONTROL_RECONNECT:CONTROL_WINDOW;
    if(event==2)return key==2?CONTROL_MUTE:CONTROL_NONE; /* Never clears unread. */
    return key==2?CONTROL_READ:CONTROL_PAGE;
}
