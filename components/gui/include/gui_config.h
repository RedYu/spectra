#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void gui_config_set_animations_enabled(bool enabled);

bool gui_config_are_animations_enabled(void);

#ifdef __cplusplus
}
#endif
