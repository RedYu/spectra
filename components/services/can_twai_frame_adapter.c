#include "can_twai_frame_adapter.h"

#include <stddef.h>
#include <string.h>

_Static_assert(
    CAN_TWAI_CLASSIC_DATA_MAX_LENGTH ==
    CAN_FRAME_CLASSIC_DATA_MAX_LENGTH,
    "TWAI and shared Classical CAN payload sizes must match"
);

_Static_assert(
    CAN_TWAI_STANDARD_ID_MAX ==
    CAN_FRAME_STANDARD_ID_MAX,
    "TWAI and shared standard identifier limits must match"
);

_Static_assert(
    CAN_TWAI_EXTENDED_ID_MAX ==
    CAN_FRAME_EXTENDED_ID_MAX,
    "TWAI and shared extended identifier limits must match"
);

static bool can_twai_frame_identifier_valid(
    const can_twai_frame_t *frame
)
{
    if (frame == NULL) {
        return false;
    }

    if (frame->extended) {
        return frame->identifier <=
               CAN_TWAI_EXTENDED_ID_MAX;
    }

    return frame->identifier <=
           CAN_TWAI_STANDARD_ID_MAX;
}

esp_err_t can_twai_frame_to_common(
    const can_twai_frame_t *source,
    can_frame_t *destination
)
{
    if ((source == NULL) ||
        (destination == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    memset(
        destination,
        0,
        sizeof(*destination)
    );

    if (!can_twai_frame_identifier_valid(
            source
        )) {

        return ESP_ERR_INVALID_ARG;
    }

    if (source->data_length >
        CAN_TWAI_CLASSIC_DATA_MAX_LENGTH) {

        return ESP_ERR_INVALID_SIZE;
    }

    destination->bus =
        CAN_BUS_PRIMARY;

    destination->identifier =
        source->identifier;

    if (source->extended) {
        destination->flags |=
            CAN_FRAME_FLAG_EXTENDED_ID;
    }

    if (source->remote) {
        destination->flags |=
            CAN_FRAME_FLAG_REMOTE;
    }

    /*
     * The current TWAI driver model stores the Classical CAN DLC in
     * data_length. It supports DLC values from zero to eight.
     */
    destination->dlc =
        source->data_length;

    destination->data_length =
        source->remote
            ? 0U
            : source->data_length;

    if (!source->remote &&
        (source->data_length > 0U)) {

        memcpy(
            destination->data,
            source->data,
            source->data_length
        );
    }

    destination->timestamp_us =
        source->timestamp_us;

    destination->timestamp_source =
        (source->timestamp_us != 0U)
            ? CAN_TIMESTAMP_SOURCE_SOFTWARE
            : CAN_TIMESTAMP_SOURCE_NONE;

    return can_frame_validate(
        destination
    );
}

esp_err_t can_twai_frame_from_common(
    const can_frame_t *source,
    can_twai_frame_t *destination
)
{
    if ((source == NULL) ||
        (destination == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    memset(
        destination,
        0,
        sizeof(*destination)
    );

    if (source->bus !=
        CAN_BUS_PRIMARY) {

        return ESP_ERR_INVALID_ARG;
    }

    if ((source->flags &
         ~((uint32_t)CAN_FRAME_FLAGS_MASK)) != 0U) {

        return ESP_ERR_INVALID_ARG;
    }

    const bool fd =
        (source->flags &
         CAN_FRAME_FLAG_FD) != 0U;

    if (fd) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    const esp_err_t validation_result =
        can_frame_validate(
            source
        );

    if (validation_result != ESP_OK) {
        return validation_result;
    }

    const bool extended =
        (source->flags &
         CAN_FRAME_FLAG_EXTENDED_ID) != 0U;

    const bool remote =
        (source->flags &
         CAN_FRAME_FLAG_REMOTE) != 0U;

    if (source->dlc >
        CAN_TWAI_CLASSIC_DATA_MAX_LENGTH) {

        return ESP_ERR_INVALID_SIZE;
    }

    destination->identifier =
        source->identifier;

    destination->extended =
        extended;

    destination->remote =
        remote;

    if (remote) {
        destination->data_length =
            source->dlc;
    } else {
        destination->data_length =
            source->data_length;

        if (source->data_length > 0U) {
            memcpy(
                destination->data,
                source->data,
                source->data_length
            );
        }
    }

    destination->timestamp_us = 0U;

    return ESP_OK;
}
