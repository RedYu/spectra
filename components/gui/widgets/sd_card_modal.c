#include "widgets/sd_card_modal.h"

#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"

#include "widgets/modal_dialog.h"
#include "logging_service.h"
#include "settings_model.h"
#include "storage_sd_service.h"

#include "sd_card_driver.h"
#include "gui_config.h"

static const char *TAG =
    "sd_card_modal";

static modal_dialog_t s_sd_dialog = {0};

static void sd_card_modal_action_cb(
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

bool sd_card_modal_open(
    lv_obj_t *parent
)
{
    if (parent == NULL) {
        return false;
    }

    if (modal_dialog_is_open(
            &s_sd_dialog
        )) {

        return true;
    }

    storage_sd_state_t state;

    const esp_err_t state_result =
        storage_sd_service_get_state(
            &state
        );

    if (state_result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to get SD storage state: %s",
            esp_err_to_name(state_result)
        );

        return false;
    }

    const bool mounted =
        state == STORAGE_SD_STATE_MOUNTED;

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

        .primary_button_text =
            mounted
                ? "Unmount"
                : "Mount",

        .primary_action =
            sd_card_modal_action_cb,

        .close_on_primary_action =
            false,

        .secondary_button_text =
            "Close",

        .secondary_action =
            NULL,

        .close_on_secondary_action =
            true,

        .show_progress_bar =
            false,

        .initial_progress =
            0U,

        .progress_text =
            NULL,

        .animate_open =
            gui_config_get_animations_enabled(),

        .close_on_overlay_click =
            false,
    };

    if (!modal_dialog_create(
            &s_sd_dialog,
            parent,
            &config
        )) {

        ESP_LOGE(
            TAG,
            "Failed to create SD-card dialog"
        );

        return false;
    }

    sd_card_modal_update(
        &s_sd_dialog
    );

    return true;
}

static void sd_card_modal_action_cb(
    modal_dialog_t *dialog
)
{
    if ((dialog == NULL) ||
        !modal_dialog_is_open(dialog)) {

        return;
    }

    storage_sd_state_t state;

    esp_err_t result =
        storage_sd_service_get_state(
            &state
        );

    if (result != ESP_OK) {
        modal_dialog_set_title(
            dialog,
            "SD Card Error"
        );

        modal_dialog_set_message(
            dialog,
            "Failed to read the current SD-card state."
        );

        modal_dialog_set_primary_enabled(
            dialog,
            false
        );

        modal_dialog_set_secondary_enabled(
            dialog,
            true
        );

        ESP_LOGE(
            TAG,
            "Failed to get SD storage state: %s",
            esp_err_to_name(result)
        );

        return;
    }

    const bool unmount_operation =
        state == STORAGE_SD_STATE_MOUNTED;

    modal_dialog_set_primary_enabled(
        dialog,
        false
    );

    modal_dialog_set_secondary_enabled(
        dialog,
        false
    );

    if (unmount_operation) {
        modal_dialog_set_title(
            dialog,
            "Unmounting SD Card"
        );

        modal_dialog_set_message(
            dialog,
            "Please wait while the SD card is being unmounted."
        );

        result =
            storage_sd_service_unmount();
    } else {
        modal_dialog_set_title(
            dialog,
            "Mounting SD Card"
        );

        modal_dialog_set_message(
            dialog,
            "Please wait while the SD card is being mounted."
        );

        result =
            storage_sd_service_mount();

        if (result == ESP_OK) {
            app_settings_t settings;

            const esp_err_t settings_result =
                settings_model_get(
                    &settings
                );

            if (settings_result != ESP_OK) {
                ESP_LOGW(
                    TAG,
                    "Failed to get logging setting: %s",
                    esp_err_to_name(settings_result)
                );
            } else if (settings.logging.sd_enabled) {
                const esp_err_t logging_result =
                    logging_service_enable_file();

                if ((logging_result != ESP_OK) &&
                    (logging_result != ESP_ERR_INVALID_STATE)) {

                    ESP_LOGW(
                        TAG,
                        "Failed to enable SD logging: %s",
                        esp_err_to_name(logging_result)
                    );
                }
            }
        }
    }

    sd_card_modal_show_result(
        dialog,
        result,
        !unmount_operation
    );
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
    if ((dialog == NULL) ||
        !modal_dialog_is_open(dialog)) {

        return;
    }

    storage_sd_state_t state;

    const esp_err_t state_result =
        storage_sd_service_get_state(
            &state
        );

    if (state_result != ESP_OK) {
        modal_dialog_set_title(
            dialog,
            "SD Card Error"
        );

        modal_dialog_set_message(
            dialog,
            esp_err_to_name(state_result)
        );

        modal_dialog_set_primary_enabled(
            dialog,
            false
        );

        modal_dialog_set_secondary_enabled(
            dialog,
            true
        );

        return;
    }

    if (state == STORAGE_SD_STATE_MOUNTED) {
        char card_info[768];

        modal_dialog_set_title(
            dialog,
            "SD Card Information"
        );

        const esp_err_t info_result =
            sd_card_driver_get_info_text(
                card_info,
                sizeof(card_info)
            );

        if (info_result == ESP_OK) {
            modal_dialog_set_message(
                dialog,
                card_info
            );
        } else {
            modal_dialog_set_message(
                dialog,
                "The SD card is mounted, but its information could not be read."
            );

            ESP_LOGW(
                TAG,
                "Failed to get SD-card information: %s",
                esp_err_to_name(info_result)
            );
        }

        modal_dialog_set_primary_text(
            dialog,
            "Unmount"
        );
    } else if (state == STORAGE_SD_STATE_ERROR) {
        modal_dialog_set_title(
            dialog,
            "SD Card Error"
        );

        modal_dialog_set_message(
            dialog,
            "The SD-card service reported an error."
        );

        /*
         * Allow another mount attempt, which may recover a transient
         * card or SPI error.
         */
        modal_dialog_set_primary_text(
            dialog,
            "Retry"
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
