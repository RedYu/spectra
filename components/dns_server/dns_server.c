/*
 * SPDX-FileCopyrightText: 2021-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <strings.h>

#include "esp_log.h"
#include "esp_system.h"
#include "esp_check.h"
#include "esp_netif.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "dns_server.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define DNS_PORT (53)
#define DNS_MAX_LEN (256)

#define QD_TYPE_A (0x0001)
#define ANS_TTL_SEC (300)
#define DNS_OPCODE_MASK    (0x7800U)
#define DNS_RESPONSE_FLAG  (0x8000U)

static const char *TAG = "dns_redirect_server";

// DNS Header Packet
typedef struct __attribute__((__packed__))
{
    uint16_t id;
    uint16_t flags;
    uint16_t qd_count;
    uint16_t an_count;
    uint16_t ns_count;
    uint16_t ar_count;
} dns_header_t;

// DNS Question Packet
typedef struct {
    uint16_t type;
    uint16_t class;
} dns_question_t;

// DNS Answer Packet
typedef struct __attribute__((__packed__))
{
    uint16_t ptr_offset;
    uint16_t type;
    uint16_t class;
    uint32_t ttl;
    uint16_t addr_len;
    uint32_t ip_addr;
} dns_answer_t;

// DNS server handle
struct dns_server_handle {
    atomic_bool started;
    
    TaskHandle_t task;
    SemaphoreHandle_t stopped;

    atomic_int socket_fd;

    size_t num_of_entries;
    dns_entry_pair_t entry[];
};

/*
    Parse the name from the packet from the DNS name format to a regular .-seperated name
    returns the pointer to the next part of the packet
*/
static const uint8_t *parse_dns_name(
    const uint8_t *position,
    const uint8_t *packet_end,
    char *parsed_name,
    size_t parsed_name_size
)
{
    if ((position == NULL) ||
        (packet_end == NULL) ||
        (parsed_name == NULL) ||
        (parsed_name_size == 0U) ||
        (position >= packet_end)) {

        return NULL;
    }

    size_t output_length = 0U;

    while (position < packet_end) {
        const uint8_t label_length =
            *position++;

        if (label_length == 0U) {
            if (output_length == 0U) {
                parsed_name[0] = '\0';
            } else {
                parsed_name[output_length - 1U] = '\0';
            }

            return position;
        }

        /*
         * Compressed names are not supported in questions by this
         * simple DNS server.
         */
        if (((label_length & 0xC0U) != 0U) ||
            (label_length > 63U)) {

            return NULL;
        }

        if ((size_t)(packet_end - position) <
            (size_t)label_length) {

            return NULL;
        }

        if ((output_length +
             (size_t)label_length +
             1U) >= parsed_name_size) {

            return NULL;
        }

        memcpy(
            &parsed_name[output_length],
            position,
            label_length
        );

        output_length += label_length;
        parsed_name[output_length++] = '.';

        position += label_length;
    }

    return NULL;
}

/*
 * Parse a DNS request and prepare an IPv4 response using the
 * configured DNS entries.
 */
static int parse_dns_request(
    const char *request,
    size_t request_length,
    char *reply,
    size_t reply_capacity,
    uint32_t source_address,
    dns_server_handle_t handle
)
{
    if ((request == NULL) ||
        (reply == NULL) ||
        (handle == NULL) ||
        (request_length < sizeof(dns_header_t)) ||
        (request_length > reply_capacity)) {

        return -1;
    }

    /*
    * Prepare the response using the original request as its base.
    */
    memset(
        reply,
        0,
        reply_capacity
    );

    memcpy(
        reply,
        request,
        request_length
    );

    dns_header_t *header =
        (dns_header_t *)reply;

    uint16_t flags =
        ntohs(header->flags);

    /*
     * Ignore packets that are already DNS responses.
     */
    if ((flags & DNS_RESPONSE_FLAG) != 0U) {
        return 0;
    }

    /*
     * Only standard DNS queries are supported.
     */
    if ((flags & DNS_OPCODE_MASK) != 0U) {
        return 0;
    }

    const uint16_t question_count =
        ntohs(header->qd_count);

    /*
    * This implementation supports exactly one DNS question.
    */
    if (question_count != 1U) {
        return 0;
    }

    flags |= DNS_RESPONSE_FLAG;

    header->flags = htons(flags);
    header->an_count = htons(0U);
    header->ns_count = htons(0U);
    header->ar_count = htons(0U);

    const size_t answer_size =
        sizeof(dns_answer_t);

    if ((answer_size > reply_capacity) ||
        (request_length >
        (reply_capacity - answer_size))) {

        return -1;
    }

    const uint8_t *packet_end =
        (const uint8_t *)reply +
        request_length;

    const uint8_t *question_position =
        (const uint8_t *)reply +
        sizeof(dns_header_t);

    char name[128];

    const uint8_t *name_end =
        parse_dns_name(
            question_position,
            packet_end,
            name,
            sizeof(name)
        );

    if (name_end == NULL) {
        return -1;
    }

    if ((size_t)(packet_end - name_end) <
        sizeof(dns_question_t)) {

        return -1;
    }

    dns_question_t question;

    memcpy(
        &question,
        name_end,
        sizeof(question)
    );

    const uint16_t question_type =
        ntohs(question.type);

    const uint16_t question_class =
        ntohs(question.class);

    ESP_LOGI(
        TAG,
        "DNS query: name=%s, type=%" PRIu16,
        name,
        question_type
    );

    /*
     * Only IPv4 Internet-class questions are supported.
     */
    if ((question_type != QD_TYPE_A) ||
        (question_class != 1U)) {

        return (int)request_length;
    }

    esp_ip4_addr_t response_ip = {
        .addr = IPADDR_ANY
    };

    for (size_t index = 0U;
        index < handle->num_of_entries;
        ++index) {

        const bool name_matches =
            (strcmp(
                handle->entry[index].name,
                "*"
            ) == 0) ||
            (strcasecmp(
                handle->entry[index].name,
                name
            ) == 0);

        if (!name_matches) {
            continue;
        }

        const uint32_t source_mask =
            handle->entry[index]
                .source_netmask.addr;

        const uint32_t source_network =
            handle->entry[index]
                .source_network.addr;

        const bool source_matches =
            (source_mask == IPADDR_ANY) ||
            ((source_address & source_mask) ==
            (source_network & source_mask));

        if (!source_matches) {
            continue;
        }

        if (handle->entry[index].if_key != NULL) {
            esp_netif_t *network_interface =
                esp_netif_get_handle_from_ifkey(
                    handle->entry[index].if_key
                );

            if (network_interface == NULL) {
                continue;
            }

            esp_netif_ip_info_t ip_info;

            const esp_err_t result =
                esp_netif_get_ip_info(
                    network_interface,
                    &ip_info
                );

            if (result != ESP_OK) {
                continue;
            }

            response_ip.addr =
                ip_info.ip.addr;

            break;
        }

        if (handle->entry[index].ip.addr !=
            IPADDR_ANY) {

            response_ip.addr =
                handle->entry[index].ip.addr;

            break;
        }
    }

    if (response_ip.addr == IPADDR_ANY) {
        return (int)request_length;
    }

    dns_answer_t answer = {
        .ptr_offset = htons(
            0xC000U |
            sizeof(dns_header_t)
        ),
        .type = htons(QD_TYPE_A),
        .class = htons(1U),
        .ttl = htonl(ANS_TTL_SEC),
        .addr_len = htons(
            sizeof(response_ip.addr)
        ),
        .ip_addr = response_ip.addr,
    };

    memcpy(
        reply + request_length,
        &answer,
        sizeof(answer)
    );

    header->an_count = htons(1U);

    return (int)(
        request_length +
        sizeof(answer)
    );
}

/*
 * Listen for IPv4 DNS requests and respond using the first matching
 * configured rule.
 */
static void dns_server_task(
    void *pv_parameters
)
{
    dns_server_handle_t handle =
        pv_parameters;

    char rx_buffer[DNS_MAX_LEN];
    char reply[DNS_MAX_LEN];

    struct sockaddr_in destination = {
        .sin_family = AF_INET,
        .sin_port = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    const int socket_fd =
        socket(
            AF_INET,
            SOCK_DGRAM,
            IPPROTO_IP
        );

    if (socket_fd < 0) {
        ESP_LOGE(
            TAG,
            "Failed to create DNS socket: errno=%d",
            errno
        );

        atomic_store(
            &handle->socket_fd,
            -1
        );

        (void)xSemaphoreGive(
            handle->stopped
        );

        vTaskDelete(NULL);
        return;
    }

    atomic_store(
        &handle->socket_fd,
        socket_fd
    );

    const int bind_result =
        bind(
            socket_fd,
            (struct sockaddr *)&destination,
            sizeof(destination)
        );

    if (bind_result < 0) {
        ESP_LOGE(
            TAG,
            "Failed to bind DNS socket: errno=%d",
            errno
        );

        const int descriptor =
            atomic_exchange(
                &handle->socket_fd,
                -1
            );

        if (descriptor >= 0) {
            (void)close(
                descriptor
            );
        }

        (void)xSemaphoreGive(
            handle->stopped
        );

        vTaskDelete(NULL);
        return;
    }

    while (atomic_load(
           &handle->started
       )) {
        struct sockaddr_storage source_address;
        socklen_t source_length =
            sizeof(source_address);

        const int received_length =
            recvfrom(
                socket_fd,
                rx_buffer,
                sizeof(rx_buffer),
                0,
                (struct sockaddr *)&source_address,
                &source_length
            );

        if (received_length < 0) {
           if (atomic_load(
                    &handle->started
                )) {
                ESP_LOGE(
                    TAG,
                    "Failed to receive DNS request: errno=%d",
                    errno
                );
            }

            break;
        }

        if ((source_address.ss_family != AF_INET) ||
            (source_length <
            sizeof(struct sockaddr_in))) {

            continue;
        }

        const struct sockaddr_in *ipv4_source =
            (const struct sockaddr_in *)
            &source_address;

        const int reply_length =
            parse_dns_request(
                rx_buffer,
                (size_t)received_length,
                reply,
                sizeof(reply),
                ipv4_source->sin_addr.s_addr,
                handle
            );

        if (reply_length < 0) {
            ESP_LOGW(
                TAG,
                "Invalid DNS request"
            );

            continue;
        }

        if (reply_length == 0) {
            continue;
        }

        const int sent_length =
            sendto(
                socket_fd,
                reply,
                (size_t)reply_length,
                0,
                (struct sockaddr *)&source_address,
                source_length
            );

        if (sent_length < 0) {
            if (atomic_load(
                    &handle->started
                )) {

                ESP_LOGW(
                    TAG,
                    "Failed to send DNS response: errno=%d",
                    errno
                );
            }

            continue;
        }
    }

    const int descriptor =
        atomic_exchange(
            &handle->socket_fd,
            -1
        );

    if (descriptor >= 0) {
        (void)shutdown(
            descriptor,
            SHUT_RDWR
        );

        (void)close(
            descriptor
        );
    }

    (void)xSemaphoreGive(
        handle->stopped
    );

    vTaskDelete(NULL);
}

dns_server_handle_t start_dns_server(
    const dns_server_config_t *config
)
{
    if ((config == NULL) ||
        (config->num_of_entries == 0U) ||
        (config->num_of_entries >
        DNS_SERVER_MAX_ITEMS)) {

        return NULL;
    }

    for (size_t index = 0U;
        index < config->num_of_entries;
        ++index) {

        if (config->item[index].name == NULL) {
            return NULL;
        }
    }

    const size_t entry_count =
        (size_t)config->num_of_entries;

    if (entry_count >
        ((SIZE_MAX - sizeof(struct dns_server_handle)) /
         sizeof(dns_entry_pair_t))) {

        return NULL;
    }

    const size_t allocation_size =
        sizeof(struct dns_server_handle) +
        (entry_count * sizeof(dns_entry_pair_t));

    dns_server_handle_t handle =
        calloc(
            1U,
            allocation_size
        );

    if (handle == NULL) {
        return NULL;
    }

    handle->stopped =
        xSemaphoreCreateBinary();

    if (handle->stopped == NULL) {
        free(handle);
        return NULL;
    }

    atomic_init(
        &handle->started,
        true
    );

    atomic_init(
        &handle->socket_fd,
        -1
    );

    handle->num_of_entries =
        entry_count;

    memcpy(
        handle->entry,
        config->item,
        entry_count * sizeof(dns_entry_pair_t)
    );

    const BaseType_t task_result =
        xTaskCreate(
            dns_server_task,
            "dns_server",
            4096U,
            handle,
            5U,
            &handle->task
        );

    if (task_result != pdPASS) {
        vSemaphoreDelete(
            handle->stopped
        );

        free(handle);

        return NULL;
    }

    return handle;
}

void stop_dns_server(
    dns_server_handle_t handle
)
{
    if (handle == NULL) {
        return;
    }

    atomic_store(
        &handle->started,
        false
    );

    const int socket_fd =
        atomic_load(
            &handle->socket_fd
        );

    if (socket_fd >= 0) {
        (void)shutdown(
            socket_fd,
            SHUT_RDWR
        );
    }

    (void)xSemaphoreTake(
        handle->stopped,
        portMAX_DELAY
    );

    vSemaphoreDelete(
        handle->stopped
    );

    free(handle);
}
