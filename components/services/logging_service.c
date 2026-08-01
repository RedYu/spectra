#include "logging_service.h"

#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "storage_sd_service.h"

#define LOGGING_FILE_PATH                  ("spectra.log")

#define LOGGING_QUEUE_LENGTH               (64U)
#define LOGGING_MESSAGE_MAX_LENGTH         (256U)

#define LOGGING_TASK_NAME                  ("logging_task")
#define LOGGING_TASK_STACK_SIZE            (4096U)
#define LOGGING_TASK_PRIORITY              (4U)

#define LOGGING_FLUSH_INTERVAL_MS          (1000U)

typedef enum
{
    LOGGING_QUEUE_ITEM_MESSAGE,
    LOGGING_QUEUE_ITEM_OPEN,
    LOGGING_QUEUE_ITEM_FLUSH,
    LOGGING_QUEUE_ITEM_CLOSE

} logging_queue_item_type_t;

typedef struct
{
    logging_queue_item_type_t type;

    size_t length;
    char data[LOGGING_MESSAGE_MAX_LENGTH];

} logging_queue_item_t;

static QueueHandle_t s_log_queue = NULL;
static SemaphoreHandle_t s_control_mutex = NULL;
static SemaphoreHandle_t s_command_done = NULL;
static TaskHandle_t s_logging_task = NULL;

static vprintf_like_t s_previous_vprintf = NULL;

static esp_err_t s_command_result = ESP_FAIL;

static atomic_bool s_initialized;
static atomic_bool s_file_open;
static atomic_bool s_accept_file_messages;

static atomic_uint_fast32_t s_active_callbacks;
static atomic_uint_fast32_t s_dropped_messages;
static atomic_uint_fast32_t s_pending_dropped_messages;

static int logging_service_vprintf(
    const char *format,
    va_list arguments
);

static void logging_service_task(
    void *argument
);

static esp_err_t logging_service_control_lock(void);

static void logging_service_control_unlock(void);

static esp_err_t logging_service_send_command(
    logging_queue_item_type_t type
);

static void logging_service_record_dropped_message(void);

static esp_err_t logging_service_write_message(
    FILE *file,
    const logging_queue_item_t *item
);

static esp_err_t logging_service_write_dropped_notice(
    FILE *file
);

static esp_err_t logging_service_close_file(
    FILE **file
);

esp_err_t logging_service_init(void)
{
    if (atomic_load(&s_initialized)) {
        return ESP_ERR_INVALID_STATE;
    }

    s_log_queue = xQueueCreate(
        LOGGING_QUEUE_LENGTH,
        sizeof(logging_queue_item_t)
    );

    if (s_log_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_control_mutex = xSemaphoreCreateMutex();

    if (s_control_mutex == NULL) {
        vQueueDelete(s_log_queue);
        s_log_queue = NULL;

        return ESP_ERR_NO_MEM;
    }

    s_command_done = xSemaphoreCreateBinary();

    if (s_command_done == NULL) {
        vSemaphoreDelete(s_control_mutex);
        vQueueDelete(s_log_queue);

        s_control_mutex = NULL;
        s_log_queue = NULL;

        return ESP_ERR_NO_MEM;
    }

    atomic_store(
        &s_file_open,
        false
    );

    atomic_store(
        &s_accept_file_messages,
        false
    );

    atomic_store(
        &s_active_callbacks,
        0U
    );

    atomic_store(
        &s_dropped_messages,
        0U
    );

    atomic_store(
        &s_pending_dropped_messages,
        0U
    );

    const BaseType_t task_result = xTaskCreate(
        logging_service_task,
        LOGGING_TASK_NAME,
        LOGGING_TASK_STACK_SIZE,
        NULL,
        LOGGING_TASK_PRIORITY,
        &s_logging_task
    );

    if (task_result != pdPASS) {
        vSemaphoreDelete(s_command_done);
        vSemaphoreDelete(s_control_mutex);
        vQueueDelete(s_log_queue);

        s_command_done = NULL;
        s_control_mutex = NULL;
        s_log_queue = NULL;

        return ESP_ERR_NO_MEM;
    }

    /*
     * Preserve the previous output callback so UART logging continues.
     */
    s_previous_vprintf = esp_log_set_vprintf(
        logging_service_vprintf
    );

    atomic_store(
        &s_initialized,
        true
    );

    return ESP_OK;
}

esp_err_t logging_service_enable_file(void)
{
    if (!atomic_load(&s_initialized)) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t lock_result =
        logging_service_control_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    if (atomic_load(&s_file_open)) {
        logging_service_control_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t result =
        logging_service_send_command(
            LOGGING_QUEUE_ITEM_OPEN
        );

    if (result == ESP_OK) {
        atomic_store(
            &s_accept_file_messages,
            true
        );
    }

    logging_service_control_unlock();

    return result;
}

esp_err_t logging_service_disable_file(void)
{
    if (!atomic_load(&s_initialized)) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t lock_result =
        logging_service_control_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    if (!atomic_load(&s_file_open)) {
        atomic_store(
            &s_accept_file_messages,
            false
        );

        logging_service_control_unlock();
        return ESP_OK;
    }

    /*
     * Prevent new callbacks from adding messages to the queue.
     */
    atomic_store(
        &s_accept_file_messages,
        false
    );

    /*
     * A callback may have observed the previous enabled state but may
     * not have submitted its message yet. Wait until all such callbacks
     * have finished before placing CLOSE into the FIFO queue.
     */
    while (atomic_load(&s_active_callbacks) != 0U) {
        vTaskDelay(1U);
    }

    /*
     * CLOSE is placed after every previously accepted message.
     * Consequently, the logging task drains the queue before flushing
     * and closing the file.
     */
    const esp_err_t result =
        logging_service_send_command(
            LOGGING_QUEUE_ITEM_CLOSE
        );

    logging_service_control_unlock();

    return result;
}

esp_err_t logging_service_flush(void)
{
    if (!atomic_load(&s_initialized) ||
        !atomic_load(&s_file_open)) {

        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t lock_result =
        logging_service_control_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    esp_err_t result = ESP_ERR_INVALID_STATE;

    if (atomic_load(&s_file_open)) {
        result = logging_service_send_command(
            LOGGING_QUEUE_ITEM_FLUSH
        );
    }

    logging_service_control_unlock();

    return result;
}

esp_err_t logging_service_get_file_enabled(
    bool *enabled
)
{
    if (enabled == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *enabled = false;

    if (!atomic_load(&s_initialized)) {
        return ESP_ERR_INVALID_STATE;
    }

    *enabled = atomic_load(
        &s_file_open
    );

    return ESP_OK;
}

esp_err_t logging_service_get_dropped_count(
    uint32_t *count
)
{
    if (count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *count = 0U;

    if (!atomic_load(&s_initialized)) {
        return ESP_ERR_INVALID_STATE;
    }

    *count = (uint32_t)atomic_load(
        &s_dropped_messages
    );

    return ESP_OK;
}

esp_err_t logging_service_reset_dropped_count(void)
{
    if (!atomic_load(&s_initialized)) {
        return ESP_ERR_INVALID_STATE;
    }

    atomic_store(
        &s_dropped_messages,
        0U
    );

    atomic_store(
        &s_pending_dropped_messages,
        0U
    );

    return ESP_OK;
}

static esp_err_t logging_service_control_lock(void)
{
    if (s_control_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_control_mutex,
            portMAX_DELAY
        ) != pdTRUE) {

        return ESP_FAIL;
    }

    return ESP_OK;
}

static void logging_service_control_unlock(void)
{
    (void)xSemaphoreGive(
        s_control_mutex
    );
}

static esp_err_t logging_service_send_command(
    logging_queue_item_type_t type
)
{
    if ((s_log_queue == NULL) ||
        (s_command_done == NULL)) {

        return ESP_ERR_INVALID_STATE;
    }

    logging_queue_item_t item = {
        .type = type,
        .length = 0U,
        .data = {0},
    };

    /*
     * Remove an unexpected stale notification before sending a new
     * serialized control command.
     */
    (void)xSemaphoreTake(
        s_command_done,
        0U
    );

    if (xQueueSend(
            s_log_queue,
            &item,
            portMAX_DELAY
        ) != pdTRUE) {

        return ESP_FAIL;
    }

    if (xSemaphoreTake(
            s_command_done,
            portMAX_DELAY
        ) != pdTRUE) {

        return ESP_FAIL;
    }

    return s_command_result;
}

static int logging_service_vprintf(
    const char *format,
    va_list arguments
)
{
    if (format == NULL) {
        return 0;
    }

    va_list uart_arguments;

    va_copy(
        uart_arguments,
        arguments
    );

    int output_length;

    /*
     * Preserve normal ESP-IDF UART output.
     */
    if (s_previous_vprintf != NULL) {
        output_length = s_previous_vprintf(
            format,
            uart_arguments
        );
    } else {
        output_length = vprintf(
            format,
            uart_arguments
        );
    }

    va_end(
        uart_arguments
    );

    if (!atomic_load(&s_initialized)) {
        return output_length;
    }

    /*
     * Do not send messages generated by the logging task back to its
     * own queue. Storage errors may otherwise create an endless loop
     * of failed writes and additional error messages.
     */
    if (xTaskGetCurrentTaskHandle() ==
        s_logging_task) {

        return output_length;
    }

    atomic_fetch_add(
        &s_active_callbacks,
        1U
    );

    /*
     * Recheck the state after incrementing the active callback counter.
     * This synchronizes the callback with logging_service_disable_file().
     */
    if (atomic_load(&s_accept_file_messages) &&
        (s_log_queue != NULL)) {

        logging_queue_item_t item = {
            .type = LOGGING_QUEUE_ITEM_MESSAGE,
            .length = 0U,
            .data = {0},
        };

        va_list queue_arguments;

        va_copy(
            queue_arguments,
            arguments
        );

        const int formatted_length = vsnprintf(
            item.data,
            sizeof(item.data),
            format,
            queue_arguments
        );

        va_end(
            queue_arguments
        );

        if (formatted_length > 0) {
            if ((size_t)formatted_length >=
                sizeof(item.data)) {

                item.length =
                    sizeof(item.data) - 1U;
            } else {
                item.length =
                    (size_t)formatted_length;
            }

            if (xQueueSend(
                    s_log_queue,
                    &item,
                    0U
                ) != pdTRUE) {

                logging_service_record_dropped_message();
            }
        }
    }

    atomic_fetch_sub(
        &s_active_callbacks,
        1U
    );

    return output_length;
}

static void logging_service_task(
    void *argument
)
{
    (void)argument;

    FILE *log_file = NULL;
    logging_queue_item_t item;

    const TickType_t flush_interval_ticks =
        pdMS_TO_TICKS(
            LOGGING_FLUSH_INTERVAL_MS
        );

    TickType_t last_flush_tick =
        xTaskGetTickCount();

    while (true) {
        const BaseType_t received = xQueueReceive(
            s_log_queue,
            &item,
            flush_interval_ticks
        );

        if (received == pdTRUE) {
            switch (item.type) {
                case LOGGING_QUEUE_ITEM_MESSAGE:
                    if (log_file != NULL) {
                        const esp_err_t result =
                            logging_service_write_message(
                                log_file,
                                &item
                            );

                        if (result != ESP_OK) {
                            logging_service_record_dropped_message();
                        }
                    }
                    break;

                case LOGGING_QUEUE_ITEM_OPEN:
                    if (log_file != NULL) {
                        s_command_result =
                            ESP_ERR_INVALID_STATE;
                    } else {
                        s_command_result =
                            storage_sd_service_open(
                                LOGGING_FILE_PATH,
                                "a",
                                &log_file
                            );

                        if (s_command_result == ESP_OK) {
                            if (setvbuf(
                                    log_file,
                                    NULL,
                                    _IOFBF,
                                    1024U
                                ) != 0) {

                                const esp_err_t close_result =
                                    storage_sd_service_close(
                                        &log_file
                                    );

                                if ((close_result != ESP_OK) &&
                                    (log_file != NULL)) {

                                    /*
                                     * Keep file-open state accurate so
                                     * disable_file() can retry closing it.
                                     */
                                    s_command_result =
                                        close_result;
                                } else {
                                    s_command_result =
                                        ESP_FAIL;
                                }
                            } else {
                                last_flush_tick =
                                    xTaskGetTickCount();
                            }
                        }
                    }

                    atomic_store(
                        &s_file_open,
                        log_file != NULL
                    );

                    (void)xSemaphoreGive(
                        s_command_done
                    );
                    break;

                case LOGGING_QUEUE_ITEM_FLUSH:
                    if (log_file == NULL) {
                        s_command_result =
                            ESP_ERR_INVALID_STATE;
                    } else {
                        const esp_err_t notice_result =
                            logging_service_write_dropped_notice(
                                log_file
                            );

                        const esp_err_t flush_result =
                            storage_sd_service_flush(
                                log_file
                            );

                        s_command_result =
                            (notice_result != ESP_OK)
                                ? notice_result
                                : flush_result;

                        last_flush_tick =
                            xTaskGetTickCount();
                    }

                    (void)xSemaphoreGive(
                        s_command_done
                    );
                    break;

                case LOGGING_QUEUE_ITEM_CLOSE:
                    s_command_result =
                        logging_service_close_file(
                            &log_file
                        );

                    atomic_store(
                        &s_file_open,
                        log_file != NULL
                    );

                    last_flush_tick =
                        xTaskGetTickCount();

                    (void)xSemaphoreGive(
                        s_command_done
                    );
                    break;

                default:
                    break;
            }
        }

        /*
         * A queue timeout alone is insufficient because a continuous
         * stream of messages continually restarts the timeout.
         */
        const TickType_t current_tick =
            xTaskGetTickCount();

        if ((log_file != NULL) &&
            ((current_tick - last_flush_tick) >=
             flush_interval_ticks)) {

            (void)logging_service_write_dropped_notice(
                log_file
            );

            (void)storage_sd_service_flush(
                log_file
            );

            last_flush_tick = current_tick;
        }
    }
}

static void logging_service_record_dropped_message(void)
{
    atomic_fetch_add(
        &s_dropped_messages,
        1U
    );

    atomic_fetch_add(
        &s_pending_dropped_messages,
        1U
    );
}

static esp_err_t logging_service_write_message(
    FILE *file,
    const logging_queue_item_t *item
)
{
    if ((file == NULL) ||
        (item == NULL) ||
        (item->length == 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    size_t written = 0U;

    const esp_err_t result =
        storage_sd_service_write(
            file,
            item->data,
            item->length,
            &written
        );

    if (result != ESP_OK) {
        return result;
    }

    if (written != item->length) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t logging_service_write_dropped_notice(
    FILE *file
)
{
    if (file == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint32_t dropped =
        (uint32_t)atomic_exchange(
            &s_pending_dropped_messages,
            0U
        );

    if (dropped == 0U) {
        return ESP_OK;
    }

    char notice[LOGGING_MESSAGE_MAX_LENGTH];

    const int length = snprintf(
        notice,
        sizeof(notice),
        "\n[logging] %lu log message(s) were dropped\n",
        (unsigned long)dropped
    );

    if (length <= 0) {
        /*
         * Preserve the count so a later flush can report it.
         */
        atomic_fetch_add(
            &s_pending_dropped_messages,
            dropped
        );

        return ESP_FAIL;
    }

    const size_t notice_length =
        ((size_t)length < sizeof(notice))
            ? (size_t)length
            : sizeof(notice) - 1U;

    size_t written = 0U;

    const esp_err_t result =
        storage_sd_service_write(
            file,
            notice,
            notice_length,
            &written
        );

    if ((result != ESP_OK) ||
        (written != notice_length)) {

        /*
         * Preserve the count so a later flush can report it.
         */
        atomic_fetch_add(
            &s_pending_dropped_messages,
            dropped
        );

        return (result != ESP_OK)
            ? result
            : ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t logging_service_close_file(
    FILE **file
)
{
    if (file == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (*file == NULL) {
        return ESP_OK;
    }

    esp_err_t result =
        logging_service_write_dropped_notice(
            *file
        );

    const esp_err_t flush_result =
        storage_sd_service_flush(
            *file
        );

    if ((result == ESP_OK) &&
        (flush_result != ESP_OK)) {

        result = flush_result;
    }

    /*
     * Always attempt to close the file, even when flushing failed.
     */
    const esp_err_t close_result =
        storage_sd_service_close(
            file
        );

    if ((result == ESP_OK) &&
        (close_result != ESP_OK)) {

        result = close_result;
    }

    return result;
}
