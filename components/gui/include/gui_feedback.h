/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file gui_feedback.h
 * @brief Audible feedback for LVGL controls.
 *
 * All functions must be called from the GUI task because they access
 * LVGL objects directly.
 */

/**
 * @brief Attach audible press feedback to an LVGL object.
 *
 * Call this function exactly once for each object. Repeated attachment
 * causes multiple click signals for one press.
 *
 * @param[in] object LVGL object receiving the feedback callback.
 * NULL is accepted and has no effect.
 */
void gui_feedback_attach(
    lv_obj_t *object
);

#ifdef __cplusplus
}
#endif
