/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Yurii Ridkovets
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "xcp_protocol.h"
#ifdef __cplusplus
extern "C" {
#endif
#define XCP_COMMAND_CONNECT       (0xFFU)
#define XCP_COMMAND_DISCONNECT    (0xFEU)
#define XCP_COMMAND_GET_STATUS    (0xFDU)
#define XCP_COMMAND_SYNCH         (0xFCU)
#define XCP_COMMAND_GET_COMM_MODE_INFO (0xFBU)
#define XCP_COMMAND_GET_ID        (0xFAU)
#define XCP_COMMAND_SET_MTA       (0xF6U)
#define XCP_COMMAND_UPLOAD        (0xF5U)
#define XCP_COMMAND_SHORT_UPLOAD  (0xF4U)
#define XCP_COMMAND_DOWNLOAD      (0xF0U)
#define XCP_COMMAND_DOWNLOAD_NEXT (0xEFU)
#define XCP_COMMAND_GET_SEED      (0xF8U)
#define XCP_COMMAND_UNLOCK        (0xF7U)
#define XCP_COMMAND_SET_CAL_PAGE  (0xEBU)
#define XCP_COMMAND_GET_CAL_PAGE  (0xEAU)
#define XCP_COMMAND_CLEAR_DAQ_LIST       (0xE3U)
#define XCP_COMMAND_SET_DAQ_PTR          (0xE2U)
#define XCP_COMMAND_WRITE_DAQ            (0xE1U)
#define XCP_COMMAND_SET_DAQ_LIST_MODE    (0xE0U)
#define XCP_COMMAND_GET_DAQ_LIST_MODE    (0xDFU)
#define XCP_COMMAND_START_STOP_DAQ_LIST  (0xDEU)
#define XCP_COMMAND_START_STOP_SYNCH     (0xDDU)
#define XCP_COMMAND_FREE_DAQ             (0xD6U)
#define XCP_COMMAND_ALLOC_DAQ            (0xD5U)
#define XCP_COMMAND_ALLOC_ODT            (0xD4U)
#define XCP_COMMAND_ALLOC_ODT_ENTRY      (0xD3U)
#define XCP_COMMAND_PROGRAM_START        (0xD2U)
#define XCP_COMMAND_PROGRAM_CLEAR        (0xD1U)
#define XCP_COMMAND_PROGRAM              (0xD0U)
#define XCP_COMMAND_PROGRAM_RESET        (0xCFU)
#define XCP_COMMAND_PROGRAM_PREPARE      (0xCCU)
#define XCP_COMMAND_PROGRAM_FORMAT       (0xCBU)
#define XCP_COMMAND_PROGRAM_NEXT         (0xCAU)
#define XCP_COMMAND_PROGRAM_MAX          (0xC9U)
#define XCP_COMMAND_PROGRAM_VERIFY       (0xC8U)

#define XCP_RESOURCE_CAL_PAG (0x01U)
#define XCP_RESOURCE_DAQ     (0x04U)
#define XCP_RESOURCE_STIM    (0x08U)
#define XCP_RESOURCE_PGM     (0x10U)

typedef struct
{
    uint8_t session_status;
    uint8_t resource_protection_status;
    uint16_t session_configuration_id;

} xcp_get_status_response_t;

typedef struct
{
    uint8_t communication_mode_optional;
    uint8_t maximum_block_size;
    uint8_t minimum_separation_time;
    uint8_t queue_size;
    uint8_t driver_version;

} xcp_get_communication_mode_info_response_t;

typedef struct
{
    uint8_t transfer_mode;
    uint32_t length;

} xcp_get_id_response_t;
esp_err_t xcp_command_encode_connect(
    uint8_t mode, uint8_t *buffer, size_t capacity, size_t *encoded_size
);
esp_err_t xcp_command_encode_disconnect(
    uint8_t *buffer, size_t capacity, size_t *encoded_size
);
esp_err_t xcp_command_encode_get_status(
    uint8_t *buffer, size_t capacity, size_t *encoded_size
);
esp_err_t xcp_command_encode_synch(
    uint8_t *buffer, size_t capacity, size_t *encoded_size
);
esp_err_t xcp_command_encode_get_communication_mode_info(
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_encode_get_id(
    uint8_t identification_type,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_encode_set_mta(
    uint8_t address_extension,
    uint32_t address,
    bool byte_order_big_endian,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_encode_upload(
    uint8_t element_count,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_encode_short_upload(
    uint8_t element_count,
    uint8_t address_extension,
    uint32_t address,
    bool byte_order_big_endian,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_encode_download(
    uint8_t element_count,
    const uint8_t *data,
    size_t data_size,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_encode_download_next(
    uint8_t remaining_element_count,
    const uint8_t *data,
    size_t data_size,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_encode_get_seed(
    uint8_t mode,
    uint8_t resource,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_encode_unlock(
    const uint8_t *key,
    size_t key_size,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_encode_set_calibration_page(
    uint8_t mode,
    uint8_t segment,
    uint8_t page,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_encode_get_calibration_page(
    uint8_t mode,
    uint8_t segment,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_encode_daq_list(
    uint8_t command,
    uint16_t daq_list,
    bool byte_order_big_endian,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_encode_set_daq_pointer(
    uint16_t daq_list,
    uint8_t odt,
    uint8_t entry,
    bool byte_order_big_endian,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_encode_write_daq(
    uint8_t bit_offset,
    uint8_t element_size,
    uint8_t address_extension,
    uint32_t address,
    bool byte_order_big_endian,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_encode_set_daq_list_mode(
    uint8_t mode,
    uint16_t daq_list,
    uint16_t event_channel,
    uint8_t prescaler,
    uint8_t priority,
    bool byte_order_big_endian,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_encode_start_stop_daq_list(
    uint8_t mode,
    uint16_t daq_list,
    bool byte_order_big_endian,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_encode_start_stop_synchronization(
    uint8_t mode,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_encode_allocate_daq(
    uint16_t count,
    bool byte_order_big_endian,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_encode_free_daq(
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_encode_allocate_odt(
    uint16_t daq_list,
    uint8_t count,
    bool byte_order_big_endian,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_encode_allocate_odt_entry(
    uint16_t daq_list,
    uint8_t odt,
    uint8_t count,
    bool byte_order_big_endian,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_encode_program_start(
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_encode_program_clear(
    uint8_t mode,
    uint32_t range,
    bool byte_order_big_endian,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_encode_program_packet(
    uint8_t command,
    uint8_t element_count,
    const uint8_t *data,
    size_t data_size,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_encode_program_reset(
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_encode_program_prepare(
    uint16_t code_size,
    bool byte_order_big_endian,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_encode_program_format(
    uint8_t compression,
    uint8_t encryption,
    uint8_t programming,
    uint8_t access,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_encode_program_verify(
    uint8_t mode,
    uint8_t type,
    uint32_t value,
    bool byte_order_big_endian,
    uint8_t *buffer,
    size_t capacity,
    size_t *encoded_size
);
esp_err_t xcp_command_decode_get_status_response(
    const xcp_packet_t *packet,
    bool byte_order_big_endian,
    xcp_get_status_response_t *response
);
esp_err_t xcp_command_decode_get_communication_mode_info_response(
    const xcp_packet_t *packet,
    xcp_get_communication_mode_info_response_t *response
);
esp_err_t xcp_command_decode_get_id_response(
    const xcp_packet_t *packet,
    bool byte_order_big_endian,
    xcp_get_id_response_t *response
);
#ifdef __cplusplus
}
#endif
