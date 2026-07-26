#include "widgets/sd_card_modal.h"

#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"

#include "widgets/modal_dialog.h"
#include "storage_service.h"
#include "logging_service.h"
#include "settings_model.h"
#include "storage_sd_service.h"

#include "sd_card_driver.h"
#include "gui_config.h"

static const char *TAG =
    "sd_card_modal";

static modal_dialog_t s_sd_dialog;

static void sd_card_modal_action_cb(
    modal_dialog_t *dialog
);

static void sd_card_modal_close_cb(
    modal_dialog_t *dialog
);

static void sd_card_modal_update(
    modal_dialog_t *dialog
);

static void sd_card_modal_show_result(
    modal_dialog_t *dialog,
    esp_err_t result,
    bool mount_operation
);

void sd_card_modal_open(
    lv_obj_t *parent
)
{
    if (parent == NULL) {
        return;
    }

    /*
     * Prevent creating a second dialog while the first one is open.
     */
    if (modal_dialog_is_open(
            &s_sd_dialog
        )) {

        return;
    }

    const bool mounted =
        sd_card_driver_is_mounted();

    const modal_dialog_config_t config = {
        .title =
            mounted
                ? "SD Card Mounted"
                : "SD Card Not Mounted",

        .message =
            mounted
                ? "The SD card is mounted and ready to use."
                : "The SD card is not mounted.",

        .icon = NULL,

        /*
         * Primary button changes depending on the current state.
         */
        .primary_button_text =
            mounted
                ? "Unmount"
                : "Mount",

        .primary_action =
            sd_card_modal_action_cb,

        /*
         * Do not close automatically because the operation result
         * must be displayed in the same dialog.
         */
        .close_on_primary_action =
            false,

        .secondary_button_text =
            "Close",

        .secondary_action =
            sd_card_modal_close_cb,

        .close_on_secondary_action =
            true,

        .show_progress_bar =
            false,

        .initial_progress =
            0,

        .progress_text =
            NULL,

        .animate_open =
            true && gui_config_are_animations_enabled(),

        .close_on_overlay_click =
            false
    };

    if (!modal_dialog_create(
            &s_sd_dialog,
            parent,
            &config
        )) {

        ESP_LOGE(
            TAG,
            "Failed to create SD card dialog"
        );

        return;
    }
}

static void sd_card_modal_action_cb(
    modal_dialog_t *dialog
)
{
    if (dialog == NULL) {
        return;
    }

    /*
     * Read the state again.
     *
     * Do not rely only on the state that was read when the dialog
     * was created because it may have changed.
     */
    const bool mounted =
        sd_card_driver_is_mounted();

    modal_dialog_set_primary_enabled(
        dialog,
        false
    );

    modal_dialog_set_secondary_enabled(
        dialog,
        false
    );

    if (mounted) {
        modal_dialog_set_title(
            dialog,
            "Unmounting SD Card"
        );

        modal_dialog_set_message(
            dialog,
            "Please wait while the SD card is being unmounted."
        );

        const esp_err_t logging_result =
            logging_service_disable_file();

        if (logging_result != ESP_OK) {
            ESP_LOGW(
                TAG,
                "Failed to disable SD logging: %s",
                esp_err_to_name(logging_result)
            );
        }

        const esp_err_t result =
            sd_card_driver_unmount();

        storage_sd_service_set_state(result == ESP_OK ? 
            STORAGE_STATE_UNAVAILABLE : STORAGE_STATE_ERROR
        );
        
        sd_card_modal_show_result(
            dialog,
            result,
            false
        );
    } else {
        modal_dialog_set_title(
            dialog,
            "Mounting SD Card"
        );

        modal_dialog_set_message(
            dialog,
            "Please wait while the SD card is being mounted."
        );

        const esp_err_t result =
            sd_card_driver_mount();

        storage_sd_service_set_state(result == ESP_OK ? 
            STORAGE_STATE_MOUNTED : STORAGE_STATE_UNAVAILABLE
        );

        if (result == ESP_OK) {
            const app_settings_t *settings =
                settings_model_get();

            if (settings != NULL &&
                settings->logging.sd_enabled) {

                const esp_err_t logging_result =
                    logging_service_enable_file();

                if (logging_result != ESP_OK) {
                    ESP_LOGE(
                        TAG,
                        "Failed to enable SD logging: %s",
                        esp_err_to_name(logging_result)
                    );
                } else {
                    ESP_LOGI(
                        TAG,
                        "SD file logging enabled"
                    );
                }
            }
        }

        sd_card_modal_show_result(
            dialog,
            result,
            true
        );
    }
}

static void sd_card_modal_close_cb(
    modal_dialog_t *dialog
)
{
    /*
     * The dialog will be closed automatically because
     * close_on_secondary_action is enabled.
     */
    (void)dialog;
}

static void sd_card_modal_show_result(
    modal_dialog_t *dialog,
    esp_err_t result,
    bool mount_operation
)
{
    if (dialog == NULL) {
        return;
    }

    modal_dialog_set_secondary_enabled(
        dialog,
        true
    );

    if (result != ESP_OK) {
        char message[128];

        snprintf(
            message,
            sizeof(message),
            "%s failed.\nError: %s",
            mount_operation
                ? "SD card mounting"
                : "SD card unmounting",
            esp_err_to_name(result)
        );

        modal_dialog_set_title(
            dialog,
            "SD Card Error"
        );

        modal_dialog_set_message(
            dialog,
            message
        );

        modal_dialog_set_primary_enabled(
            dialog,
            true
        );

        ESP_LOGE(
            TAG,
            "%s failed: %s",
            mount_operation
                ? "Mount"
                : "Unmount",
            esp_err_to_name(result)
        );

        return;
    }

    sd_card_modal_update(
        dialog
    );
}

static void sd_card_modal_update(
    modal_dialog_t *dialog
)
{
    if (dialog == NULL) {
        return;
    }

    const bool mounted =
        sd_card_driver_is_mounted();

    if (mounted) {
        static char card_info[768];

        modal_dialog_set_title(
            dialog,
            "SD Card Information"
        );

        if (sd_card_get_info_text(
                card_info,
                sizeof(card_info)
            )) {

            modal_dialog_set_message(
                dialog,
                card_info
            );
        } else {
            modal_dialog_set_message(
                dialog,
                "The SD card is mounted, but its information could not be read."
            );
        }

        modal_dialog_set_primary_text(
            dialog,
            "Unmount"
        );
    } else {
        modal_dialog_set_title(
            dialog,
            "SD Card"
        );

        modal_dialog_set_message(
            dialog,
            "The SD card is not mounted."
        );

        modal_dialog_set_primary_text(
            dialog,
            "Mount"
        );
    }

    modal_dialog_set_primary_enabled(
        dialog,
        true
    );

    modal_dialog_set_secondary_enabled(
        dialog,
        true
    );
}

