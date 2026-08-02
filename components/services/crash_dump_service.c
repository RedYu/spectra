#include "crash_dump_service.h"

#include <stdint.h>
#include <stdio.h>

#include "esp_core_dump.h"
#include "esp_partition.h"

#include "storage_sd_service.h"

#define CRASH_DUMP_FILE_PATH       ("coredump.elf")
#define CRASH_DUMP_TEMP_FILE_PATH  ("coredump.tmp")
#define CRASH_DUMP_BUFFER_SIZE     (1024U)

esp_err_t crash_dump_service_export_to_sd(void)
{
    esp_err_t result =
        esp_core_dump_image_check();

    if (result == ESP_ERR_NOT_FOUND) {
        return ESP_OK;
    }

    if (result != ESP_OK) {
        return result;
    }

    size_t image_address = 0U;
    size_t image_size = 0U;

    result = esp_core_dump_image_get(
        &image_address,
        &image_size
    );

    if (result != ESP_OK) {
        return result;
    }

    const esp_partition_t *partition =
        esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA,
            ESP_PARTITION_SUBTYPE_DATA_COREDUMP,
            NULL
        );

    if (partition == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    if (image_address < partition->address) {
        return ESP_ERR_INVALID_SIZE;
    }

    const size_t image_offset =
        image_address - partition->address;

    if ((image_size == 0U) ||
        (image_offset >= partition->size) ||
        (image_size >
         (partition->size - image_offset))) {

        return ESP_ERR_INVALID_SIZE;
    }

    FILE *file = NULL;

    result = storage_sd_service_open(
        CRASH_DUMP_TEMP_FILE_PATH,
        "wb",
        &file
    );

    if (result != ESP_OK) {
        return result;
    }

    uint8_t buffer[CRASH_DUMP_BUFFER_SIZE];

    size_t copied = 0U;

    while (copied < image_size) {
        size_t chunk_size =
            image_size - copied;

        if (chunk_size > sizeof(buffer)) {
            chunk_size = sizeof(buffer);
        }

        result = esp_partition_read(
            partition,
            image_offset + copied,
            buffer,
            chunk_size
        );

        if (result != ESP_OK) {
            break;
        }

        size_t written = 0U;

        result = storage_sd_service_write(
            file,
            buffer,
            chunk_size,
            &written
        );

        if ((result != ESP_OK) ||
            (written != chunk_size)) {

            if (result == ESP_OK) {
                result = ESP_FAIL;
            }

            break;
        }

        copied += chunk_size;
    }

    if (result == ESP_OK) {
        result = storage_sd_service_flush(
            file
        );
    }

    const esp_err_t close_result =
        storage_sd_service_close(
            &file
        );

    if ((result == ESP_OK) &&
        (close_result != ESP_OK)) {

        result = close_result;
    }

    /*
     * Remove an incomplete temporary file after any write, flush or
     * close failure. Keep the original core dump in flash.
     */
    if (result != ESP_OK) {
        (void)storage_sd_service_remove(
            CRASH_DUMP_TEMP_FILE_PATH
        );

        return result;
    }

    /*
    * FAT may not allow rename() to replace an existing destination.
    */
    const esp_err_t remove_result =
        storage_sd_service_remove(
            CRASH_DUMP_FILE_PATH
        );

    if ((remove_result != ESP_OK) &&
        (remove_result != ESP_ERR_NOT_FOUND)) {

        (void)storage_sd_service_remove(
            CRASH_DUMP_TEMP_FILE_PATH
        );

        return remove_result;
    }

    /*
    * Publish the completely written dump using its final name.
    */
    result = storage_sd_service_rename(
        CRASH_DUMP_TEMP_FILE_PATH,
        CRASH_DUMP_FILE_PATH
    );

    if (result != ESP_OK) {
        (void)storage_sd_service_remove(
            CRASH_DUMP_TEMP_FILE_PATH
        );

        return result;
    }

    /*
    * Erase the flash dump only after the final SD file has been
    * published successfully.
    */
    return esp_core_dump_image_erase();
}
