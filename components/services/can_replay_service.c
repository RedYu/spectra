/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#include "can_replay_service.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"

#include "app_task_priorities.h"
#include "can_logger_binary_format.h"
#include "can_router.h"
#include "storage_sd_service.h"

#define CAN_REPLAY_TASK_STACK_SIZE       (4096U)
#define CAN_REPLAY_TX_TIMEOUT_MS         (20U)
#define CAN_REPLAY_CONFIRM_TIMEOUT_MS    (1000U)
#define CAN_REPLAY_STOP_TIMEOUT_MS       (2000U)
#define CAN_REPLAY_ASC_LINE_SIZE         (384U)
#define CAN_REPLAY_READ_BUFFER_SIZE      (4096U)
#define CAN_REPLAY_EVENT_MASK            \
    (CAN_ROUTER_EVENT_MASK(CAN_EVENT_TX_COMPLETED) | \
     CAN_ROUTER_EVENT_MASK(CAN_EVENT_TX_FAILED) | \
     CAN_ROUTER_EVENT_MASK(CAN_EVENT_TX_ABORTED))

typedef struct
{
    FILE *file;
    bool scl;
    uint64_t file_size;
    uint64_t position;
    uint8_t *buffer;
    size_t buffer_offset;
    size_t buffer_used;
    char line[CAN_REPLAY_ASC_LINE_SIZE];

} can_replay_reader_t;

typedef struct
{
    can_frame_t frame;
    uint64_t time_us;
    can_frame_direction_t direction;
    can_event_type_t event_type;

} can_replay_record_t;

static const char *TAG = "can_replay_service";

static SemaphoreHandle_t s_lock = NULL;
static TaskHandle_t s_task = NULL;
static uint32_t s_subscription = CAN_ROUTER_SUBSCRIPTION_ID_NONE;
static can_replay_info_t s_info;
static atomic_bool s_stop_requested = false;
static atomic_bool s_pause_requested = false;
static atomic_bool s_submission_active = false;
static atomic_uint_fast32_t s_pending_transaction = 0U;
static atomic_int s_confirmation_result = ESP_OK;
static atomic_uint_fast32_t s_early_transaction = 0U;
static atomic_int s_early_confirmation_result = ESP_OK;

static uint16_t can_replay_u16_le(
    const uint8_t *data
)
{
    return (uint16_t)data[0] |
        ((uint16_t)data[1] << 8U);
}

static uint32_t can_replay_u32_le(
    const uint8_t *data
)
{
    return (uint32_t)data[0] |
        ((uint32_t)data[1] << 8U) |
        ((uint32_t)data[2] << 16U) |
        ((uint32_t)data[3] << 24U);
}

static uint64_t can_replay_u64_le(
    const uint8_t *data
)
{
    uint64_t value = 0U;

    for (size_t index = 0U;
         index < sizeof(value);
         ++index) {

        value |= ((uint64_t)data[index]) << (index * 8U);
    }

    return value;
}

static esp_err_t can_replay_read_exact(
    FILE *file,
    void *buffer,
    size_t size,
    bool *end
)
{
    size_t read_size = 0U;
    const esp_err_t result = storage_sd_service_read(
        file,
        buffer,
        size,
        &read_size
    );

    if (end != NULL) {
        *end = (result == ESP_OK) && (read_size == 0U);
    }

    if (result != ESP_OK) {
        return result;
    }

    return (read_size == size)
        ? ESP_OK
        : (read_size == 0U)
            ? ESP_ERR_NOT_FOUND
            : ESP_ERR_INVALID_SIZE;
}

static esp_err_t can_replay_reader_open(
    can_replay_reader_t *reader,
    const char *path
)
{
    if ((reader == NULL) || (path == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(reader, 0, sizeof(*reader));
    struct stat information = {0};
    esp_err_t result = storage_sd_service_stat(
        path,
        &information
    );

    if ((result != ESP_OK) ||
        !S_ISREG(information.st_mode) ||
        (information.st_size <= 0)) {

        return (result != ESP_OK)
            ? result
            : ESP_ERR_INVALID_ARG;
    }

    reader->file_size = (uint64_t)information.st_size;
    reader->scl = false;
    const char *extension = strrchr(path, '.');

    if (extension != NULL) {
        reader->scl = strcasecmp(extension, ".scl") == 0;

        if (!reader->scl &&
            (strcasecmp(extension, ".asc") != 0)) {

            return ESP_ERR_NOT_SUPPORTED;
        }
    } else {
        return ESP_ERR_NOT_SUPPORTED;
    }

    result = storage_sd_service_open(path, "rb", &reader->file);

    if (result != ESP_OK) {
        return result;
    }

    reader->buffer = heap_caps_malloc(
        CAN_REPLAY_READ_BUFFER_SIZE,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );

    if (reader->buffer == NULL) {
        (void)storage_sd_service_close(&reader->file);
        return ESP_ERR_NO_MEM;
    }

    if (reader->scl) {
        uint8_t header[CAN_LOGGER_SCL_FILE_HEADER_SIZE];
        result = can_replay_read_exact(
            reader->file,
            header,
            sizeof(header),
            NULL
        );

        if ((result == ESP_OK) &&
            ((header[0] != CAN_LOGGER_SCL_MAGIC_0) ||
             (header[1] != CAN_LOGGER_SCL_MAGIC_1) ||
             (header[2] != CAN_LOGGER_SCL_MAGIC_2) ||
             (header[3] != CAN_LOGGER_SCL_MAGIC_3) ||
             (can_replay_u16_le(&header[4]) !=
              CAN_LOGGER_SCL_VERSION) ||
             (can_replay_u16_le(&header[6]) !=
              CAN_LOGGER_SCL_FILE_HEADER_SIZE))) {

            result = ESP_ERR_INVALID_RESPONSE;
        }

        reader->position = sizeof(header);
    }

    if (result != ESP_OK) {
        (void)storage_sd_service_close(&reader->file);
        heap_caps_free(reader->buffer);
        reader->buffer = NULL;
    }

    return result;
}

static void can_replay_reader_close(
    can_replay_reader_t *reader
)
{
    if (reader == NULL) {
        return;
    }

    if (reader->file != NULL) {
        (void)storage_sd_service_close(&reader->file);
    }

    heap_caps_free(reader->buffer);
    reader->buffer = NULL;
    reader->buffer_offset = 0U;
    reader->buffer_used = 0U;
}

static esp_err_t can_replay_read_scl(
    can_replay_reader_t *reader,
    can_replay_record_t *record
)
{
    uint8_t header[CAN_LOGGER_SCL_EVENT_HEADER_SIZE];
    esp_err_t result = can_replay_read_exact(
        reader->file,
        header,
        sizeof(header),
        NULL
    );

    if (result != ESP_OK) {
        return result;
    }

    const uint16_t record_size = can_replay_u16_le(header);
    const uint8_t data_length = header[8];

    if ((header[2] != CAN_LOGGER_SCL_RECORD_CAN_EVENT) ||
        (record_size !=
         (CAN_LOGGER_SCL_EVENT_HEADER_SIZE + data_length)) ||
        (data_length > CAN_FRAME_FD_DATA_MAX_LENGTH) ||
        ((uint32_t)header[4] >= (uint32_t)CAN_BUS_COUNT) ||
        ((uint32_t)header[5] >
         (uint32_t)CAN_FRAME_DIRECTION_TX) ||
        ((uint32_t)header[3] >=
         (uint32_t)CAN_EVENT_TYPE_COUNT)) {

        return ESP_ERR_INVALID_RESPONSE;
    }

    memset(record, 0, sizeof(*record));
    record->frame.bus = (can_bus_id_t)header[4];
    record->direction = (can_frame_direction_t)header[5];
    record->event_type = (can_event_type_t)header[3];
    record->frame.dlc = header[7];
    record->frame.data_length = data_length;
    record->frame.flags = can_replay_u32_le(&header[12]);
    record->frame.identifier = can_replay_u32_le(&header[16]);
    record->frame.timestamp_us = can_replay_u64_le(&header[40]);
    record->frame.timestamp_source = CAN_TIMESTAMP_SOURCE_NONE;
    record->time_us = can_replay_u64_le(&header[48]);

    if (data_length != 0U) {
        result = can_replay_read_exact(
            reader->file,
            record->frame.data,
            data_length,
            NULL
        );
    }

    reader->position += record_size;

    if (result == ESP_OK) {
        result = can_frame_validate(&record->frame);
    }

    return result;
}

static esp_err_t can_replay_read_line(
    can_replay_reader_t *reader
)
{
    size_t used = 0U;

    while (used < (sizeof(reader->line) - 1U)) {
        if (reader->buffer_offset >= reader->buffer_used) {
            reader->buffer_offset = 0U;
            reader->buffer_used = 0U;
            const esp_err_t result = storage_sd_service_read(
                reader->file,
                reader->buffer,
                CAN_REPLAY_READ_BUFFER_SIZE,
                &reader->buffer_used
            );

            if (result != ESP_OK) {
                return result;
            }
        }

        if (reader->buffer_used == 0U) {
            if (used == 0U) {
                return ESP_ERR_NOT_FOUND;
            }
            break;
        }

        const char character =
            (char)reader->buffer[reader->buffer_offset++];

        reader->position++;

        if (character == '\n') {
            break;
        }

        if (character != '\r') {
            reader->line[used++] = character;
        }
    }

    reader->line[used] = '\0';
    return (used < (sizeof(reader->line) - 1U))
        ? ESP_OK
        : ESP_ERR_INVALID_SIZE;
}

static bool can_replay_parse_hex_id(
    char *text,
    uint32_t *identifier,
    bool *extended
)
{
    const size_t length = strlen(text);
    *extended = (length != 0U) &&
        ((text[length - 1U] == 'x') ||
         (text[length - 1U] == 'X'));

    if (*extended) {
        text[length - 1U] = '\0';
    }

    char *end = NULL;
    const unsigned long value = strtoul(text, &end, 16);

    return (end != text) && (*end == '\0') &&
        (value <= (*extended ? 0x1FFFFFFFUL : 0x7FFUL)) &&
        ((*identifier = (uint32_t)value), true);
}

static esp_err_t can_replay_read_asc(
    can_replay_reader_t *reader,
    can_replay_record_t *record
)
{
    while (true) {
        esp_err_t result = can_replay_read_line(reader);

        if (result != ESP_OK) {
            return result;
        }

        char *save = NULL;
        char *token = strtok_r(reader->line, " \t", &save);

        if ((token == NULL) || (token[0] == '/')) {
            continue;
        }

        char *end = NULL;
        const double seconds = strtod(token, &end);

        if ((end == token) || (*end != '\0') || (seconds < 0.0)) {
            continue;
        }

        memset(record, 0, sizeof(*record));
        record->event_type = CAN_EVENT_RX;
        record->time_us = (uint64_t)(seconds * 1000000.0 + 0.5);
        token = strtok_r(NULL, " \t", &save);
        const bool fd = (token != NULL) &&
            (strcmp(token, "CANFD") == 0);

        if (fd) {
            token = strtok_r(NULL, " \t", &save);
        }

        if (token == NULL) {
            continue;
        }

        const unsigned long channel = strtoul(token, &end, 10);

        if ((end == token) || (*end != '\0') ||
            ((channel != 1U) && (channel != 2U))) {

            continue;
        }

        record->frame.bus = (channel == 1U)
            ? CAN_BUS_PRIMARY
            : CAN_BUS_SECONDARY;

        if (fd) {
            token = strtok_r(NULL, " \t", &save);
            record->direction =
                (token != NULL) && (strcmp(token, "Tx") == 0)
                    ? CAN_FRAME_DIRECTION_TX
                    : CAN_FRAME_DIRECTION_RX;
            record->event_type =
                (record->direction == CAN_FRAME_DIRECTION_TX)
                    ? CAN_EVENT_TX_COMPLETED
                    : CAN_EVENT_RX;
        }

        char *id_text = strtok_r(NULL, " \t", &save);
        bool extended = false;

        if ((id_text == NULL) ||
            !can_replay_parse_hex_id(
                id_text,
                &record->frame.identifier,
                &extended
            )) {

            continue;
        }

        if (extended) {
            record->frame.flags |= CAN_FRAME_FLAG_EXTENDED_ID;
        }

        if (fd) {
            record->frame.flags |= CAN_FRAME_FLAG_FD;
            const char *brs = strtok_r(NULL, " \t", &save);
            const char *esi = strtok_r(NULL, " \t", &save);
            const char *dlc = strtok_r(NULL, " \t", &save);
            const char *length = strtok_r(NULL, " \t", &save);

            if ((brs == NULL) || (esi == NULL) ||
                (dlc == NULL) || (length == NULL)) {

                continue;
            }

            if (strtoul(brs, NULL, 10) != 0U) {
                record->frame.flags |= CAN_FRAME_FLAG_BRS;
            }

            if (strtoul(esi, NULL, 10) != 0U) {
                record->frame.flags |= CAN_FRAME_FLAG_ESI;
            }

            record->frame.dlc = (uint8_t)strtoul(dlc, NULL, 10);
            record->frame.data_length =
                (uint8_t)strtoul(length, NULL, 10);
        } else {
            token = strtok_r(NULL, " \t", &save);
            record->direction =
                (token != NULL) && (strcmp(token, "Tx") == 0)
                    ? CAN_FRAME_DIRECTION_TX
                    : CAN_FRAME_DIRECTION_RX;
            record->event_type =
                (record->direction == CAN_FRAME_DIRECTION_TX)
                    ? CAN_EVENT_TX_COMPLETED
                    : CAN_EVENT_RX;
            token = strtok_r(NULL, " \t", &save);

            if ((token != NULL) && (token[0] == 'r')) {
                record->frame.flags |= CAN_FRAME_FLAG_REMOTE;
            }

            token = strtok_r(NULL, " \t", &save);

            if (token == NULL) {
                continue;
            }

            record->frame.dlc = (uint8_t)strtoul(token, NULL, 10);
            record->frame.data_length =
                (record->frame.flags & CAN_FRAME_FLAG_REMOTE) != 0U
                    ? 0U
                    : record->frame.dlc;
        }

        if (record->frame.data_length >
            CAN_FRAME_FD_DATA_MAX_LENGTH) {

            return ESP_ERR_INVALID_SIZE;
        }

        for (uint8_t index = 0U;
             index < record->frame.data_length;
             ++index) {

            token = strtok_r(NULL, " \t", &save);

            if (token == NULL) {
                return ESP_ERR_INVALID_RESPONSE;
            }

            record->frame.data[index] =
                (uint8_t)strtoul(token, NULL, 16);
        }

        record->frame.timestamp_source = CAN_TIMESTAMP_SOURCE_NONE;
        return can_frame_validate(&record->frame);
    }
}

static esp_err_t can_replay_reader_next(
    can_replay_reader_t *reader,
    can_replay_record_t *record
)
{
    return reader->scl
        ? can_replay_read_scl(reader, record)
        : can_replay_read_asc(reader, record);
}

static bool can_replay_record_selected(
    const can_replay_config_t *config,
    can_replay_record_t *record
)
{
    if ((!config->primary_enabled &&
         (record->frame.bus == CAN_BUS_PRIMARY)) ||
        (!config->secondary_enabled &&
         (record->frame.bus == CAN_BUS_SECONDARY)) ||
        ((record->event_type != CAN_EVENT_RX) &&
         (record->event_type != CAN_EVENT_TX_COMPLETED)) ||
        (!config->replay_rx_events &&
         (record->event_type == CAN_EVENT_RX)) ||
        (!config->replay_tx_events &&
         (record->event_type == CAN_EVENT_TX_COMPLETED)) ||
        (config->skip_remote_frames &&
         ((record->frame.flags & CAN_FRAME_FLAG_REMOTE) != 0U)) ||
        (config->time_range_enabled &&
         ((record->time_us < config->time_start_us) ||
          (record->time_us > config->time_end_us))) ||
        (config->identifier_filter_enabled &&
         ((record->frame.identifier < config->identifier_min) ||
          (record->frame.identifier > config->identifier_max)))) {

        return false;
    }

    if (!config->preserve_bus) {
        record->frame.bus = config->target_bus;
    }

    if ((record->frame.bus == CAN_BUS_PRIMARY) &&
        ((record->frame.flags & CAN_FRAME_FLAG_FD) != 0U)) {

        return false;
    }

    return true;
}

static void can_replay_router_event(
    const can_event_t *event,
    void *context
)
{
    (void)context;

    const uint32_t pending = (uint32_t)atomic_load(
        &s_pending_transaction
    );

    if ((event == NULL) ||
        ((event->type != CAN_EVENT_TX_COMPLETED) &&
         (event->type != CAN_EVENT_TX_FAILED) &&
         (event->type != CAN_EVENT_TX_ABORTED))) {

        return;
    }

    const esp_err_t result =
        (event->type == CAN_EVENT_TX_COMPLETED)
            ? ESP_OK
            : event->result;

    if ((pending == CAN_TRANSACTION_ID_NONE) &&
        atomic_load(&s_submission_active)) {

        atomic_store(
            &s_early_confirmation_result,
            result
        );
        atomic_store(
            &s_early_transaction,
            event->transaction_id
        );
        return;
    }

    if ((pending == CAN_TRANSACTION_ID_NONE) ||
        (event->transaction_id != pending)) {

        return;
    }

    atomic_store(
        &s_confirmation_result,
        result
    );
    atomic_store(
        &s_pending_transaction,
        CAN_TRANSACTION_ID_NONE
    );

    if (s_task != NULL) {
        xTaskNotifyGive(s_task);
    }
}

static void can_replay_task(
    void *context
)
{
    (void)context;
    can_replay_config_t config;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    config = s_info.config;
    xSemaphoreGive(s_lock);

    esp_err_t result = ESP_OK;
    uint32_t repeat = 0U;

    while (!atomic_load(&s_stop_requested) &&
           ((config.repeat_count == 0U) ||
            (repeat < config.repeat_count))) {

        can_replay_reader_t reader;
        result = can_replay_reader_open(&reader, config.path);

        if (result != ESP_OK) {
            break;
        }

        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_info.file_size = reader.file_size;
        s_info.file_position = reader.position;
        s_info.current_repeat = repeat + 1U;
        xSemaphoreGive(s_lock);

        uint64_t started_at_us =
            (uint64_t)esp_timer_get_time() +
            ((repeat == 0U)
                ? (uint64_t)config.start_delay_ms * 1000ULL
                : 0U);
        uint64_t paused_at_us = 0U;
        uint64_t first_record_time_us = UINT64_MAX;
        can_replay_record_t record;
        bool record_loaded = false;

        while (!atomic_load(&s_stop_requested)) {
            while (atomic_load(&s_pause_requested) &&
                   !atomic_load(&s_stop_requested)) {

                if (paused_at_us == 0U) {
                    paused_at_us = (uint64_t)esp_timer_get_time();
                }

                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100U));
            }

            if (paused_at_us != 0U) {
                started_at_us +=
                    (uint64_t)esp_timer_get_time() - paused_at_us;
                paused_at_us = 0U;
            }

            if (!record_loaded) {
                result = can_replay_reader_next(&reader, &record);

                if (result == ESP_ERR_NOT_FOUND) {
                    result = ESP_OK;
                    break;
                }

                if (result != ESP_OK) {
                    break;
                }

                record_loaded = true;
                xSemaphoreTake(s_lock, portMAX_DELAY);
                s_info.records_read++;
                s_info.file_position = reader.position;
                xSemaphoreGive(s_lock);
            }

            if (!can_replay_record_selected(&config, &record)) {
                xSemaphoreTake(s_lock, portMAX_DELAY);
                s_info.frames_skipped++;
                xSemaphoreGive(s_lock);
                record_loaded = false;
                continue;
            }

            if (first_record_time_us == UINT64_MAX) {
                first_record_time_us = record.time_us;
            }

            const uint64_t relative_us =
                record.time_us >= first_record_time_us
                    ? record.time_us - first_record_time_us
                    : 0U;
            const uint64_t scheduled_us = config.maximum_speed
                ? started_at_us
                : started_at_us +
                    ((relative_us * config.speed_denominator) /
                     config.speed_numerator);
            uint64_t now_us = (uint64_t)esp_timer_get_time();

            while (!config.maximum_speed &&
                   (now_us < scheduled_us) &&
                   !atomic_load(&s_stop_requested) &&
                   !atomic_load(&s_pause_requested)) {

                const uint64_t wait_us = scheduled_us - now_us;
                const TickType_t ticks = pdMS_TO_TICKS(
                    (uint32_t)(wait_us / 1000ULL)
                );
                ulTaskNotifyTake(
                    pdTRUE,
                    ticks > 1U ? ticks - 1U : 1U
                );
                now_us = (uint64_t)esp_timer_get_time();
            }

            if (atomic_load(&s_pause_requested)) {
                continue;
            }

            now_us = (uint64_t)esp_timer_get_time();
            const uint64_t lag_us = !config.maximum_speed &&
                (now_us > scheduled_us)
                ? now_us - scheduled_us
                : 0U;
            const uint64_t maximum_lag_us =
                (uint64_t)config.maximum_lag_ms * 1000ULL;

            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_info.frames_selected++;
            s_info.replay_time_us = relative_us;
            s_info.elapsed_us = now_us - started_at_us;
            s_info.current_lag_us = lag_us;

            if (lag_us > s_info.maximum_lag_us) {
                s_info.maximum_lag_us = lag_us;
            }
            xSemaphoreGive(s_lock);

            if (!config.maximum_speed &&
                (maximum_lag_us != 0U) &&
                (lag_us > maximum_lag_us) &&
                (config.late_policy != CAN_REPLAY_LATE_WAIT)) {

                if (config.late_policy == CAN_REPLAY_LATE_DROP) {
                    xSemaphoreTake(s_lock, portMAX_DELAY);
                    s_info.frames_dropped++;
                    xSemaphoreGive(s_lock);
                    record_loaded = false;
                    continue;
                }

                result = ESP_ERR_TIMEOUT;
                break;
            }

            uint32_t transaction = CAN_TRANSACTION_ID_NONE;
            atomic_store(
                &s_early_transaction,
                CAN_TRANSACTION_ID_NONE
            );
            atomic_store(
                &s_submission_active,
                true
            );
            result = can_router_transmit(
                &record.frame,
                CAN_REPLAY_TX_TIMEOUT_MS,
                &transaction
            );

            if (result != ESP_OK) {
                atomic_store(
                    &s_submission_active,
                    false
                );
                xSemaphoreTake(s_lock, portMAX_DELAY);
                s_info.frames_failed++;
                xSemaphoreGive(s_lock);

                if (config.stop_on_error) {
                    break;
                }
                record_loaded = false;
                continue;
            }

            atomic_store(&s_pending_transaction, transaction);
            atomic_store(
                &s_submission_active,
                false
            );

            if ((uint32_t)atomic_exchange(
                    &s_early_transaction,
                    CAN_TRANSACTION_ID_NONE
                ) == transaction) {

                atomic_store(
                    &s_confirmation_result,
                    atomic_load(&s_early_confirmation_result)
                );
                atomic_store(
                    &s_pending_transaction,
                    CAN_TRANSACTION_ID_NONE
                );
            }

            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_info.frames_submitted++;
            s_info.pending_transaction = transaction;
            xSemaphoreGive(s_lock);

            const uint64_t confirmation_deadline =
                (uint64_t)esp_timer_get_time() +
                ((uint64_t)CAN_REPLAY_CONFIRM_TIMEOUT_MS * 1000ULL);

            while ((atomic_load(&s_pending_transaction) !=
                    CAN_TRANSACTION_ID_NONE) &&
                   !atomic_load(&s_stop_requested) &&
                   ((uint64_t)esp_timer_get_time() <
                    confirmation_deadline)) {

                const uint64_t remaining_us =
                    confirmation_deadline -
                    (uint64_t)esp_timer_get_time();
                ulTaskNotifyTake(
                    pdTRUE,
                    pdMS_TO_TICKS(
                        (uint32_t)(remaining_us / 1000ULL) + 1U
                    )
                );
            }

            if (atomic_load(&s_stop_requested)) {
                atomic_store(
                    &s_pending_transaction,
                    CAN_TRANSACTION_ID_NONE
                );
                xSemaphoreTake(s_lock, portMAX_DELAY);
                s_info.pending_transaction = CAN_TRANSACTION_ID_NONE;
                xSemaphoreGive(s_lock);
                break;
            }

            const esp_err_t confirmation =
                (atomic_load(&s_pending_transaction) ==
                 CAN_TRANSACTION_ID_NONE)
                    ? atomic_load(&s_confirmation_result)
                    : ESP_ERR_TIMEOUT;
            atomic_store(
                &s_pending_transaction,
                CAN_TRANSACTION_ID_NONE
            );

            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_info.pending_transaction = CAN_TRANSACTION_ID_NONE;

            if (confirmation == ESP_OK) {
                s_info.frames_completed++;
            } else {
                s_info.frames_failed++;
                s_info.last_error = confirmation;
            }
            xSemaphoreGive(s_lock);

            if ((confirmation != ESP_OK) && config.stop_on_error) {
                result = confirmation;
                break;
            }

            record_loaded = false;
        }

        can_replay_reader_close(&reader);

        if (result != ESP_OK) {
            break;
        }

        repeat++;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_info.last_error = result;
    s_info.state = atomic_load(&s_stop_requested)
        ? CAN_REPLAY_CANCELLED
        : (result == ESP_OK)
            ? CAN_REPLAY_COMPLETE
            : CAN_REPLAY_ERROR;
    s_task = NULL;
    xSemaphoreGive(s_lock);

    ESP_LOGI(
        TAG,
        "CAN replay finished: path=%s, result=%s, repeats=%lu",
        config.path,
        esp_err_to_name(result),
        (unsigned long)repeat
    );

    vTaskDeleteWithCaps(NULL);
}

esp_err_t can_replay_service_init(void)
{
    if (s_lock != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_lock = xSemaphoreCreateMutex();

    if (s_lock == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const can_router_subscription_t subscription = {
        .bus_mask =
            CAN_ROUTER_BUS_MASK(CAN_BUS_PRIMARY) |
            CAN_ROUTER_BUS_MASK(CAN_BUS_SECONDARY),
        .event_mask = CAN_REPLAY_EVENT_MASK,
        .callback = can_replay_router_event,
        .context = NULL,
    };

    const esp_err_t result = can_router_subscribe(
        &subscription,
        &s_subscription
    );

    if (result != ESP_OK) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return result;
    }

    s_info.state = CAN_REPLAY_IDLE;
    ESP_LOGI(TAG, "CAN replay service initialized");
    return ESP_OK;
}

esp_err_t can_replay_service_start(
    const can_replay_config_t *config
)
{
    if (s_lock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if ((config->path[0] == '\0') ||
        (strncmp(config->path, "/logs/can/", 10U) != 0) ||
        (strstr(config->path, "..") != NULL) ||
        (!config->maximum_speed &&
         ((config->speed_numerator == 0U) ||
          (config->speed_denominator == 0U))) ||
        (!config->primary_enabled && !config->secondary_enabled) ||
        (!config->replay_rx_events && !config->replay_tx_events) ||
        ((uint32_t)config->late_policy >
         (uint32_t)CAN_REPLAY_LATE_STOP) ||
        (!config->preserve_bus &&
         ((uint32_t)config->target_bus >=
          (uint32_t)CAN_BUS_COUNT)) ||
        (config->identifier_filter_enabled &&
         (config->identifier_min > config->identifier_max)) ||
        (config->time_range_enabled &&
         (config->time_start_us > config->time_end_us))) {

        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);

    if (s_task != NULL) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_info, 0, sizeof(s_info));
    s_info.config = *config;
    s_info.state = CAN_REPLAY_RUNNING;
    atomic_store(&s_stop_requested, false);
    atomic_store(&s_pause_requested, false);
    atomic_store(&s_submission_active, false);
    atomic_store(
        &s_pending_transaction,
        CAN_TRANSACTION_ID_NONE
    );
    atomic_store(
        &s_early_transaction,
        CAN_TRANSACTION_ID_NONE
    );

    const BaseType_t task_result = xTaskCreateWithCaps(
        can_replay_task,
        "can_replay",
        CAN_REPLAY_TASK_STACK_SIZE,
        NULL,
        APP_TASK_PRIORITY_CAN_TRANSMIT,
        &s_task,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    xSemaphoreGive(s_lock);

    if (task_result != pdPASS) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_info.state = CAN_REPLAY_ERROR;
        s_info.last_error = ESP_ERR_NO_MEM;
        s_task = NULL;
        xSemaphoreGive(s_lock);
    } else {
        ESP_LOGI(
            TAG,
            "CAN replay started: path=%s",
            config->path
        );
    }

    return (task_result == pdPASS)
        ? ESP_OK
        : ESP_ERR_NO_MEM;
}

esp_err_t can_replay_service_pause(void)
{
    if ((s_lock == NULL) || (s_task == NULL) ||
        atomic_load(&s_pause_requested)) {

        return ESP_ERR_INVALID_STATE;
    }

    atomic_store(&s_pause_requested, true);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_info.state = CAN_REPLAY_PAUSED;
    xSemaphoreGive(s_lock);
    xTaskNotifyGive(s_task);
    return ESP_OK;
}

esp_err_t can_replay_service_resume(void)
{
    if ((s_lock == NULL) || (s_task == NULL) ||
        !atomic_load(&s_pause_requested)) {

        return ESP_ERR_INVALID_STATE;
    }

    atomic_store(&s_pause_requested, false);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_info.state = CAN_REPLAY_RUNNING;
    xSemaphoreGive(s_lock);
    xTaskNotifyGive(s_task);
    return ESP_OK;
}

esp_err_t can_replay_service_stop(void)
{
    if ((s_lock == NULL) || (s_task == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }

    TaskHandle_t task = s_task;
    atomic_store(&s_stop_requested, true);
    xTaskNotifyGive(task);

    const uint64_t deadline = (uint64_t)esp_timer_get_time() +
        ((uint64_t)CAN_REPLAY_STOP_TIMEOUT_MS * 1000ULL);

    while ((s_task != NULL) &&
           ((uint64_t)esp_timer_get_time() < deadline)) {

        vTaskDelay(1U);
    }

    return (s_task == NULL)
        ? ESP_OK
        : ESP_ERR_TIMEOUT;
}

esp_err_t can_replay_service_get_info(
    can_replay_info_t *info
)
{
    if ((s_lock == NULL) || (info == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(100U)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    *info = s_info;
    xSemaphoreGive(s_lock);
    return ESP_OK;
}
