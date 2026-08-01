#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the GUI service resources.
 *
 * Creates the boot-progress queue and initializes internal service
 * state. LVGL and GUI screens are initialized later by the GUI task.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if already
 * initialized, ESP_ERR_NO_MEM if required resources cannot be created,
 * otherwise an ESP-IDF error code.
 */
esp_err_t gui_service_init(void);

/**
 * @brief Create and start the GUI task.
 *
 * The GUI task initializes the LVGL port, registers application screens
 * and runs the LVGL handler loop.
 *
 * A successful return confirms that the task was created, but does not
 * confirm that asynchronous LVGL and screen initialization succeeded.
 *
 * @return ESP_OK when the GUI task is created,
 * ESP_ERR_INVALID_STATE if the service is not initialized or has
 * already been started, ESP_ERR_NO_MEM if the GUI task cannot be
 * created, otherwise an ESP-IDF error code.
 */
esp_err_t gui_service_start(void);

/**
 * @brief Update the boot-screen progress and status text.
 *
 * Stores the latest boot progress update for processing by the GUI
 * task. This function may be called after gui_service_init() and before
 * gui_service_start(). The progress value is limited to 0 through 100.
 *
 * @param[in] progress Boot progress percentage.
 * @param[in] status Null-terminated status text.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if status is NULL,
 * ESP_ERR_INVALID_STATE if the GUI service is not initialized,
 * otherwise an ESP-IDF error code.
 */
esp_err_t gui_service_set_boot_progress(
    uint8_t progress,
    const char *status
);

#ifdef __cplusplus
}
#endif
