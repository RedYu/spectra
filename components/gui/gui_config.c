#include "gui_config.h"

#include <stdatomic.h>

static atomic_bool s_animations_enabled =
    ATOMIC_VAR_INIT(false);

void gui_config_set_animations_enabled(
    bool enabled
)
{
    atomic_store(
        &s_animations_enabled,
        enabled
    );
}

bool gui_config_get_animations_enabled(void)
{
    return atomic_load(
        &s_animations_enabled
    );
}
