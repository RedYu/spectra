/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#include "uds_did_catalog.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UDS_DID_CATALOG_DIRECTORY "/config/uds/dids"
#define UDS_DID_CATALOG_FILE_NAME_MAX_LENGTH (96U)
#define UDS_DID_CATALOG_NAME_MAX_LENGTH (64U)
#define UDS_DID_CATALOG_DESCRIPTION_MAX_LENGTH (128U)
#define UDS_DID_CATALOG_DEFINITION_MAX_COUNT (128U)
#define UDS_DID_CATALOG_FILE_MAX_SIZE (64U * 1024U)

typedef struct
{
    char name[UDS_DID_CATALOG_NAME_MAX_LENGTH];
    char description[UDS_DID_CATALOG_DESCRIPTION_MAX_LENGTH];
    uds_did_definition_t *definitions;
    size_t count;
    size_t capacity;

} uds_did_catalog_document_t;

typedef struct
{
    char file_name[UDS_DID_CATALOG_FILE_NAME_MAX_LENGTH];
    char name[UDS_DID_CATALOG_NAME_MAX_LENGTH];
    char description[UDS_DID_CATALOG_DESCRIPTION_MAX_LENGTH];
    size_t definition_count;

} uds_did_catalog_summary_t;

/**
 * @brief Create the directory used by persistent DID catalogs.
 */
esp_err_t uds_did_catalog_service_initialize(void);

/**
 * @brief Decode and validate a versioned DID catalog JSON document.
 *
 * The caller owns the definition array and sets definitions and capacity
 * before calling this function. No definition storage is retained by the
 * service.
 */
esp_err_t uds_did_catalog_service_decode_json(
    const char *json,
    uds_did_catalog_document_t *document
);

/**
 * @brief Encode a validated DID catalog as allocated JSON text.
 *
 * The caller releases the returned text with free().
 */
esp_err_t uds_did_catalog_service_encode_json(
    const uds_did_catalog_document_t *document,
    char **json
);

/**
 * @brief Load a catalog from /config/uds/dids into caller-owned storage.
 */
esp_err_t uds_did_catalog_service_load(
    const char *file_name,
    uds_did_catalog_document_t *document
);

/**
 * @brief Validate and persist a catalog as a separate JSON file.
 */
esp_err_t uds_did_catalog_service_save(
    const char *file_name,
    const uds_did_catalog_document_t *document
);

/**
 * @brief Remove one persistent DID catalog.
 */
esp_err_t uds_did_catalog_service_remove(
    const char *file_name
);

/**
 * @brief List validated persistent DID catalogs with pagination.
 */
esp_err_t uds_did_catalog_service_list(
    size_t offset,
    uds_did_catalog_summary_t *catalogs,
    size_t capacity,
    size_t *count,
    bool *has_more
);

#ifdef __cplusplus
}
#endif
