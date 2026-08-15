#include "can_twai_driver.h"

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

    bool in_use;

} can_twai_tx_slot_t;

static const char *TAG =
    "can_twai_driver";

static twai_node_handle_t s_node = NULL;

static QueueHandle_t s_rx_queue = NULL;
static SemaphoreHandle_t s_tx_slots_available = NULL;

static can_twai_tx_slot_t *s_tx_slots = NULL;
static size_t s_tx_slot_count = 0U;

static can_twai_driver_config_t s_config;
static can_twai_driver_info_t s_info;

static portMUX_TYPE s_lock =
    portMUX_INITIALIZER_UNLOCKED;

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

    return true;
}

static void can_twai_driver_reset_tx_slots(void)
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

    portENTER_CRITICAL(
        &s_lock
    );

    for (size_t index = 0U;
         index < s_tx_slot_count;
         ++index) {

        s_tx_slots[index].in_use = false;
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

    slot->in_use = false;

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

    can_twai_tx_slot_t *completed_slot = NULL;

    BaseType_t higher_priority_task_woken =
        pdFALSE;

    portENTER_CRITICAL_ISR(
        &s_lock
    );

    for (size_t index = 0U;
         index < s_tx_slot_count;
         ++index) {

        if ((&s_tx_slots[index].native_frame ==
            event_data->done_tx_frame) &&
            s_tx_slots[index].in_use) {

            completed_slot =
                &s_tx_slots[index];

            completed_slot->in_use =
                false;

            if (event_data->is_tx_success) {
                ++s_info.transmitted_frames;
            }

            break;
        }
    }

    portEXIT_CRITICAL_ISR(
        &s_lock
    );

    if ((completed_slot != NULL) &&
        (s_tx_slots_available != NULL)) {

        (void)xSemaphoreGiveFromISR(
            s_tx_slots_available,
            &higher_priority_task_woken
        );
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

        portENTER_CRITICAL_ISR(
            &s_lock
        );

        ++s_info.received_frames;

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

    portEXIT_CRITICAL(
        &s_lock
    );

    ESP_LOGI(
        TAG,
        "Primary TWAI initialized: TX=%d, RX=%d, "
        "bitrate=%lu, mode=%u",
        (int)CAN_PRIMARY_PIN_TX,
        (int)CAN_PRIMARY_PIN_RX,
        (unsigned long)config->bitrate,
        (unsigned int)config->mode
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

    can_twai_driver_reset_tx_slots();

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

    can_twai_driver_reset_tx_slots();

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

esp_err_t can_twai_driver_transmit(
    const can_twai_frame_t *frame,
    uint32_t timeout_ms
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
        can_twai_driver_release_tx_slot(
            slot
        );

        return result;
    }

    /*
     * The slot remains allocated until on_tx_done is called.
     */
    return ESP_OK;
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

    twai_node_handle_t node =
        s_node;

    portEXIT_CRITICAL(
        &s_lock
    );

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
