#include "gui_feedback.h"

#include <stddef.h>

#include "buzzer_service.h"

static void gui_feedback_pressed_event(
    lv_event_t *event
)
{
    if (event == NULL) {
        return;
    }

    if (buzzer_service_is_running()) {
        (void)buzzer_service_play(
            BUZZER_SIGNAL_CLICK
        );
    }
}

void gui_feedback_attach(
    lv_obj_t *object
)
{
    if (object == NULL) {
        return;
    }

    lv_obj_add_event_cb(
        object,
        gui_feedback_pressed_event,
        LV_EVENT_PRESSED,
        NULL
    );
}
