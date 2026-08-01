#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Enable or disable GUI animations.
 *
 * This function is safe to call from a task other than the GUI task.
 *
 * @param[in] enabled True to enable GUI animations.
 */
void gui_config_set_animations_enabled(
    bool enabled
);

/**
 * @brief Get the current GUI-animation state.
 *
 * @return True when GUI animations are enabled; otherwise false.
 */
bool gui_config_get_animations_enabled(void);

#ifdef __cplusplus
}
#endif
