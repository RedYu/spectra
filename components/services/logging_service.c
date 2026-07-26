#include "logging_service.h"

#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "storage_service.h"
#include "storage_sd_service.h"

/*
 * Logging configuration.
 */
#define LOGGING_FILE_PATH             "/spectra.log"

#define LOGGING_QUEUE_LENGTH          64
#define LOGGING_MESSAGE_MAX_LENGTH    256

#define LOGGING_TASK_NAME             "logging_task"
#define LOGGING_TASK_STACK_SIZE       4096
#define LOGGING_TASK_PRIORITY         4

#define LOGGING_FLUSH_INTERVAL_MS     1000

typedef struct
{
    size_t length;
    char data[LOGGING_MESSAGE_MAX_LENGTH];

} logging_message_t;

static QueueHandle_t s_log_queue;
static SemaphoreHandle_t s_file_mutex;
static TaskHandle_t s_logging_task;

static FILE *s_log_file;

static vprintf_like_t s_previous_vprintf;

static bool s_initialized;
static bool s_file_logging_enabled;

static atomic_uint_fast32_t s_dropped_messages;

static int logging_service_vprintf(
    const char *format,
    va_list arguments
);

static void logging_service_task(
    void *argument
);

static esp_err_t logging_service_write_message(
    const logging_message_t *message
);

static esp_err_t logging_service_flush_internal(void);

static void logging_service_write_dropped_notice(void);

esp_err_t logging_service_init(void)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    s_log_queue =
        xQueueCreate(
            LOGGING_QUEUE_LENGTH,
            sizeof(logging_message_t)
        );

    if (s_log_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_file_mutex =
        xSemaphoreCreateMutex();

    if (s_file_mutex == NULL) {
        vQueueDelete(
            s_log_queue
        );

        s_log_queue = NULL;

        return ESP_ERR_NO_MEM;
    }

    atomic_store(
        &s_dropped_messages,
        0
    );

    const BaseType_t task_created =
        xTaskCreate(
            logging_service_task,
            LOGGING_TASK_NAME,
            LOGGING_TASK_STACK_SIZE,
            NULL,
            LOGGING_TASK_PRIORITY,
            &s_logging_task
        );

    if (task_created != pdPASS) {
        vSemaphoreDelete(
            s_file_mutex
        );

        vQueueDelete(
            s_log_queue
        );

        s_file_mutex = NULL;
        s_log_queue = NULL;

        return ESP_ERR_NO_MEM;
    }

    /*
     * Save the previous output function so UART logging can continue.
     */
    s_previous_vprintf =
        esp_log_set_vprintf(
            logging_service_vprintf
        );

    s_initialized = true;

    return ESP_OK;
}

esp_err_t logging_service_enable_file(void)
{
    if (!s_initialized ||
        s_file_mutex == NULL) {

        return ESP_ERR_INVALID_STATE;
    }

    if (!storage_service_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_file_mutex,
            portMAX_DELAY
        ) != pdTRUE) {

        return ESP_FAIL;
    }

    if (s_file_logging_enabled ||
        s_log_file != NULL) {

        xSemaphoreGive(
            s_file_mutex
        );

        return ESP_ERR_INVALID_STATE;
    }

    /*
     * fopen() can access the SD card, so the shared SPI bus must be locked.
     */

    storage_sd_service_open(LOGGING_FILE_PATH, "a", &s_log_file);

    if (s_log_file == NULL) {
        xSemaphoreGive(
            s_file_mutex
        );

        return ESP_FAIL;
    }

    /*
     * Use a larger stdio buffer to reduce the number of SD writes.
     */
    setvbuf(
        s_log_file,
        NULL,
        _IOFBF,
        1024
    );

    s_file_logging_enabled = true;

    xSemaphoreGive(
        s_file_mutex
    );

    return ESP_OK;
}

esp_err_t logging_service_disable_file(void)
{
    if (!s_initialized ||
        s_file_mutex == NULL) {

        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_file_mutex,
            portMAX_DELAY
        ) != pdTRUE) {

        return ESP_FAIL;
    }

    /*
     * Stop accepting new messages for the SD file before closing it.
     * UART output continues normally.
     */
    s_file_logging_enabled = false;

    if (s_log_file != NULL) {
        storage_sd_service_flush(
            s_log_file
        );

        storage_sd_service_close(
            &s_log_file
        );

        s_log_file = NULL;
    }

    xSemaphoreGive(
        s_file_mutex
    );

    return ESP_OK;
}

esp_err_t logging_service_flush(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    return logging_service_flush_internal();
}

bool logging_service_is_file_enabled(void)
{
    return s_file_logging_enabled;
}

uint32_t logging_service_get_dropped_count(void)
{
    return (uint32_t)atomic_load(
        &s_dropped_messages
    );
}

void logging_service_reset_dropped_count(void)
{
    atomic_store(
        &s_dropped_messages,
        0
    );
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
    va_list queue_arguments;

    va_copy(
        uart_arguments,
        arguments
    );

    va_copy(
        queue_arguments,
        arguments
    );

    int output_length = 0;

    /*
     * Preserve the normal ESP-IDF UART output.
     */
    if (s_previous_vprintf != NULL) {
        output_length =
            s_previous_vprintf(
                format,
                uart_arguments
            );
    } else {
        output_length =
            vprintf(
                format,
                uart_arguments
            );
    }

    va_end(
        uart_arguments
    );

    /*
     * Never write to the SD card directly from this callback.
     * Only copy the formatted message into the queue.
     */
    if (s_initialized &&
        s_file_logging_enabled &&
        s_log_queue != NULL) {

        logging_message_t message = {0};

        const int formatted_length =
            vsnprintf(
                message.data,
                sizeof(message.data),
                format,
                queue_arguments
            );

        if (formatted_length > 0) {
            if ((size_t)formatted_length >=
                sizeof(message.data)) {

                message.length =
                    sizeof(message.data) - 1;
            } else {
                message.length =
                    (size_t)formatted_length;
            }

            if (xQueueSend(
                    s_log_queue,
                    &message,
                    0
                ) != pdTRUE) {

                atomic_fetch_add(
                    &s_dropped_messages,
                    1
                );
            }
        }
    }

    va_end(
        queue_arguments
    );

    return output_length;
}

static void logging_service_task(
    void *argument
)
{
    (void)argument;

    logging_message_t message;

    while (true) {
        const BaseType_t received =
            xQueueReceive(
                s_log_queue,
                &message,
                pdMS_TO_TICKS(
                    LOGGING_FLUSH_INTERVAL_MS
                )
            );

        if (received == pdTRUE) {
            logging_service_write_message(
                &message
            );
        } else {
            /*
             * Queue timeout is used as a periodic flush timer.
             */
            logging_service_write_dropped_notice();

            logging_service_flush_internal();
        }
    }
}

static esp_err_t logging_service_write_message(
    const logging_message_t *message
)
{
    if (message == NULL ||
        message->length == 0) {

        return ESP_ERR_INVALID_ARG;
    }

    if (!s_file_logging_enabled ||
        s_log_file == NULL) {

        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_file_mutex,
            portMAX_DELAY
        ) != pdTRUE) {

        return ESP_FAIL;
    }

    esp_err_t result =
        ESP_ERR_INVALID_STATE;

    /*
     * Check the state again after taking the mutex because file logging
     * could have been disabled while the task was waiting.
     */
    if (s_file_logging_enabled &&
        s_log_file != NULL) {

        size_t written = 0;

        storage_sd_service_write(
                s_log_file,
                message->data,
                message->length,
                &written
            );

        result =
            written == message->length
                ? ESP_OK
                : ESP_FAIL;
    }

    xSemaphoreGive(
        s_file_mutex
    );

    return result;
}

static esp_err_t logging_service_flush_internal(void)
{
    if (!s_file_logging_enabled ||
        s_log_file == NULL ||
        s_file_mutex == NULL) {

        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_file_mutex,
            portMAX_DELAY
        ) != pdTRUE) {

        return ESP_FAIL;
    }

    esp_err_t result =
        ESP_ERR_INVALID_STATE;

    if (s_file_logging_enabled &&
        s_log_file != NULL) {

        const int flush_result =
            storage_sd_service_flush(
                s_log_file
            );

        result =
            flush_result == 0
                ? ESP_OK
                : ESP_FAIL;
    }

    xSemaphoreGive(
        s_file_mutex
    );

    return result;
}

static void logging_service_write_dropped_notice(void)
{
    const uint32_t dropped =
        (uint32_t)atomic_exchange(
            &s_dropped_messages,
            0
        );

    if (dropped == 0 ||
        !s_file_logging_enabled) {

        return;
    }

    logging_message_t message = {0};

    const int length =
        snprintf(
            message.data,
            sizeof(message.data),
            "\n[logging] %lu log message(s) were dropped\n",
            (unsigned long)dropped
        );

    if (length <= 0) {
        return;
    }

    message.length =
        (size_t)length < sizeof(message.data)
            ? (size_t)length
            : sizeof(message.data) - 1;

    logging_service_write_message(
        &message
    );
}
