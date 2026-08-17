#include "can_twai_driver.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"

#include "board_config.h"

#define CAN_TWAI_TIMESTAMP_RESOLUTION_HZ    (1000000U)
#define CAN_TWAI_INTERRUPT_PRIORITY         (1)

typedef struct
{
    twai_frame_t native_frame;

    uint8_t data[
        CAN_TWAI_CLASSIC_DATA_MAX_LENGTH
    ];

    uint32_t transmission_id;
    void *transmission_context;

    bool confirmation_requested;
    bool in_use;

} can_twai_tx_slot_t;

static const char *TAG =
    "can_twai_driver";

static uint32_t s_next_transmission_id = 1U;

static twai_node_handle_t s_node = NULL;

static QueueHandle_t s_rx_queue = NULL;
static SemaphoreHandle_t s_tx_slots_available = NULL;

static can_twai_tx_slot_t *s_tx_slots = NULL;
static size_t s_tx_slot_count = 0U;

static can_twai_driver_config_t s_config;
static can_twai_driver_info_t s_info;

static portMUX_TYPE s_lock =
    portMUX_INITIALIZER_UNLOCKED;


static uint32_t can_twai_driver_next_transmission_id(void)
{
    uint32_t transmission_id;

    portENTER_CRITICAL(
        &s_lock
    );

    transmission_id =
        s_next_transmission_id++;

    if (s_next_transmission_id == 0U) {
        s_next_transmission_id = 1U;
    }

    portEXIT_CRITICAL(
        &s_lock
    );

    return transmission_id;
}

static can_twai_state_t can_twai_driver_map_state(
    twai_error_state_t state
)
{
    switch (state) {
        case TWAI_ERROR_ACTIVE:
            return CAN_TWAI_STATE_ERROR_ACTIVE;

        case TWAI_ERROR_WARNING:
            return CAN_TWAI_STATE_ERROR_WARNING;

        case TWAI_ERROR_PASSIVE:
            return CAN_TWAI_STATE_ERROR_PASSIVE;

        case TWAI_ERROR_BUS_OFF:
            return CAN_TWAI_STATE_BUS_OFF;

        default:
            return CAN_TWAI_STATE_UNKNOWN;
    }
}

static bool can_twai_driver_validate_filter(
    const can_twai_acceptance_filter_t *filter
)
{
    if (filter == NULL) {
        return false;
    }

    const uint32_t identifier_max =
        filter->extended
            ? CAN_TWAI_EXTENDED_ID_MAX
            : CAN_TWAI_STANDARD_ID_MAX;

    if ((filter->identifier & ~identifier_max) != 0U) {
        return false;
    }

    if ((filter->mask & ~identifier_max) != 0U) {
        return false;
    }

    return true;
}

static bool can_twai_driver_validate_config(
    const can_twai_driver_config_t *config
)
{
    if (config == NULL) {
        return false;
    }

    if (config->bitrate == 0U) {
        return false;
    }

    if ((config->sample_point_permill > 0U) &&
        ((config->sample_point_permill < 500U) ||
         (config->sample_point_permill >= 1000U))) {

        return false;
    }

    if ((config->mode < CAN_TWAI_MODE_NORMAL) ||
        (config->mode > CAN_TWAI_MODE_SELF_TEST)) {

        return false;
    }

    if ((config->tx_queue_depth == 0U) ||
        (config->rx_queue_length == 0U)) {

        return false;
    }

    if ((config->transmit_retry_count < -1) ||
        (config->transmit_retry_count > 15)) {

        return false;
    }

    if (!can_twai_driver_validate_filter(
            &config->acceptance_filter
        )) {

        return false;
    }

    return true;
}

static void can_twai_driver_reset_tx_slots(
    bool count_aborted
)
{
    if ((s_tx_slots == NULL) ||
        (s_tx_slots_available == NULL)) {

        return;
    }

    while (xSemaphoreTake(
               s_tx_slots_available,
               0U
           ) == pdTRUE) {
    }

    uint32_t aborted_count = 0U;

    portENTER_CRITICAL(
        &s_lock
    );

    for (size_t index = 0U;
         index < s_tx_slot_count;
         ++index) {

        if (s_tx_slots[index].in_use) {
            ++aborted_count;
        }

        s_tx_slots[index].in_use = false;
        s_tx_slots[index].confirmation_requested = false;
        s_tx_slots[index].transmission_id = 0U;
        s_tx_slots[index].transmission_context = NULL;
    }

    s_info.tx_slots_used = 0U;

    if (count_aborted) {
        s_info.aborted_transmissions +=
            aborted_count;
    }

    portEXIT_CRITICAL(
        &s_lock
    );

    for (size_t index = 0U;
         index < s_tx_slot_count;
         ++index) {

        (void)xSemaphoreGive(
            s_tx_slots_available
        );
    }
}

static can_twai_tx_slot_t *
can_twai_driver_allocate_tx_slot(void)
{
    can_twai_tx_slot_t *slot = NULL;

    portENTER_CRITICAL(
        &s_lock
    );

    for (size_t index = 0U;
         index < s_tx_slot_count;
         ++index) {

        if (!s_tx_slots[index].in_use) {
            s_tx_slots[index].in_use = true;
            slot = &s_tx_slots[index];

            ++s_info.tx_slots_used;

            if (s_info.tx_slots_used >
                s_info.tx_slots_peak) {

                s_info.tx_slots_peak =
                    s_info.tx_slots_used;
            }

            break;
        }
    }

    portEXIT_CRITICAL(
        &s_lock
    );

    return slot;
}

static void can_twai_driver_release_tx_slot(
    can_twai_tx_slot_t *slot
)
{
    if (slot == NULL) {
        return;
    }

    portENTER_CRITICAL(
        &s_lock
    );

    if (slot->in_use) {
        slot->in_use = false;

        if (s_info.tx_slots_used > 0U) {
            --s_info.tx_slots_used;
        }
    }

    portEXIT_CRITICAL(
        &s_lock
    );

    if (s_tx_slots_available != NULL) {
        (void)xSemaphoreGive(
            s_tx_slots_available
        );
    }
}

static bool can_twai_driver_tx_done_callback(
    twai_node_handle_t handle,
    const twai_tx_done_event_data_t *event_data,
    void *user_context
)
{
    (void)handle;
    (void)user_context;

    if ((event_data == NULL) ||
        (event_data->done_tx_frame == NULL) ||
        (s_tx_slots == NULL)) {

        return false;
    }

    BaseType_t higher_priority_task_woken =
        pdFALSE;

    can_twai_tx_confirmation_t confirmation = {0};

    can_twai_tx_confirmation_cb_t callback = NULL;
    void *callback_context = NULL;

    bool confirmation_requested = false;
    bool slot_found = false;

    portENTER_CRITICAL_ISR(
        &s_lock
    );

    for (size_t index = 0U;
         index < s_tx_slot_count;
         ++index) {

        can_twai_tx_slot_t *slot =
            &s_tx_slots[index];

        if ((&slot->native_frame ==
             event_data->done_tx_frame) &&
            slot->in_use) {

            confirmation.transmission_id =
                slot->transmission_id;

            confirmation.identifier =
                slot->native_frame.header.id;

            confirmation.extended =
                slot->native_frame.header.ide;

            confirmation.remote =
                slot->native_frame.header.rtr;

            confirmation.successful =
                event_data->is_tx_success;

            confirmation.transmission_context =
                slot->transmission_context;

            confirmation_requested =
                slot->confirmation_requested;

            callback =
                s_config.tx_confirmation_callback;

            callback_context =
                s_config.tx_confirmation_context;

            
            slot->in_use = false;

            if (s_info.tx_slots_used > 0U) {
                --s_info.tx_slots_used;
            }

            slot->confirmation_requested = false;
            slot->transmission_id = 0U;
            slot->transmission_context = NULL;

            if (event_data->is_tx_success) {
                ++s_info.transmitted_frames;
                ++s_info.successful_transmissions;
            } else {
                ++s_info.failed_transmissions;
            }

            slot_found = true;
            break;
        }
    }

    portEXIT_CRITICAL_ISR(
        &s_lock
    );

    if (slot_found &&
        confirmation_requested &&
        (callback != NULL)) {

        if (callback(
                &confirmation,
                callback_context
            )) {

            higher_priority_task_woken =
                pdTRUE;
        }
    }

    if (slot_found &&
        (s_tx_slots_available != NULL)) {

        BaseType_t semaphore_task_woken =
            pdFALSE;

        (void)xSemaphoreGiveFromISR(
            s_tx_slots_available,
            &semaphore_task_woken
        );

        if (semaphore_task_woken == pdTRUE) {
            higher_priority_task_woken =
                pdTRUE;
        }
    }

    return higher_priority_task_woken ==
           pdTRUE;
}

static bool can_twai_driver_rx_done_callback(
    twai_node_handle_t handle,
    const twai_rx_done_event_data_t *event_data,
    void *user_context
)
{
    (void)event_data;
    (void)user_context;

    if (s_rx_queue == NULL) {
        return false;
    }

    uint8_t native_data[
        CAN_TWAI_CLASSIC_DATA_MAX_LENGTH
    ] = {0};

    twai_frame_t native_frame = {
        .buffer = native_data,
        .buffer_len = sizeof(native_data),
    };

    if (twai_node_receive_from_isr(
            handle,
            &native_frame
        ) != ESP_OK) {

        return false;
    }

    /*
     * ESP32-S3 supports only Classical CAN.
     */
    if (native_frame.header.fdf ||
        (native_frame.header.dlc >
         CAN_TWAI_CLASSIC_DATA_MAX_LENGTH)) {

        portENTER_CRITICAL_ISR(
            &s_lock
        );

        ++s_info.dropped_rx_frames;

        portEXIT_CRITICAL_ISR(
            &s_lock
        );

        return false;
    }

    can_twai_frame_t frame = {
        .identifier =
            native_frame.header.id,

        .data_length =
            (uint8_t)native_frame.header.dlc,

        .extended =
            native_frame.header.ide,

        .remote =
            native_frame.header.rtr,

        .timestamp_us =
            native_frame.header.timestamp,
    };

    if (!frame.remote &&
        (frame.data_length > 0U)) {

        memcpy(
            frame.data,
            native_data,
            frame.data_length
        );
    }

    BaseType_t higher_priority_task_woken =
        pdFALSE;

    if (xQueueSendFromISR(
            s_rx_queue,
            &frame,
            &higher_priority_task_woken
        ) == pdTRUE) {

        const UBaseType_t queue_current =
            uxQueueMessagesWaitingFromISR(
                s_rx_queue
            );

        portENTER_CRITICAL_ISR(
            &s_lock
        );

        ++s_info.received_frames;

        if ((uint32_t)queue_current >
            s_info.rx_queue_peak) {

            s_info.rx_queue_peak =
                (uint32_t)queue_current;
        }

        portEXIT_CRITICAL_ISR(
            &s_lock
        );

    } else {
        portENTER_CRITICAL_ISR(
            &s_lock
        );

        ++s_info.dropped_rx_frames;

        portEXIT_CRITICAL_ISR(
            &s_lock
        );
    }

    return higher_priority_task_woken ==
           pdTRUE;
}

static bool can_twai_driver_state_change_callback(
    twai_node_handle_t handle,
    const twai_state_change_event_data_t *event_data,
    void *user_context
)
{
    (void)handle;
    (void)user_context;

    if (event_data == NULL) {
        return false;
    }

    portENTER_CRITICAL_ISR(
        &s_lock
    );

    s_info.state =
        can_twai_driver_map_state(
            event_data->new_sta
        );

    portEXIT_CRITICAL_ISR(
        &s_lock
    );

    return false;
}

static bool can_twai_driver_error_callback(
    twai_node_handle_t handle,
    const twai_error_event_data_t *event_data,
    void *user_context
)
{
    (void)handle;
    (void)user_context;

    if (event_data == NULL) {
        return false;
    }

    portENTER_CRITICAL_ISR(
        &s_lock
    );

    if (event_data->err_flags.arb_lost) {
        ++s_info.arbitration_lost_count;
    }

    if (event_data->err_flags.bit_err) {
        ++s_info.bit_error_count;
    }

    if (event_data->err_flags.form_err) {
        ++s_info.form_error_count;
    }

    if (event_data->err_flags.stuff_err) {
        ++s_info.stuff_error_count;
    }

    if (event_data->err_flags.ack_err) {
        ++s_info.acknowledgement_error_count;
    }

    portEXIT_CRITICAL_ISR(
        &s_lock
    );

    return false;
}

static esp_err_t can_twai_driver_apply_acceptance_filter(
    const can_twai_acceptance_filter_t *filter
)
{
    if ((filter == NULL) ||
        (s_node == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    const twai_mask_filter_config_t filter_config = {
        .id =
            filter->identifier,

        .mask =
            filter->mask,

        .is_ext =
            filter->extended,

        .no_classic = false,
        .no_fd = true,
        .dual_filter = false,
    };

    return twai_node_config_mask_filter(
        s_node,
        0U,
        &filter_config
    );
}

static void can_twai_driver_restore_statistics(
    const can_twai_driver_info_t *previous_info
)
{
    if (previous_info == NULL) {
        return;
    }

    portENTER_CRITICAL(
        &s_lock
    );

    s_info.transmitted_frames =
        previous_info->transmitted_frames;

    s_info.received_frames =
        previous_info->received_frames;

    s_info.dropped_rx_frames =
        previous_info->dropped_rx_frames;

    s_info.arbitration_lost_count =
        previous_info->arbitration_lost_count;

    s_info.bit_error_count =
        previous_info->bit_error_count;

    s_info.form_error_count =
        previous_info->form_error_count;

    s_info.stuff_error_count =
        previous_info->stuff_error_count;

    s_info.transmit_error_count =
        previous_info->transmit_error_count;

    s_info.receive_error_count =
        previous_info->receive_error_count;

    s_info.bus_error_count =
        previous_info->bus_error_count;

    s_info.acknowledgement_error_count =
        previous_info->acknowledgement_error_count;

    s_info.successful_transmissions =
        previous_info->successful_transmissions;

    s_info.failed_transmissions =
        previous_info->failed_transmissions;

    s_info.aborted_transmissions =
        previous_info->aborted_transmissions;

    if (previous_info->tx_slots_peak >
        s_info.tx_slots_peak) {

        s_info.tx_slots_peak =
            previous_info->tx_slots_peak;
    }

    if (previous_info->rx_queue_peak >
        s_info.rx_queue_peak) {

        s_info.rx_queue_peak =
            previous_info->rx_queue_peak;
    }

    portEXIT_CRITICAL(
        &s_lock
    );
}

esp_err_t can_twai_driver_init(
    const can_twai_driver_config_t *config
)
{
    if (!can_twai_driver_validate_config(
            config
        )) {

        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(
        &s_lock
    );

    const bool already_initialized =
        s_info.initialized;

    portEXIT_CRITICAL(
        &s_lock
    );

    if (already_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    s_rx_queue =
        xQueueCreate(
            config->rx_queue_length,
            sizeof(can_twai_frame_t)
        );

    if (s_rx_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_tx_slots =
        calloc(
            config->tx_queue_depth,
            sizeof(*s_tx_slots)
        );

    if (s_tx_slots == NULL) {
        vQueueDelete(
            s_rx_queue
        );

        s_rx_queue = NULL;

        return ESP_ERR_NO_MEM;
    }

    s_tx_slot_count =
        config->tx_queue_depth;

    s_tx_slots_available =
        xSemaphoreCreateCounting(
            config->tx_queue_depth,
            config->tx_queue_depth
        );

    if (s_tx_slots_available == NULL) {
        free(
            s_tx_slots
        );

        vQueueDelete(
            s_rx_queue
        );

        s_tx_slots = NULL;
        s_tx_slot_count = 0U;
        s_rx_queue = NULL;

        return ESP_ERR_NO_MEM;
    }

    const twai_onchip_node_config_t node_config = {
        .io_cfg = {
            .tx =
                CAN_PRIMARY_PIN_TX,

            .rx =
                CAN_PRIMARY_PIN_RX,

            .quanta_clk_out =
                GPIO_NUM_NC,

            .bus_off_indicator =
                GPIO_NUM_NC,
        },

        .clk_src =
            0,

        .bit_timing = {
            .bitrate =
                config->bitrate,

            .sp_permill =
                config->sample_point_permill,

            .ssp_permill =
                0U,
        },

        .timestamp_resolution_hz =
            CAN_TWAI_TIMESTAMP_RESOLUTION_HZ,

        .fail_retry_cnt =
            config->transmit_retry_count,

        .tx_queue_depth =
            config->tx_queue_depth,

        .intr_priority =
            CAN_TWAI_INTERRUPT_PRIORITY,

        .flags = {
            .enable_self_test =
                config->mode ==
                CAN_TWAI_MODE_SELF_TEST,

            .enable_loopback =
                config->mode ==
                CAN_TWAI_MODE_SELF_TEST,

            .enable_listen_only =
                config->mode ==
                CAN_TWAI_MODE_LISTEN_ONLY,

            .no_receive_rtr =
                false,

            .sleep_allow_pd =
                false,
        },
    };

    esp_err_t result =
        twai_new_node_onchip(
            &node_config,
            &s_node
        );

    if (result != ESP_OK) {
        goto initialization_failed;
    }

    result =
        can_twai_driver_apply_acceptance_filter(
            &config->acceptance_filter
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to configure TWAI acceptance filter: %s",
            esp_err_to_name(result)
        );

        (void)twai_node_delete(
            s_node
        );

        s_node = NULL;

        goto initialization_failed;
    }

    const twai_event_callbacks_t callbacks = {
        .on_tx_done =
            can_twai_driver_tx_done_callback,

        .on_rx_done =
            can_twai_driver_rx_done_callback,

        .on_state_change =
            can_twai_driver_state_change_callback,

        .on_error =
            can_twai_driver_error_callback,
    };

    result =
        twai_node_register_event_callbacks(
            s_node,
            &callbacks,
            NULL
        );

    if (result != ESP_OK) {
        (void)twai_node_delete(
            s_node
        );

        s_node = NULL;

        goto initialization_failed;
    }

    s_config = *config;

    portENTER_CRITICAL(
        &s_lock
    );

    memset(
        &s_info,
        0,
        sizeof(s_info)
    );

    s_info.initialized = true;
    s_info.started = false;
    s_info.state = CAN_TWAI_STATE_STOPPED;
    s_info.mode = config->mode;
    s_info.bitrate = config->bitrate;
    s_info.sample_point_permill =
        config->sample_point_permill;

    s_info.tx_slots_capacity =
        config->tx_queue_depth;

    s_info.rx_queue_capacity =
        config->rx_queue_length;

    portEXIT_CRITICAL(
        &s_lock
    );

    ESP_LOGI(
        TAG,
        "TWAI initialized: bitrate=%lu, "
        "filter=%s ID=0x%08lX mask=0x%08lX",
        (unsigned long)config->bitrate,
        config->acceptance_filter.extended
            ? "extended"
            : "standard",
        (unsigned long)
            config->acceptance_filter.identifier,
        (unsigned long)
            config->acceptance_filter.mask
    );

    return ESP_OK;

initialization_failed:
    vSemaphoreDelete(
        s_tx_slots_available
    );

    free(
        s_tx_slots
    );

    vQueueDelete(
        s_rx_queue
    );

    s_tx_slots_available = NULL;
    s_tx_slots = NULL;
    s_tx_slot_count = 0U;
    s_rx_queue = NULL;
    s_node = NULL;

    return result;
}

esp_err_t can_twai_driver_start(void)
{
    portENTER_CRITICAL(
        &s_lock
    );

    const bool initialized =
        s_info.initialized;

    const bool started =
        s_info.started;

    portEXIT_CRITICAL(
        &s_lock
    );

    if (!initialized ||
        started ||
        (s_node == NULL) ||
        (s_rx_queue == NULL)) {

        return ESP_ERR_INVALID_STATE;
    }

    (void)xQueueReset(
        s_rx_queue
    );

    can_twai_driver_reset_tx_slots(false);

    /*
     * Establish an initial state before enabling the node. ISR
     * callbacks may replace it as soon as the controller starts.
     */
    portENTER_CRITICAL(
        &s_lock
    );

    s_info.state =
        CAN_TWAI_STATE_ERROR_ACTIVE;

    portEXIT_CRITICAL(
        &s_lock
    );

    const esp_err_t result =
        twai_node_enable(
            s_node
        );

    if (result != ESP_OK) {
        portENTER_CRITICAL(
            &s_lock
        );

        s_info.state =
            CAN_TWAI_STATE_STOPPED;

        portEXIT_CRITICAL(
            &s_lock
        );

        return result;
    }

    portENTER_CRITICAL(
        &s_lock
    );

    s_info.started = true;

    portEXIT_CRITICAL(
        &s_lock
    );

    ESP_LOGI(
        TAG,
        "Primary TWAI started"
    );

    return ESP_OK;
}

esp_err_t can_twai_driver_stop(void)
{
    portENTER_CRITICAL(
        &s_lock
    );

    const bool initialized =
        s_info.initialized;

    const bool started =
        s_info.started;

    portEXIT_CRITICAL(
        &s_lock
    );

    if (!initialized ||
        !started ||
        (s_node == NULL)) {

        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t result =
        twai_node_disable(
            s_node
        );

    if (result != ESP_OK) {
        return result;
    }

    (void)xQueueReset(
        s_rx_queue
    );

    can_twai_driver_reset_tx_slots(true);

    portENTER_CRITICAL(
        &s_lock
    );

    s_info.started = false;
    s_info.state =
        CAN_TWAI_STATE_STOPPED;

    portEXIT_CRITICAL(
        &s_lock
    );

    ESP_LOGI(
        TAG,
        "Primary TWAI stopped"
    );

    return ESP_OK;
}

esp_err_t can_twai_driver_deinit(void)
{
    portENTER_CRITICAL(
        &s_lock
    );

    const bool initialized =
        s_info.initialized;

    const bool started =
        s_info.started;

    portEXIT_CRITICAL(
        &s_lock
    );

    if (!initialized) {
        return ESP_OK;
    }

    if (started) {
        const esp_err_t stop_result =
            can_twai_driver_stop();

        if (stop_result != ESP_OK) {
            return stop_result;
        }
    }

    const esp_err_t result =
        twai_node_delete(
            s_node
        );

    if (result != ESP_OK) {
        return result;
    }

    s_node = NULL;

    if (s_tx_slots_available != NULL) {
        vSemaphoreDelete(
            s_tx_slots_available
        );

        s_tx_slots_available = NULL;
    }

    if (s_rx_queue != NULL) {
        vQueueDelete(
            s_rx_queue
        );

        s_rx_queue = NULL;
    }

    free(
        s_tx_slots
    );

    s_tx_slots = NULL;
    s_tx_slot_count = 0U;

    memset(
        &s_config,
        0,
        sizeof(s_config)
    );

    portENTER_CRITICAL(
        &s_lock
    );

    memset(
        &s_info,
        0,
        sizeof(s_info)
    );

    s_info.state =
        CAN_TWAI_STATE_STOPPED;

    portEXIT_CRITICAL(
        &s_lock
    );

    ESP_LOGI(
        TAG,
        "Primary TWAI deinitialized"
    );

    return ESP_OK;
}

static bool can_twai_driver_filters_equal(
    const can_twai_acceptance_filter_t *first,
    const can_twai_acceptance_filter_t *second
)
{
    if ((first == NULL) ||
        (second == NULL)) {

        return false;
    }

    return
        (first->identifier == second->identifier) &&
        (first->mask == second->mask) &&
        (first->extended == second->extended);
}

static bool can_twai_driver_configs_equal(
    const can_twai_driver_config_t *first,
    const can_twai_driver_config_t *second
)
{
    if ((first == NULL) ||
        (second == NULL)) {

        return false;
    }

    return
        (first->bitrate == second->bitrate) &&
        (first->sample_point_permill ==
         second->sample_point_permill) &&
        (first->mode == second->mode) &&
        (first->tx_queue_depth ==
         second->tx_queue_depth) &&
        (first->rx_queue_length ==
         second->rx_queue_length) &&
        (first->transmit_retry_count ==
         second->transmit_retry_count) &&
        (first->tx_confirmation_callback ==
         second->tx_confirmation_callback) &&
        (first->tx_confirmation_context ==
         second->tx_confirmation_context) &&
        can_twai_driver_filters_equal(
            &first->acceptance_filter,
            &second->acceptance_filter
        );
}

esp_err_t can_twai_driver_reconfigure(
    const can_twai_driver_config_t *config
)
{
    if (!can_twai_driver_validate_config(
            config
        )) {

        return ESP_ERR_INVALID_ARG;
    }

    can_twai_driver_info_t previous_info;

    portENTER_CRITICAL(
        &s_lock
    );

    previous_info = s_info;

    portEXIT_CRITICAL(
        &s_lock
    );

    if (!previous_info.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    const can_twai_driver_config_t previous_config =
        s_config;

    if (can_twai_driver_configs_equal(
            &previous_config,
            config
        )) {

        return ESP_OK;
    }

    const bool was_started =
        previous_info.started;

    if (was_started) {
        const esp_err_t stop_result =
            can_twai_driver_stop();

        if (stop_result != ESP_OK) {
            return stop_result;
        }

        portENTER_CRITICAL(
            &s_lock
        );

        previous_info = s_info;

        portEXIT_CRITICAL(
            &s_lock
        );
    }

    esp_err_t result =
        can_twai_driver_deinit();

    if (result != ESP_OK) {
        /*
         * Deinitialization failed before the old configuration was
         * completely removed. Try to return it to the running state.
         */
        if (was_started &&
            can_twai_driver_is_initialized() &&
            !can_twai_driver_is_started()) {

            (void)can_twai_driver_start();
        }

        return result;
    }

    result =
        can_twai_driver_init(
            config
        );

    if (result == ESP_OK) {
        if (was_started) {
            result =
                can_twai_driver_start();
        }

        if (result == ESP_OK) {
            can_twai_driver_restore_statistics(
                &previous_info
            );

            ESP_LOGI(
                TAG,
                "TWAI reconfigured: bitrate=%lu, "
                "mode=%u, TX queue=%u, RX queue=%u",
                (unsigned long)config->bitrate,
                (unsigned int)config->mode,
                (unsigned int)config->tx_queue_depth,
                (unsigned int)config->rx_queue_length
            );

            return ESP_OK;
        }

        /*
         * The new node was created, but it could not be started.
         */
        ESP_LOGE(
            TAG,
            "Failed to start reconfigured TWAI: %s",
            esp_err_to_name(result)
        );

        (void)can_twai_driver_deinit();
    } else {
        ESP_LOGE(
            TAG,
            "Failed to initialize new TWAI configuration: %s",
            esp_err_to_name(result)
        );
    }

    const esp_err_t original_result =
        result;

    ESP_LOGW(
        TAG,
        "Restoring previous TWAI configuration"
    );

    esp_err_t restore_result =
        can_twai_driver_init(
            &previous_config
        );

    if ((restore_result == ESP_OK) &&
        was_started) {

        restore_result =
            can_twai_driver_start();
    }

    if (restore_result == ESP_OK) {
        can_twai_driver_restore_statistics(
            &previous_info
        );

        ESP_LOGI(
            TAG,
            "Previous TWAI configuration restored"
        );
    } else {
        ESP_LOGE(
            TAG,
            "Failed to restore previous TWAI configuration: %s",
            esp_err_to_name(restore_result)
        );
    }

    return original_result;
}

static esp_err_t can_twai_driver_transmit_internal(
    const can_twai_frame_t *frame,
    void *transmission_context,
    bool confirmation_requested,
    uint32_t timeout_ms,
    uint32_t *transmission_id
)
{
    if (frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if ((frame->data_length >
         CAN_TWAI_CLASSIC_DATA_MAX_LENGTH) ||
        (frame->extended &&
         (frame->identifier >
          CAN_TWAI_EXTENDED_ID_MAX)) ||
        (!frame->extended &&
         (frame->identifier >
          CAN_TWAI_STANDARD_ID_MAX))) {

        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(
        &s_lock
    );

    const bool started =
        s_info.started;

    portEXIT_CRITICAL(
        &s_lock
    );

    if (!started ||
        (s_node == NULL) ||
        (s_tx_slots_available == NULL)) {

        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_tx_slots_available,
            pdMS_TO_TICKS(
                timeout_ms
            )
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    can_twai_tx_slot_t *slot =
        can_twai_driver_allocate_tx_slot();

    if (slot == NULL) {
        (void)xSemaphoreGive(
            s_tx_slots_available
        );

        return ESP_FAIL;
    }

    const uint32_t assigned_transmission_id =
        confirmation_requested
            ? can_twai_driver_next_transmission_id()
            : 0U;

    slot->confirmation_requested =
        confirmation_requested;

    slot->transmission_context =
        transmission_context;

    slot->transmission_id =
        assigned_transmission_id;

    if (transmission_id != NULL) {
        *transmission_id =
            assigned_transmission_id;
    }

    memset(
        &slot->native_frame,
        0,
        sizeof(slot->native_frame)
    );

    memset(
        slot->data,
        0,
        sizeof(slot->data)
    );

    if (!frame->remote &&
        (frame->data_length > 0U)) {

        memcpy(
            slot->data,
            frame->data,
            frame->data_length
        );
    }

    slot->native_frame.header.id =
        frame->identifier;

    slot->native_frame.header.dlc =
        frame->data_length;

    slot->native_frame.header.ide =
        frame->extended;

    slot->native_frame.header.rtr =
        frame->remote;

    slot->native_frame.header.fdf =
        false;

    slot->native_frame.header.brs =
        false;

    slot->native_frame.buffer =
        slot->data;

    slot->native_frame.buffer_len =
        frame->remote
            ? 0U
            : frame->data_length;

    const esp_err_t result =
        twai_node_transmit(
            s_node,
            &slot->native_frame,
            0
        );

    if (result != ESP_OK) {
        /*
         * No completion callback will be generated because the frame
         * was not accepted by the hardware driver.
         */
        if (transmission_id != NULL) {
            *transmission_id = 0U;
        }

        slot->confirmation_requested = false;
        slot->transmission_id = 0U;
        slot->transmission_context = NULL;

        can_twai_driver_release_tx_slot(
            slot
        );

        return result;
    }

    /*
     * Do not read slot fields here. A fast TX completion callback may
     * already have released and cleared the slot.
     */
    return ESP_OK;
}

esp_err_t can_twai_driver_transmit(
    const can_twai_frame_t *frame,
    uint32_t timeout_ms
)
{
    return can_twai_driver_transmit_internal(
        frame,
        NULL,
        false,
        timeout_ms,
        NULL
    );
}

esp_err_t can_twai_driver_transmit_tracked(
    const can_twai_frame_t *frame,
    void *transmission_context,
    uint32_t timeout_ms,
    uint32_t *transmission_id
)
{
    if (transmission_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *transmission_id = 0U;

    portENTER_CRITICAL(
        &s_lock
    );

    const bool callback_configured =
        s_config.tx_confirmation_callback != NULL;

    portEXIT_CRITICAL(
        &s_lock
    );

    if (!callback_configured) {
        return ESP_ERR_INVALID_STATE;
    }

    return can_twai_driver_transmit_internal(
        frame,
        transmission_context,
        true,
        timeout_ms,
        transmission_id
    );
}

esp_err_t can_twai_driver_receive(
    can_twai_frame_t *frame,
    uint32_t timeout_ms
)
{
    if (frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(
        frame,
        0,
        sizeof(*frame)
    );

    portENTER_CRITICAL(
        &s_lock
    );

    const bool started =
        s_info.started;

    portEXIT_CRITICAL(
        &s_lock
    );

    if (!started ||
        (s_rx_queue == NULL)) {

        return ESP_ERR_INVALID_STATE;
    }

    if (xQueueReceive(
            s_rx_queue,
            frame,
            pdMS_TO_TICKS(
                timeout_ms
            )
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t can_twai_driver_set_acceptance_filter(
    const can_twai_acceptance_filter_t *filter
)
{
    if (!can_twai_driver_validate_filter(
            filter
        )) {

        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(
        &s_lock
    );

    const bool initialized =
        s_info.initialized;

    const bool was_started =
        s_info.started;

    portEXIT_CRITICAL(
        &s_lock
    );

    if (!initialized ||
        (s_node == NULL)) {

        return ESP_ERR_INVALID_STATE;
    }

    const can_twai_acceptance_filter_t previous_filter =
        s_config.acceptance_filter;

    if (can_twai_driver_filters_equal(
            &previous_filter,
            filter
        )) {

        return ESP_OK;
    }

    /*
     * Hardware acceptance filters may only be changed while the TWAI
     * node is disabled.
     */
    if (was_started) {
        const esp_err_t stop_result =
            can_twai_driver_stop();

        if (stop_result != ESP_OK) {
            return stop_result;
        }
    }

    esp_err_t result =
        can_twai_driver_apply_acceptance_filter(
            filter
        );

    if (result != ESP_OK) {
        const esp_err_t apply_result =
            result;

        ESP_LOGE(
            TAG,
            "Failed to apply TWAI acceptance filter: %s",
            esp_err_to_name(apply_result)
        );

        result =
            can_twai_driver_apply_acceptance_filter(
                &previous_filter
            );

        if (result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Failed to restore previous TWAI filter: %s",
                esp_err_to_name(result)
            );

            /*
             * The effective hardware filter is now uncertain. Keep
             * the controller stopped.
             */
            return result;
        }

        if (was_started) {
            result =
                can_twai_driver_start();

            if (result != ESP_OK) {
                ESP_LOGE(
                    TAG,
                    "Failed to restart TWAI after filter error: %s",
                    esp_err_to_name(result)
                );

                return result;
            }
        }

        return apply_result;
    }

    if (was_started) {
        result =
            can_twai_driver_start();

        if (result != ESP_OK) {
            const esp_err_t start_result =
                result;

            ESP_LOGE(
                TAG,
                "Failed to restart TWAI with new filter: %s",
                esp_err_to_name(start_result)
            );

            /*
             * The node remains disabled after the failed start, so the
             * previous filter can be restored immediately.
             */
            result =
                can_twai_driver_apply_acceptance_filter(
                    &previous_filter
                );

            if (result != ESP_OK) {
                ESP_LOGE(
                    TAG,
                    "Failed to roll back TWAI filter: %s",
                    esp_err_to_name(result)
                );

                return result;
            }

            result =
                can_twai_driver_start();

            if (result != ESP_OK) {
                ESP_LOGE(
                    TAG,
                    "Failed to restart TWAI after filter rollback: %s",
                    esp_err_to_name(result)
                );

                return result;
            }

            ESP_LOGW(
                TAG,
                "Previous TWAI filter restored"
            );

            return start_result;
        }
    }

    /*
     * Publish the new configuration only after both the hardware
     * filter update and optional controller restart have succeeded.
     */
    s_config.acceptance_filter =
        *filter;

    ESP_LOGI(
        TAG,
        "TWAI acceptance filter changed: "
        "format=%s, ID=0x%08lX, mask=0x%08lX",
        filter->extended
            ? "extended"
            : "standard",
        (unsigned long)filter->identifier,
        (unsigned long)filter->mask
    );

    return ESP_OK;
}

esp_err_t can_twai_driver_recover(void)
{
    portENTER_CRITICAL(
        &s_lock
    );

    const bool can_recover =
        s_info.initialized &&
        s_info.started &&
        (s_info.state ==
         CAN_TWAI_STATE_BUS_OFF);

    portEXIT_CRITICAL(
        &s_lock
    );

    if (!can_recover ||
        (s_node == NULL)) {

        return ESP_ERR_INVALID_STATE;
    }

    /*
     * Set this before calling the driver because a state-change
     * callback may run immediately during recovery.
     */
    portENTER_CRITICAL(
        &s_lock
    );

    s_info.state =
        CAN_TWAI_STATE_RECOVERING;

    portEXIT_CRITICAL(
        &s_lock
    );

    const esp_err_t result =
        twai_node_recover(
            s_node
        );

    if (result != ESP_OK) {
        portENTER_CRITICAL(
            &s_lock
        );

        s_info.state =
            CAN_TWAI_STATE_BUS_OFF;

        portEXIT_CRITICAL(
            &s_lock
        );
    }

    return result;
}

esp_err_t can_twai_driver_get_info(
    can_twai_driver_info_t *info
)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    portENTER_CRITICAL(
        &s_lock
    );

    *info = s_info;

    QueueHandle_t rx_queue =
        s_rx_queue;

    twai_node_handle_t node =
        s_node;

    portEXIT_CRITICAL(
        &s_lock
    );

    if (rx_queue != NULL) {
        info->rx_queue_current =
            (uint32_t)uxQueueMessagesWaiting(
                rx_queue
            );
    }

    if (!info->initialized ||
        (node == NULL)) {

        return ESP_OK;
    }

    twai_node_status_t status = {0};
    twai_node_record_t statistics = {0};

    const esp_err_t result =
        twai_node_get_info(
            node,
            &status,
            &statistics
        );

    if (result != ESP_OK) {
        return result;
    }

    if (info->started &&
        (info->state !=
         CAN_TWAI_STATE_RECOVERING)) {

        info->state =
            can_twai_driver_map_state(
                status.state
            );
    }

    info->transmit_error_count =
        status.tx_error_count;

    info->receive_error_count =
        status.rx_error_count;

    info->bus_error_count =
        statistics.bus_err_num;

    return ESP_OK;
}

bool can_twai_driver_is_initialized(void)
{
    portENTER_CRITICAL(
        &s_lock
    );

    const bool initialized =
        s_info.initialized;

    portEXIT_CRITICAL(
        &s_lock
    );

    return initialized;
}

bool can_twai_driver_is_started(void)
{
    portENTER_CRITICAL(
        &s_lock
    );

    const bool started =
        s_info.started;

    portEXIT_CRITICAL(
        &s_lock
    );

    return started;
}
