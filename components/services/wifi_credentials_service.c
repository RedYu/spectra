#include "wifi_credentials_service.h"

#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_random.h"
#include "nvs.h"

#define WIFI_CREDENTIALS_NAMESPACE          ("wifi_sta")

#define WIFI_CREDENTIALS_KEY_RECORD         ("record")
#define WIFI_CREDENTIALS_RECORD_VERSION     (1U)

#define WIFI_CREDENTIALS_LOCK_TIMEOUT_MS     (1000U)
#define WIFI_CREDENTIAL_ID_RANDOM_SIZE       (8U)

typedef struct
{
    uint32_t version;
    wifi_sta_credentials_t credentials;

} wifi_credentials_record_t;

_Static_assert(
    ((WIFI_CREDENTIAL_ID_RANDOM_SIZE * 2U) + 1U) ==
        SETTINGS_WIFI_CREDENTIAL_ID_LENGTH,
    "Credential identifier size does not match random data size"
);

static SemaphoreHandle_t s_mutex = NULL;
static bool s_initialized = false;

static esp_err_t wifi_credentials_service_lock(void)
{
    if ((s_mutex == NULL) ||
        !s_initialized) {

        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_mutex,
            pdMS_TO_TICKS(
                WIFI_CREDENTIALS_LOCK_TIMEOUT_MS
            )
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static void wifi_credentials_service_unlock(void)
{
    (void)xSemaphoreGive(
        s_mutex
    );
}

static void wifi_credentials_service_clear_memory(
    void *data,
    size_t size
)
{
    if (data == NULL) {
        return;
    }

    volatile uint8_t *bytes =
        (volatile uint8_t *)data;

    while (size > 0U) {
        *bytes = 0U;

        ++bytes;
        --size;
    }
}

static bool wifi_credentials_service_is_hex_id(
    const char *credential_id
)
{
    if (credential_id == NULL) {
        return false;
    }

    const size_t length =
        strlen(
            credential_id
        );

    if (length !=
        (SETTINGS_WIFI_CREDENTIAL_ID_LENGTH - 1U)) {

        return false;
    }

    for (size_t index = 0U;
         index < length;
         ++index) {

        const char character =
            credential_id[index];

        const bool is_digit =
            (character >= '0') &&
            (character <= '9');

        const bool is_lower_hex =
            (character >= 'a') &&
            (character <= 'f');

        const bool is_upper_hex =
            (character >= 'A') &&
            (character <= 'F');

        if (!is_digit &&
            !is_lower_hex &&
            !is_upper_hex) {

            return false;
        }
    }

    return true;
}

static void wifi_credentials_service_generate_id(
    char *credential_id
)
{
    static const char hex[] =
        "0123456789abcdef";

    uint8_t random_data[
        WIFI_CREDENTIAL_ID_RANDOM_SIZE
    ];

    esp_fill_random(
        random_data,
        sizeof(random_data)
    );

    for (size_t index = 0U;
         index < sizeof(random_data);
         ++index) {

        credential_id[index * 2U] =
            hex[
                (random_data[index] >> 4U) &
                0x0FU
            ];

        credential_id[
            (index * 2U) + 1U
        ] =
            hex[
                random_data[index] &
                0x0FU
            ];
    }

    credential_id[
        SETTINGS_WIFI_CREDENTIAL_ID_LENGTH - 1U
    ] = '\0';

    wifi_credentials_service_clear_memory(
        random_data,
        sizeof(random_data)
    );
}

esp_err_t wifi_credentials_service_init(void)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_mutex == NULL) {
        s_mutex =
            xSemaphoreCreateMutex();

        if (s_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    /*
     * Verify that NVS is initialized and that the credentials
     * namespace can be opened.
     */
    nvs_handle_t handle;

    const esp_err_t result =
        nvs_open(
            WIFI_CREDENTIALS_NAMESPACE,
            NVS_READWRITE,
            &handle
        );

    if (result != ESP_OK) {
        vSemaphoreDelete(
            s_mutex
        );

        s_mutex = NULL;

        return result;
    }

    nvs_close(
        handle
    );

    s_initialized = true;

    return ESP_OK;
}

esp_err_t wifi_credentials_service_set(
    const char *ssid,
    const char *password,
    char *out_credential_id,
    size_t credential_id_size
)
{
    if ((ssid == NULL) ||
        (password == NULL) ||
        (out_credential_id == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    if (credential_id_size <
        SETTINGS_WIFI_CREDENTIAL_ID_LENGTH) {

        return ESP_ERR_INVALID_SIZE;
    }

    memset(
        out_credential_id,
        0,
        credential_id_size
    );

    const size_t ssid_length =
        strlen(
            ssid
        );

    const size_t password_length =
        strlen(
            password
        );

    if (ssid_length == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    if ((ssid_length >=
         SETTINGS_WIFI_STA_SSID_MAX_LENGTH) ||
        (password_length >=
         SETTINGS_WIFI_PASSWORD_MAX_LENGTH)) {

        return ESP_ERR_INVALID_SIZE;
    }

    if ((password_length != 0U) &&
        (password_length <
         SETTINGS_WIFI_PASSWORD_MIN_LENGTH)) {

        return ESP_ERR_INVALID_ARG;
    }

    wifi_credentials_record_t record;

    memset(
        &record,
        0,
        sizeof(record)
    );

    record.version =
        WIFI_CREDENTIALS_RECORD_VERSION;

    (void)strlcpy(
        record.credentials.ssid,
        ssid,
        sizeof(record.credentials.ssid)
    );

    (void)strlcpy(
        record.credentials.password,
        password,
        sizeof(record.credentials.password)
    );

    wifi_credentials_service_generate_id(
        record.credentials.credential_id
    );

    const esp_err_t lock_result =
        wifi_credentials_service_lock();

    if (lock_result != ESP_OK) {
        wifi_credentials_service_clear_memory(
            &record,
            sizeof(record)
        );

        return lock_result;
    }

    nvs_handle_t handle;

    esp_err_t result =
        nvs_open(
            WIFI_CREDENTIALS_NAMESPACE,
            NVS_READWRITE,
            &handle
        );

    if (result == ESP_OK) {
        result =
            nvs_set_blob(
                handle,
                WIFI_CREDENTIALS_KEY_RECORD,
                &record,
                sizeof(record)
            );

        if (result == ESP_OK) {
            result =
                nvs_commit(
                    handle
                );
        }

        nvs_close(
            handle
        );
    }

    wifi_credentials_service_unlock();

    if (result == ESP_OK) {
        (void)strlcpy(
            out_credential_id,
            record.credentials.credential_id,
            credential_id_size
        );
    }

    wifi_credentials_service_clear_memory(
        &record,
        sizeof(record)
    );

    return result;
}

esp_err_t wifi_credentials_service_get(
    wifi_sta_credentials_t *credentials
)
{
    if (credentials == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(
        credentials,
        0,
        sizeof(*credentials)
    );

    const esp_err_t lock_result =
        wifi_credentials_service_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    nvs_handle_t handle;

    esp_err_t result =
        nvs_open(
            WIFI_CREDENTIALS_NAMESPACE,
            NVS_READONLY,
            &handle
        );

    wifi_credentials_record_t record;

    memset(
        &record,
        0,
        sizeof(record)
    );

    if (result == ESP_OK) {
        size_t record_size =
            sizeof(record);

        result =
            nvs_get_blob(
                handle,
                WIFI_CREDENTIALS_KEY_RECORD,
                &record,
                &record_size
            );

        nvs_close(
            handle
        );

        if ((result == ESP_OK) &&
            (record_size != sizeof(record))) {

            result = ESP_ERR_INVALID_SIZE;
        }
    }

    wifi_credentials_service_unlock();

    if ((result == ESP_ERR_NVS_NOT_FOUND) ||
        (result == ESP_ERR_NOT_FOUND)) {

        wifi_credentials_service_clear_memory(
            &record,
            sizeof(record)
        );

        return ESP_ERR_NOT_FOUND;
    }

    if (result != ESP_OK) {
        wifi_credentials_service_clear_memory(
            &record,
            sizeof(record)
        );

        return result;
    }

    if (record.version !=
        WIFI_CREDENTIALS_RECORD_VERSION) {

        wifi_credentials_service_clear_memory(
            &record,
            sizeof(record)
        );

        return ESP_ERR_INVALID_VERSION;
    }

    /*
     * Ensure strings are terminated even if the stored record is
     * damaged or was written by an incompatible firmware version.
     */
    record.credentials.ssid[
        SETTINGS_WIFI_STA_SSID_MAX_LENGTH - 1U
    ] = '\0';

    record.credentials.password[
        SETTINGS_WIFI_PASSWORD_MAX_LENGTH - 1U
    ] = '\0';

    record.credentials.credential_id[
        SETTINGS_WIFI_CREDENTIAL_ID_LENGTH - 1U
    ] = '\0';

    const size_t ssid_length =
        strlen(
            record.credentials.ssid
        );

    const size_t password_length =
        strlen(
            record.credentials.password
        );

    if ((ssid_length == 0U) ||
        ((password_length != 0U) &&
         (password_length <
          SETTINGS_WIFI_PASSWORD_MIN_LENGTH)) ||
        !wifi_credentials_service_is_hex_id(
            record.credentials.credential_id
        )) {

        wifi_credentials_service_clear_memory(
            &record,
            sizeof(record)
        );

        return ESP_ERR_INVALID_RESPONSE;
    }

    *credentials =
        record.credentials;

    wifi_credentials_service_clear_memory(
        &record,
        sizeof(record)
    );

    return ESP_OK;
}

esp_err_t wifi_credentials_service_clear(void)
{
    const esp_err_t lock_result =
        wifi_credentials_service_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    nvs_handle_t handle;

    esp_err_t result =
        nvs_open(
            WIFI_CREDENTIALS_NAMESPACE,
            NVS_READWRITE,
            &handle
        );

    if (result == ESP_OK) {
        result =
            nvs_erase_key(
                handle,
                WIFI_CREDENTIALS_KEY_RECORD
            );

        if (result == ESP_ERR_NVS_NOT_FOUND) {
            result = ESP_OK;
        } else if (result == ESP_OK) {
            result =
                nvs_commit(
                    handle
                );
        }

        nvs_close(
            handle
        );
    }

    wifi_credentials_service_unlock();

    return result;
}

esp_err_t wifi_credentials_service_get_configured(
    bool *configured
)
{
    if (configured == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *configured = false;

    wifi_sta_credentials_t credentials;

    const esp_err_t result =
        wifi_credentials_service_get(
            &credentials
        );

    if (result == ESP_ERR_NOT_FOUND) {
        return ESP_OK;
    }

    if (result != ESP_OK) {
        return result;
    }

    *configured = true;

    wifi_credentials_service_clear_memory(
        &credentials,
        sizeof(credentials)
    );

    return ESP_OK;
}
