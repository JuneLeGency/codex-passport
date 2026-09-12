#pragma once
#include <stdbool.h>
typedef enum { CONTROL_NONE, CONTROL_WAKE, CONTROL_PAGE, CONTROL_READ, CONTROL_RECONNECT, CONTROL_WINDOW, CONTROL_MUTE } control_action_t;
typedef struct { bool suppress; } control_state_t;
/* Keys 0/1/2 = UP/DOWN/OK; events 0/1/2/3 = press/click/double/long. */
control_action_t control_event(control_state_t *state, int key, int event, bool asleep);
