#include "gui_config.h"

static bool s_animations_enabled = true;

void gui_config_set_animations_enabled(bool enabled)
{
    s_animations_enabled = enabled;
}

bool gui_config_are_animations_enabled(void)
{
    return s_animations_enabled;
}
