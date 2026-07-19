#include "system_model.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_err.h"

static system_model_t s_model;
static SemaphoreHandle_t s_mutex;

static esp_err_t lock_model(void)
{
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t system_model_init(void)
{
    if (s_mutex != NULL) {
        return ESP_OK;
    }

    s_mutex = xSemaphoreCreateMutex();

    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    memset(&s_model, 0, sizeof(s_model));

    return ESP_OK;
}

esp_err_t system_model_set(
    const system_model_t *model
)
{
    if (model == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }

    s_model = *model;

    xSemaphoreGive(s_mutex);

    return ESP_OK;
}

esp_err_t system_model_get_snapshot(
    system_model_t *out_model
)
{
    if (out_model == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_FAIL;
    }

    *out_model = s_model;

    xSemaphoreGive(s_mutex);

    return ESP_OK;
}

esp_err_t system_model_set_storage_ready(bool ready)
{
    esp_err_t err = lock_model();

    if (err != ESP_OK) {
        return err;
    }

    s_model.storage_ready = ready;

    xSemaphoreGive(s_mutex);

    return ESP_OK;
}

esp_err_t system_model_set_update_available(bool available)
{
    esp_err_t err = lock_model();

    if (err != ESP_OK) {
        return err;
    }

    s_model.ota_available = available;

    xSemaphoreGive(s_mutex);

    return ESP_OK;
}