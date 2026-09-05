#include "buzzer_service.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "app_task_priorities.h"
#include "buzzer_driver.h"

#define BUZZER_SERVICE_QUEUE_LENGTH         (8U)
#define BUZZER_SERVICE_TASK_STACK_SIZE      (3072U)
#define BUZZER_SERVICE_TASK_PRIORITY \
    APP_TASK_PRIORITY_BUZZER

#define BUZZER_SERVICE_QUEUE_POLL_MS        (50U)
#define BUZZER_SERVICE_STOP_TIMEOUT_MS      (2000U)

#define BUZZER_NOTIFICATION_STOP            (1UL << 0U)
#define BUZZER_NOTIFICATION_CANCEL          (1UL << 1U)

#define BUZZER_EVENT_STOPPED                (1UL << 0U)

#define ARRAY_SIZE(array) \
    (sizeof(array) / sizeof((array)[0]))

_Static_assert(
    BUZZER_SERVICE_VOLUME_MAX_PERCENT ==
    BUZZER_VOLUME_MAX_PERCENT,
    "Buzzer service and driver volume ranges must match"
);

_Static_assert(
    BUZZER_SERVICE_VOLUME_DEFAULT_PERCENT <=
    BUZZER_SERVICE_VOLUME_MAX_PERCENT,
    "Default buzzer volume exceeds maximum"
);

typedef struct
{
    uint16_t frequency_hz;
    uint16_t duration_ms;
    uint16_t pause_ms;

} buzzer_note_t;

typedef struct
{
    const buzzer_note_t *notes;
    size_t note_count;

} buzzer_pattern_t;

static const char *TAG =
    "buzzer_service";

/*
 * TODO: Serialize start and stop operations if service lifecycle
 * functions become callable concurrently from multiple tasks.
 */

/*
 * TODO: Add synchronous playback-completion support.
 *
 * Implement:
 *
 *     esp_err_t buzzer_service_wait_idle(
 *         uint32_t timeout_ms
 *     );
 *
 * The service should report an idle event after the active pattern and
 * all queued signals have completed. This allows graceful shutdown to
 * play BUZZER_SIGNAL_SHUTDOWN completely before stopping the buzzer
 * service without relying on a fixed delay.
 *
 * Consider adding:
 *
 *     esp_err_t buzzer_service_play_and_wait(
 *         buzzer_signal_t signal,
 *         uint32_t timeout_ms
 *     );
 *
 * Waiting must be implemented using a FreeRTOS event group or task
 * notification and must not block the buzzer playback task itself.
 * Cancellation and service shutdown must unblock waiting callers and
 * return an appropriate error.
 */
 
static QueueHandle_t s_queue = NULL;
static EventGroupHandle_t s_events = NULL;
static TaskHandle_t s_task = NULL;

static atomic_bool s_running =
    ATOMIC_VAR_INIT(false);

static atomic_bool s_enabled =
    ATOMIC_VAR_INIT(true);

static atomic_uint_fast8_t s_volume_percent =
    ATOMIC_VAR_INIT(
        BUZZER_SERVICE_VOLUME_DEFAULT_PERCENT
    );

/*
 * Short UI feedback.
 */
static const buzzer_note_t s_click_pattern[] = {
    {
        .frequency_hz = 2400U,
        .duration_ms = 50U,
        .pause_ms = 0U,
    },
};

/*
 * Successful operation: ascending tones.
 */
static const buzzer_note_t s_success_pattern[] = {
    {
        .frequency_hz = 1800U,
        .duration_ms = 80U,
        .pause_ms = 40U,
    },
    {
        .frequency_hz = 2600U,
        .duration_ms = 120U,
        .pause_ms = 0U,
    },
};

/*
 * Warning: two identical tones.
 */
static const buzzer_note_t s_warning_pattern[] = {
    {
        .frequency_hz = 1400U,
        .duration_ms = 140U,
        .pause_ms = 100U,
    },
    {
        .frequency_hz = 1400U,
        .duration_ms = 140U,
        .pause_ms = 0U,
    },
};

/*
 * Error: three low-frequency tones.
 */
static const buzzer_note_t s_error_pattern[] = {
    {
        .frequency_hz = 700U,
        .duration_ms = 160U,
        .pause_ms = 80U,
    },
    {
        .frequency_hz = 700U,
        .duration_ms = 160U,
        .pause_ms = 80U,
    },
    {
        .frequency_hz = 700U,
        .duration_ms = 220U,
        .pause_ms = 0U,
    },
};

/*
 * Application startup: ascending sequence.
 */
static const buzzer_note_t s_startup_pattern[] = {
    {
        .frequency_hz = 1200U,
        .duration_ms = 70U,
        .pause_ms = 30U,
    },
    {
        .frequency_hz = 1800U,
        .duration_ms = 70U,
        .pause_ms = 30U,
    },
    {
        .frequency_hz = 2600U,
        .duration_ms = 120U,
        .pause_ms = 0U,
    },
};

/*
 * Restart or shutdown: descending sequence.
 */
static const buzzer_note_t s_shutdown_pattern[] = {
    {
        .frequency_hz = 2400U,
        .duration_ms = 80U,
        .pause_ms = 40U,
    },
    {
        .frequency_hz = 1600U,
        .duration_ms = 80U,
        .pause_ms = 40U,
    },
    {
        .frequency_hz = 900U,
        .duration_ms = 150U,
        .pause_ms = 0U,
    },
};

static const buzzer_pattern_t s_patterns[] = {
    [BUZZER_SIGNAL_CLICK] = {
        .notes = s_click_pattern,
        .note_count =
            ARRAY_SIZE(s_click_pattern),
    },

    [BUZZER_SIGNAL_SUCCESS] = {
        .notes = s_success_pattern,
        .note_count =
            ARRAY_SIZE(s_success_pattern),
    },

    [BUZZER_SIGNAL_WARNING] = {
        .notes = s_warning_pattern,
        .note_count =
            ARRAY_SIZE(s_warning_pattern),
    },

    [BUZZER_SIGNAL_ERROR] = {
        .notes = s_error_pattern,
        .note_count =
            ARRAY_SIZE(s_error_pattern),
    },

    [BUZZER_SIGNAL_STARTUP] = {
        .notes = s_startup_pattern,
        .note_count =
            ARRAY_SIZE(s_startup_pattern),
    },

    [BUZZER_SIGNAL_SHUTDOWN] = {
        .notes = s_shutdown_pattern,
        .note_count =
            ARRAY_SIZE(s_shutdown_pattern),
    },
};

_Static_assert(
    ARRAY_SIZE(s_patterns) ==
    BUZZER_SIGNAL_COUNT,
    "Each buzzer signal must have a pattern"
);

static bool buzzer_service_wait(
    uint32_t duration_ms
)
{
    if (duration_ms == 0U) {
        return atomic_load(
            &s_running
        );
    }

    uint32_t notification = 0U;

    (void)xTaskNotifyWait(
        0U,
        UINT32_MAX,
        &notification,
        pdMS_TO_TICKS(
            duration_ms
        )
    );

    if ((notification &
         BUZZER_NOTIFICATION_STOP) != 0U) {

        return false;
    }

    if ((notification &
         BUZZER_NOTIFICATION_CANCEL) != 0U) {

        return false;
    }

    return atomic_load(
        &s_running
    );
}

static bool buzzer_service_play_pattern(
    const buzzer_pattern_t *pattern
)
{
    if ((pattern == NULL) ||
        (pattern->notes == NULL)) {

        return true;
    }

    for (size_t index = 0U;
         index < pattern->note_count;
         ++index) {

        if (!atomic_load(&s_running) ||
            !atomic_load(&s_enabled)) {

            break;
        }

        const buzzer_note_t *note =
            &pattern->notes[index];

        const uint8_t volume_percent =
            (uint8_t)atomic_load(
                &s_volume_percent
            );

        esp_err_t result =
            buzzer_driver_set_volume(
                volume_percent
            );

        if (result != ESP_OK) {
            ESP_LOGW(
                TAG,
                "Failed to apply buzzer volume: %s",
                esp_err_to_name(result)
            );

            continue;
        }

        result =
            buzzer_driver_start_tone(
                note->frequency_hz
            );

        if (result != ESP_OK) {
            ESP_LOGW(
                TAG,
                "Failed to start %u Hz tone: %s",
                (unsigned int)note->frequency_hz,
                esp_err_to_name(result)
            );

            continue;
        }

        const bool continue_playback =
            buzzer_service_wait(
                note->duration_ms
            );

        (void)buzzer_driver_stop();

        if (!continue_playback) {
            return atomic_load(
                &s_running
            );
        }

        if (note->pause_ms > 0U) {
            if (!buzzer_service_wait(
                    note->pause_ms
                )) {

                return atomic_load(
                    &s_running
                );
            }
        }
    }

    (void)buzzer_driver_stop();

    return atomic_load(
        &s_running
    );
}

static void buzzer_service_task(
    void *argument
)
{
    (void)argument;

    while (atomic_load(&s_running)) {
        uint32_t notification = 0U;

        (void)xTaskNotifyWait(
            0U,
            UINT32_MAX,
            &notification,
            0U
        );

        if ((notification &
             BUZZER_NOTIFICATION_STOP) != 0U) {

            break;
        }

        if ((notification &
             BUZZER_NOTIFICATION_CANCEL) != 0U) {

            (void)buzzer_driver_stop();
        }

        buzzer_signal_t signal;

        if (xQueueReceive(
                s_queue,
                &signal,
                pdMS_TO_TICKS(
                    BUZZER_SERVICE_QUEUE_POLL_MS
                )
            ) != pdTRUE) {

            continue;
        }

        if (!atomic_load(&s_enabled)) {
            continue;
        }

        if ((signal < 0) ||
            (signal >= BUZZER_SIGNAL_COUNT)) {

            continue;
        }

        if (!buzzer_service_play_pattern(
                &s_patterns[signal]
            )) {

            break;
        }

        const UBaseType_t watermark =
            uxTaskGetStackHighWaterMark(
                NULL
            );

        if (watermark < 512U) {
            ESP_LOGW(
                TAG,
                "Buzzer task stack is low: %u bytes",
                (unsigned int)watermark
            );
        }
    }

    const esp_err_t result =
        buzzer_driver_deinit();

    if (result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Failed to deinitialize buzzer driver: %s",
            esp_err_to_name(result)
        );
    }

    atomic_store(
        &s_running,
        false
    );

    s_task = NULL;

    if (s_events != NULL) {
        (void)xEventGroupSetBits(
            s_events,
            BUZZER_EVENT_STOPPED
        );
    }

    /*
     * The task stack was allocated through xTaskCreateWithCaps().
     */
    vTaskDeleteWithCaps(
        NULL
    );
}

esp_err_t buzzer_service_start(void)
{
    bool expected = false;

    if (!atomic_compare_exchange_strong(
            &s_running,
            &expected,
            true
        )) {

        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result =
        buzzer_driver_init();

    if (result != ESP_OK) {
        atomic_store(
            &s_running,
            false
        );

        return result;
    }

    s_queue =
        xQueueCreate(
            BUZZER_SERVICE_QUEUE_LENGTH,
            sizeof(buzzer_signal_t)
        );

    if (s_queue == NULL) {
        (void)buzzer_driver_deinit();

        atomic_store(
            &s_running,
            false
        );

        return ESP_ERR_NO_MEM;
    }

    s_events =
        xEventGroupCreate();

    if (s_events == NULL) {
        vQueueDelete(
            s_queue
        );

        s_queue = NULL;

        (void)buzzer_driver_deinit();

        atomic_store(
            &s_running,
            false
        );

        return ESP_ERR_NO_MEM;
    }

    atomic_store(
        &s_enabled,
        true
    );

    const BaseType_t task_result =
        xTaskCreateWithCaps(
            buzzer_service_task,
            "buzzer_service",
            BUZZER_SERVICE_TASK_STACK_SIZE,
            NULL,
            BUZZER_SERVICE_TASK_PRIORITY,
            &s_task,
            MALLOC_CAP_SPIRAM |
            MALLOC_CAP_8BIT
        );

    if (task_result != pdPASS) {
        vEventGroupDelete(
            s_events
        );

        vQueueDelete(
            s_queue
        );

        s_events = NULL;
        s_queue = NULL;
        s_task = NULL;

        (void)buzzer_driver_deinit();

        atomic_store(
            &s_running,
            false
        );

        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(
        TAG,
        "Buzzer service started"
    );

    return ESP_OK;
}

esp_err_t buzzer_service_stop(void)
{
    if (!atomic_exchange(
            &s_running,
            false
        )) {

        return ESP_ERR_INVALID_STATE;
    }

    if (s_queue != NULL) {
        (void)xQueueReset(
            s_queue
        );
    }

    if (s_task != NULL) {
        (void)xTaskNotify(
            s_task,
            BUZZER_NOTIFICATION_STOP,
            eSetBits
        );
    }

    if (s_events == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const EventBits_t bits =
        xEventGroupWaitBits(
            s_events,
            BUZZER_EVENT_STOPPED,
            pdTRUE,
            pdTRUE,
            pdMS_TO_TICKS(
                BUZZER_SERVICE_STOP_TIMEOUT_MS
            )
        );

    if ((bits &
         BUZZER_EVENT_STOPPED) == 0U) {

        ESP_LOGE(
            TAG,
            "Timed out while stopping buzzer service"
        );

        return ESP_ERR_TIMEOUT;
    }

    if (s_events != NULL) {
        vEventGroupDelete(
            s_events
        );

        s_events = NULL;
    }

    if (s_queue != NULL) {
        vQueueDelete(
            s_queue
        );

        s_queue = NULL;
    }

    s_task = NULL;

    ESP_LOGI(
        TAG,
        "Buzzer service stopped"
    );

    return ESP_OK;
}

esp_err_t buzzer_service_play(
    buzzer_signal_t signal
)
{
    if ((signal < 0) ||
        (signal >= BUZZER_SIGNAL_COUNT)) {

        return ESP_ERR_INVALID_ARG;
    }

    if (!atomic_load(&s_running) ||
        (s_queue == NULL)) {

        return ESP_ERR_INVALID_STATE;
    }

    if (!atomic_load(&s_enabled)) {
        return ESP_OK;
    }

    if (xQueueSend(
            s_queue,
            &signal,
            0U
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t buzzer_service_cancel(void)
{
    if (!atomic_load(&s_running) ||
        (s_queue == NULL) ||
        (s_task == NULL)) {

        return ESP_ERR_INVALID_STATE;
    }

    (void)xQueueReset(
        s_queue
    );

    (void)xTaskNotify(
        s_task,
        BUZZER_NOTIFICATION_CANCEL,
        eSetBits
    );

    return ESP_OK;
}

esp_err_t buzzer_service_set_enabled(
    bool enabled
)
{
    if (!atomic_load(&s_running)) {
        return ESP_ERR_INVALID_STATE;
    }

    atomic_store(
        &s_enabled,
        enabled
    );

    if (!enabled) {
        return buzzer_service_cancel();
    }

    return ESP_OK;
}

esp_err_t buzzer_service_get_enabled(
    bool *enabled
)
{
    if (enabled == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *enabled = false;

    if (!atomic_load(&s_running)) {
        return ESP_ERR_INVALID_STATE;
    }

    *enabled =
        atomic_load(
            &s_enabled
        );

    return ESP_OK;
}

bool buzzer_service_is_running(void)
{
    return atomic_load(
        &s_running
    );
}

esp_err_t buzzer_service_set_volume(
    uint8_t volume_percent
)
{
    if (volume_percent >
        BUZZER_SERVICE_VOLUME_MAX_PERCENT) {

        return ESP_ERR_INVALID_ARG;
    }

    if (!atomic_load(&s_running)) {
        return ESP_ERR_INVALID_STATE;
    }

    atomic_store(
        &s_volume_percent,
        volume_percent
    );

    /*
     * Stop current playback immediately when muted. A non-zero volume
     * is applied when the next note starts.
     */
    if (volume_percent == 0U) {
        return buzzer_service_cancel();
    }

    return ESP_OK;
}

esp_err_t buzzer_service_get_volume(
    uint8_t *volume_percent
)
{
    if (volume_percent == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *volume_percent = 0U;

    if (!atomic_load(&s_running)) {
        return ESP_ERR_INVALID_STATE;
    }

    *volume_percent =
        (uint8_t)atomic_load(
            &s_volume_percent
        );

    return ESP_OK;
}
