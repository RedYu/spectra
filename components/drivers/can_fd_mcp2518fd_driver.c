#include "can_fd_mcp2518fd_driver.h"

#include <stdatomic.h>
#include <stddef.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"

#if CAN_FD_MCP2518FD_ENABLE_PROFILING
#include "esp_timer.h"
#endif

#include "board.h"
#include "board_config.h"

#define MCP2518FD_SPI_INSTRUCTION_RESET       (0x0U)
#define MCP2518FD_SPI_INSTRUCTION_WRITE       (0x2U)
#define MCP2518FD_SPI_INSTRUCTION_READ        (0x3U)
#define MCP2518FD_SPI_INSTRUCTION_WRITE_CRC   (0xAU)
#define MCP2518FD_SPI_INSTRUCTION_READ_CRC    (0xBU)

#define MCP2518FD_REGISTER_ADDRESS_MASK       (0x0FFFU)

#define MCP2518FD_REGISTER_CICON              (0x000U)

#define MCP2518FD_REGISTER_CIFIFOCON1         (0x05CU)
#define MCP2518FD_REGISTER_CIFIFOSTA1         (0x060U)
#define MCP2518FD_REGISTER_CIFIFOUA1          (0x064U)

#define MCP2518FD_REGISTER_CIFIFOCON2         (0x068U)
#define MCP2518FD_REGISTER_CIFIFOSTA2         (0x06CU)
#define MCP2518FD_REGISTER_CIFIFOUA2          (0x070U)

#define MCP2518FD_REGISTER_CIFLTCON0          (0x1D0U)
#define MCP2518FD_REGISTER_CIFLTOBJ0          (0x1F0U)
#define MCP2518FD_REGISTER_CIMASK0            (0x1F4U)

#define MCP2518FD_REGISTER_ECCCON             (0xE0CU)

#define MCP2518FD_REGISTER_CIDBTCFG           (0x008U)
#define MCP2518FD_REGISTER_CITDC              (0x00CU)

#define MCP2518FD_DBTCFG_BRP_SHIFT            (24U)
#define MCP2518FD_DBTCFG_TSEG1_SHIFT          (16U)
#define MCP2518FD_DBTCFG_TSEG2_SHIFT          (8U)
#define MCP2518FD_DBTCFG_SJW_SHIFT            (0U)

#define MCP2518FD_DATA_BRP_MAX                (256U)
#define MCP2518FD_DATA_TSEG1_MAX              (32U)
#define MCP2518FD_DATA_TSEG2_MAX              (16U)
#define MCP2518FD_DATA_SJW_MAX                (16U)

#define MCP2518FD_DATA_TQ_MIN                 (3U)
#define MCP2518FD_DATA_TQ_MAX                 (49U)

#define MCP2518FD_DATA_SAMPLE_POINT           (800U)

#define MCP2518FD_TDC_TDCMOD_SHIFT            (16U)
#define MCP2518FD_TDC_TDCO_SHIFT              (8U)

#define MCP2518FD_TDC_TDCMOD_DISABLED         (0U)
#define MCP2518FD_TDC_TDCMOD_MANUAL           (1U)
#define MCP2518FD_TDC_TDCMOD_AUTOMATIC        (2U)

#define MCP2518FD_TDC_TDCO_MAX                (127U)

#define MCP2518FD_DATA_BITRATE_MAX \
    CAN_FD_MCP2518FD_BITRATE_8_MBIT

#define MCP2518FD_MESSAGE_RAM_BASE            (0x400U)
#define MCP2518FD_MESSAGE_RAM_SIZE            (2048U)

#define MCP2518FD_CICON_TXQEN                 (1UL << 20U)
#define MCP2518FD_CICON_STEF                  (1UL << 19U)

#define MCP2518FD_ECCCON_ECCEN                (1UL << 0U)

#define MCP2518FD_FIFO_PLSIZE_SHIFT           (29U)
#define MCP2518FD_FIFO_FSIZE_SHIFT            (24U)
#define MCP2518FD_FIFO_TXPRI_SHIFT            (16U)

#define MCP2518FD_FIFO_TXEN                   (1UL << 7U)
#define MCP2518FD_FIFO_FRESET                 (1UL << 10U)

/*
 * Payload size encoding:
 * 000 = 8 bytes
 * 001 = 12 bytes
 * 010 = 16 bytes
 * 011 = 20 bytes
 * 100 = 24 bytes
 * 101 = 32 bytes
 * 110 = 48 bytes
 * 111 = 64 bytes
 */
#define MCP2518FD_FIFO_PAYLOAD_8_BYTES        (0U)

#define MCP2518FD_FIFO_STATUS_RXOVIF          (1UL << 3U)
#define MCP2518FD_FIFO_STATUS_TFERFFIF        (1UL << 2U)
#define MCP2518FD_FIFO_STATUS_FIFOCI_SHIFT    (8U)
#define MCP2518FD_FIFO_STATUS_FIFOCI_MASK     (0x1FUL << 8U)

#define MCP2518FD_MESSAGE_RAM_OFFSET_MASK     (0x07FFU)

#define MCP2518FD_OBJECT_SID_MASK             (0x000007FFUL)
#define MCP2518FD_OBJECT_EID_MASK             (0x0003FFFFUL)
#define MCP2518FD_OBJECT_EID_SHIFT            (11U)

#define MCP2518FD_OBJECT_DLC_MASK             (0x0FU)
#define MCP2518FD_OBJECT_IDE                  (1UL << 4U)
#define MCP2518FD_OBJECT_RTR                  (1UL << 5U)
#define MCP2518FD_OBJECT_BRS                  (1UL << 6U)
#define MCP2518FD_OBJECT_FDF                  (1UL << 7U)
#define MCP2518FD_OBJECT_ESI                  (1UL << 8U)

#define MCP2518FD_FILTER_ENABLE               (1UL << 7U)
#define MCP2518FD_FILTER_FIFO_MASK            (0x1FU)

#define MCP2518FD_FILTERS_PER_CONTROL_REGISTER  (4U)
#define MCP2518FD_FILTER_CONTROL_REGISTER_COUNT (8U)

#define MCP2518FD_FILTER_CONTROL_STRIDE       (4U)
#define MCP2518FD_FILTER_OBJECT_STRIDE        (8U)
#define MCP2518FD_FILTER_CONTROL_BYTE_MASK    (0xFFUL)

#define MCP2518FD_FILTER_EXIDE                (1UL << 30U)
#define MCP2518FD_FILTER_MIDE                 (1UL << 30U)

#define MCP2518FD_TX_FIFO_NUMBER              (1U)
#define MCP2518FD_RX_FIFO_NUMBER              (2U)

#define MCP2518FD_FIFO_STATUS_TFNRFNIF        (1UL << 0U)
#define MCP2518FD_FIFO_STATUS_TXATIF          (1UL << 4U)
#define MCP2518FD_FIFO_STATUS_TXERR           (1UL << 5U)
#define MCP2518FD_FIFO_STATUS_TXLARB          (1UL << 6U)
#define MCP2518FD_FIFO_STATUS_TXABT           (1UL << 7U)

#define MCP2518FD_FIFO_UINC                   (1UL << 8U)
#define MCP2518FD_FIFO_TXREQ                  (1UL << 9U)

#define MCP2518FD_FIFO_COMMAND_BYTE_OFFSET    (1U)
#define MCP2518FD_FIFO_UINC_BYTE              (1U << 0U)

#define MCP2518FD_FIFO_PAYLOAD_64_BYTES       (7U)

#define MCP2518FD_TX_OBJECT_HEADER_SIZE       (8U)
#define MCP2518FD_TX_OBJECT_MAX_SIZE          \
    (MCP2518FD_TX_OBJECT_HEADER_SIZE +        \
     CAN_FD_MCP2518FD_DATA_MAX_LENGTH)

#define MCP2518FD_RX_OBJECT_HEADER_SIZE       (12U)
#define MCP2518FD_RX_OBJECT_MAX_SIZE          \
    (MCP2518FD_RX_OBJECT_HEADER_SIZE +        \
     CAN_FD_MCP2518FD_DATA_MAX_LENGTH)

#define MCP2518FD_MESSAGE_RAM_TRANSFER_MAX    \
    (MCP2518FD_RX_OBJECT_MAX_SIZE)

#define MCP2518FD_REGISTER_CIINT              (0x01CU)
#define MCP2518FD_REGISTER_CITREC             (0x034U)

#define MCP2518FD_CIINT_CERRIE                (1UL << 29U)
#define MCP2518FD_CIINT_SERRIE                (1UL << 28U)
#define MCP2518FD_CIINT_RXOVIE                (1UL << 27U)
#define MCP2518FD_CIINT_TXATIE                (1UL << 26U)
#define MCP2518FD_CIINT_ECCIE                 (1UL << 24U)
#define MCP2518FD_CIINT_RXIE                  (1UL << 17U)
#define MCP2518FD_CIINT_RXIF                  (1UL << 1U)
#define MCP2518FD_CIINT_CERRIF                (1UL << 13U)
#define MCP2518FD_CIINT_SERRIF                (1UL << 12U)
#define MCP2518FD_CIINT_RXOVIF                (1UL << 11U)
#define MCP2518FD_CIINT_TXATIF                (1UL << 10U)
#define MCP2518FD_CIINT_ECCIF                 (1UL << 8U)

#define MCP2518FD_CIINT_ERROR_FLAGS           \
    (MCP2518FD_CIINT_CERRIF |                 \
     MCP2518FD_CIINT_SERRIF |                 \
     MCP2518FD_CIINT_RXOVIF |                 \
     MCP2518FD_CIINT_TXATIF |                 \
     MCP2518FD_CIINT_ECCIF)

#define MCP2518FD_FIFO_TXATIE                 (1UL << 4U)
#define MCP2518FD_FIFO_RXOVIE                 (1UL << 3U)

#define MCP2518FD_REGISTER_CIBDIAG0          (0x038U)
#define MCP2518FD_REGISTER_CIBDIAG1          (0x03CU)

#define MCP2518FD_BDIAG0_NRERRCNT_MASK       (0x000000FFUL)
#define MCP2518FD_BDIAG0_NTERRCNT_MASK       (0x0000FF00UL)
#define MCP2518FD_BDIAG0_DRERRCNT_MASK       (0x00FF0000UL)
#define MCP2518FD_BDIAG0_DTERRCNT_MASK       (0xFF000000UL)

#define MCP2518FD_BDIAG0_NTERRCNT_SHIFT      (8U)
#define MCP2518FD_BDIAG0_DRERRCNT_SHIFT      (16U)
#define MCP2518FD_BDIAG0_DTERRCNT_SHIFT      (24U)

#define MCP2518FD_BDIAG1_NBIT0ERR            (1UL << 16U)
#define MCP2518FD_BDIAG1_NBIT1ERR            (1UL << 17U)
#define MCP2518FD_BDIAG1_NACKERR             (1UL << 18U)
#define MCP2518FD_BDIAG1_NFORMERR            (1UL << 19U)
#define MCP2518FD_BDIAG1_NSTUFERR            (1UL << 20U)
#define MCP2518FD_BDIAG1_NCRCERR             (1UL << 21U)
#define MCP2518FD_BDIAG1_TXBOERR             (1UL << 23U)

#define MCP2518FD_BDIAG1_DBIT0ERR            (1UL << 24U)
#define MCP2518FD_BDIAG1_DBIT1ERR            (1UL << 25U)
#define MCP2518FD_BDIAG1_DFORMERR            (1UL << 27U)
#define MCP2518FD_BDIAG1_DSTUFERR            (1UL << 28U)
#define MCP2518FD_BDIAG1_DCRCERR             (1UL << 29U)
#define MCP2518FD_BDIAG1_ESI                 (1UL << 30U)
#define MCP2518FD_BDIAG1_DLCMM               (1UL << 31U)

#define MCP2518FD_BDIAG1_BIT_ERROR_MASK      \
    (MCP2518FD_BDIAG1_NBIT0ERR |             \
     MCP2518FD_BDIAG1_NBIT1ERR |             \
     MCP2518FD_BDIAG1_DBIT0ERR |             \
     MCP2518FD_BDIAG1_DBIT1ERR)

#define MCP2518FD_BDIAG1_FORM_ERROR_MASK     \
    (MCP2518FD_BDIAG1_NFORMERR |             \
     MCP2518FD_BDIAG1_DFORMERR)

#define MCP2518FD_BDIAG1_STUFF_ERROR_MASK    \
    (MCP2518FD_BDIAG1_NSTUFERR |             \
     MCP2518FD_BDIAG1_DSTUFERR)

#define MCP2518FD_BDIAG1_CRC_ERROR_MASK      \
    (MCP2518FD_BDIAG1_NCRCERR |              \
     MCP2518FD_BDIAG1_DCRCERR)

#define MCP2518FD_REGISTER_ECCSTAT           (0xE10U)

#define MCP2518FD_ECCSTAT_SECIF              (1UL << 1U)
#define MCP2518FD_ECCSTAT_DEDIF              (1UL << 2U)

#define MCP2518FD_ECCSTAT_ERROR_ADDRESS_SHIFT \
    (16U)

#define MCP2518FD_ECCSTAT_ERROR_ADDRESS_MASK \
    (0x0FFFUL << MCP2518FD_ECCSTAT_ERROR_ADDRESS_SHIFT)

#define MCP2518FD_TREC_REC_MASK               (0xFFUL)
#define MCP2518FD_TREC_TEC_SHIFT              (8U)
#define MCP2518FD_TREC_TEC_MASK               (0xFFUL << 8U)

#define MCP2518FD_TREC_EWARN                  (1UL << 16U)
#define MCP2518FD_TREC_RXWARN                 (1UL << 17U)
#define MCP2518FD_TREC_TXWARN                 (1UL << 18U)
#define MCP2518FD_TREC_RXBP                   (1UL << 19U)
#define MCP2518FD_TREC_TXBP                   (1UL << 20U)
#define MCP2518FD_TREC_TXBO                   (1UL << 21U)

#define MCP2518FD_INTERRUPT_TASK_STACK_SIZE   (3072U)
#define MCP2518FD_INTERRUPT_TASK_PRIORITY     (6U)
#define MCP2518FD_INTERRUPT_STOP_TIMEOUT_MS   (500U)

#define MCP2518FD_REGISTER_OSC                (0x0E00U)
#define MCP2518FD_REGISTER_DEVID              (0x0E14U)
#define MCP2518FD_REGISTER_CINBTCFG           (0x0004U)

#define MCP2518FD_CICON_REQOP_SHIFT           (24U)
#define MCP2518FD_CICON_REQOP_MASK            (0x07UL << 24U)

#define MCP2518FD_CICON_OPMOD_SHIFT           (21U)
#define MCP2518FD_CICON_OPMOD_MASK            (0x07UL << 21U)

#define MCP2518FD_CICON_BRSDIS                (1UL << 12U)
#define MCP2518FD_CICON_ISOCRCEN              (1UL << 5U)

#define MCP2518FD_REGISTER_CITXREQ            (0x030U)

#define MCP2518FD_CICON_ABAT                  (1UL << 27U)

#define MCP2518FD_ABORT_TIMEOUT_MS            (100U)

#define MCP2518FD_REGISTER_CITEFCON           (0x040U)
#define MCP2518FD_REGISTER_CITEFSTA           (0x044U)
#define MCP2518FD_REGISTER_CITEFUA            (0x048U)

#define MCP2518FD_REGISTER_CITSCON            (0x014U)

#define MCP2518FD_TSCON_TBCPRE_MASK           (0x03FFUL)
#define MCP2518FD_TSCON_TBCEN                 (1UL << 16U)
#define MCP2518FD_TSCON_TSEOF                 (1UL << 17U)

#define MCP2518FD_TIMESTAMP_FREQUENCY_HZ      (1000000U)

#define MCP2518FD_FIFO_RXTSEN                 (1UL << 5U)
#define MCP2518FD_FIFO_TFNRFNIE               (1UL << 0U)

#define MCP2518FD_TEF_OBJECT_SIZE             (12U)
#define MCP2518FD_TEF_TIMESTAMP_OFFSET        (8U)

#define MCP2518FD_TEF_FSIZE_SHIFT             (24U)
#define MCP2518FD_TEF_FRESET                  (1UL << 10U)
#define MCP2518FD_TEF_UINC                    (1UL << 8U)

#define MCP2518FD_TEF_TEFTSEN                 (1UL << 5U)
#define MCP2518FD_TEF_TEFOVIE                 (1UL << 3U)
#define MCP2518FD_TEF_TEFNEIE                 (1UL << 0U)

#define MCP2518FD_TEF_STATUS_TEFOVIF          (1UL << 3U)
#define MCP2518FD_TEF_STATUS_TEFNEIF          (1UL << 0U)

#define MCP2518FD_CIINT_TEFIE                 (1UL << 20U)
#define MCP2518FD_CIINT_TEFIF                 (1UL << 4U)

#define MCP2518FD_SEQUENCE_SHIFT              (9U)
#define MCP2518FD_SEQUENCE_MASK               (0x007FFFFFUL)

#define MCP2518FD_OPERATION_MODE_NORMAL_FD          (0U)
#define MCP2518FD_OPERATION_MODE_INTERNAL_LOOPBACK  (2U)
#define MCP2518FD_OPERATION_MODE_LISTEN_ONLY        (3U)
#define MCP2518FD_OPERATION_MODE_CONFIG             (4U)
#define MCP2518FD_OPERATION_MODE_EXTERNAL_LOOPBACK  (5U)
#define MCP2518FD_OPERATION_MODE_NORMAL_20          (6U)
#define MCP2518FD_OPERATION_MODE_RESTRICTED         (7U)

#define MCP2518FD_CICON_RTXAT                 (1UL << 16U)

#define MCP2518FD_FIFO_TXAT_SHIFT             (21U)
#define MCP2518FD_FIFO_TXAT_MASK              (0x03UL << 21U)

#define MCP2518FD_FIFO_TXAT_DISABLED          (0x00UL)
#define MCP2518FD_FIFO_TXAT_THREE_ATTEMPTS    (0x01UL)
#define MCP2518FD_FIFO_TXAT_UNLIMITED         (0x02UL)

#define MCP2518FD_MODE_TIMEOUT_MS             (100U)

#define MCP2518FD_NBTCFG_BRP_SHIFT            (24U)
#define MCP2518FD_NBTCFG_TSEG1_SHIFT          (16U)
#define MCP2518FD_NBTCFG_TSEG2_SHIFT          (8U)
#define MCP2518FD_NBTCFG_SJW_SHIFT            (0U)

#define MCP2518FD_NOMINAL_BRP_MAX             (256U)
#define MCP2518FD_NOMINAL_TSEG1_MAX           (256U)
#define MCP2518FD_NOMINAL_TSEG2_MAX           (128U)

#define MCP2518FD_NOMINAL_TQ_MIN              (8U)
#define MCP2518FD_NOMINAL_TQ_MAX              (385U)

#define MCP2518FD_NOMINAL_SAMPLE_POINT        (800U)

#define MCP2518FD_OSC_PLL_READY               (1UL << 8U)
#define MCP2518FD_OSC_READY                   (1UL << 10U)

#define MCP2518FD_OSC_SCLKDIV                 (1UL << 4U)
#define MCP2518FD_OSC_SCLKDIV_STATUS          (1UL << 12U)

#define MCP2518FD_OSC_CONFIGURATION_MASK      (0x0000007DU)

#define MCP2518FD_OSC_PLL_ENABLE              (1UL << 0U)

#define MCP2518FD_RESET_DELAY_MS              (2U)
#define MCP2518FD_OSC_READY_TIMEOUT_MS        (100U)
#define MCP2518FD_OSC_READY_POLL_MS           (1U)

#define MCP2518FD_SPI_COMMAND_SIZE            (2U)
#define MCP2518FD_REGISTER_SIZE               (4U)
#define MCP2518FD_SPI_REGISTER_TRANSFER_SIZE  \
    (MCP2518FD_SPI_COMMAND_SIZE +             \
     MCP2518FD_REGISTER_SIZE)

#define MCP2518FD_LOCK_TIMEOUT_MS             (100U)

#define MCP2518FD_DELAY_TICKS(milliseconds)              \
    ((pdMS_TO_TICKS(milliseconds) > 0U)                  \
        ? pdMS_TO_TICKS(milliseconds)                    \
        : 1U)

#define MCP2518FD_SUPPORTED_OSCILLATOR_HZ \
    (20000000U)

#define MCP2518FD_CLASSIC_BITRATE_MAX \
    CAN_FD_MCP2518FD_BITRATE_1_MBIT

#define MCP2518FD_MESSAGE_RAM_CLEAR_CHUNK (64U)

#define MCP2518FD_RX_ADDRESS_VERIFY_INTERVAL (32U)

static const char *TAG =
    "can_fd_mcp2518fd";

/*
 * TODO: Extend MCP2518FD hardware-feature support.
 *
 * - CiVEC:
 *   Use the interrupt vector register to identify the active interrupt
 *   source and FIFO number without sequentially polling every status
 *   register. Handle RX FIFO, TEF, exhausted TX attempts, system
 *   errors, CAN bus errors and ECC events.
 *
 * - CiTBC:
 *   Add access to the current 32-bit Time Base Counter value:
 *
 *       esp_err_t can_fd_mcp2518fd_driver_get_timestamp(
 *           uint32_t *timestamp
 *       );
 *
 * - CiRXIF, CiTXIF, CiRXOVIF and CiTXATIF:
 *   Add aggregate FIFO bitmap handling for received frames, completed
 *   transmissions, RX overflows and exhausted transmission attempts.
 *   This will become useful when multiple RX/TX FIFOs are introduced.
 *
 * - CiTXQCON, CiTXQSTA and CiTXQUA:
 *   Consider supporting the dedicated Transmit Queue for CAN-ID-based
 *   ordering, high-priority diagnostic requests and periodic frames.
 *   FIFO1 remains simpler and more predictable for the current design.
 *
 * - FIFO3 through FIFO31:
 *   Add optional allocation and acceptance-filter routing for multiple
 *   receive classes, for example:
 *
 *       FIFO2: all frames
 *       FIFO3: diagnostic identifiers
 *       FIFO4: error-sensitive traffic
 *       FIFO5: high-priority frames
 *
 *   Account for the additional Message RAM usage, especially when
 *   FIFOs use 64-byte CAN FD payload objects.
 *
 * - IOCON:
 *   Explicitly configure INT, RXCAN, TXCAN and GPIO0/GPIO1 behavior,
 *   including the INT open-drain mode and any transceiver standby
 *   control required by the board.
 *
 * - Pipelined SPI DMA:
 *   Message RAM transfers use spi_device_transmit(), which internally
 *   queues the DMA transaction and waits for its completion without
 *   actively polling the SPI peripheral.
 *
 *   Consider explicit spi_device_queue_trans() and
 *   spi_device_get_trans_result() only when independent transactions
 *   can be safely pipelined. RX FIFO reads currently require the
 *   sequence:
 *
 *       read object -> wait for DMA -> UINC -> read next object
 *
 *   Therefore multiple RX objects cannot be queued in advance without
 *   changing the FIFO-draining architecture.
 *
 *   If pipelining is introduced, ensure transaction descriptors and
 *   DMA buffers remain valid until completion and preserve
 *   serialization with the driver mutex.
 *
 * - SPI CRC:
 *   Implement READ_CRC and WRITE_CRC transfers, CRC calculation,
 *   response validation and CRC error reporting. Enable this through
 *   the existing spi_crc_enabled configuration field for improved
 *   robustness in automotive environments.
 *
 * - ECC diagnostics:
 *   Add optional ECC fault injection for self-tests, address-specific
 *   Message RAM recovery and a dedicated ECC event callback.
 */

typedef struct
{
    uint16_t brp;
    uint16_t tseg1;
    uint16_t tseg2;
    uint16_t sjw;

    uint16_t sample_point_permill;
    uint32_t bitrate;

} mcp2518fd_nominal_timing_t;

typedef struct
{
    uint16_t brp;
    uint16_t tseg1;
    uint16_t tseg2;
    uint16_t sjw;

    uint16_t sample_point_permill;
    uint16_t tdc_offset;

    uint32_t bitrate;

} mcp2518fd_data_timing_t;

static can_fd_mcp2518fd_profile_t s_profile;

static spi_device_handle_t s_spi_device = NULL;
static SemaphoreHandle_t s_mutex = NULL;

static bool s_spi_bus_owned = false;
static bool s_initialized = false;
static bool s_started = false;

static can_fd_mcp2518fd_config_t s_config;
static can_fd_mcp2518fd_info_t s_info;

static TaskHandle_t s_interrupt_task = NULL;

static QueueHandle_t s_tx_event_queue = NULL;

static SemaphoreHandle_t s_rx_ready_semaphore = NULL;

static atomic_bool s_interrupt_task_stop =
    ATOMIC_VAR_INIT(false);

static bool s_gpio_handler_registered = false;

static uint32_t s_pending_tx_frames = 0U;
static uint32_t s_next_tx_sequence = 0U;

static bool s_rx_fifo_address_valid = false;

static uint16_t s_rx_fifo_base_address = 0U;
static uint16_t s_rx_fifo_next_address = 0U;
static uint16_t s_rx_fifo_end_address = 0U;

static uint32_t s_rx_fifo_address_uses = 0U;

static esp_err_t mcp2518fd_process_tef_unlocked(void);
static uint32_t mcp2518fd_get_tx_attempt_configuration(void);

static bool mcp2518fd_length_to_dlc(
    uint8_t length,
    uint8_t *dlc
);

static bool mcp2518fd_dlc_to_length(
    uint8_t dlc,
    uint8_t *length
);

static esp_err_t mcp2518fd_read_register_unlocked(
    uint16_t address,
    uint32_t *value
);

static size_t mcp2518fd_rx_object_size(
    const can_fd_mcp2518fd_config_t *config
);

static esp_err_t mcp2518fd_lock(void)
{
    if (s_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_mutex,
            pdMS_TO_TICKS(
                MCP2518FD_LOCK_TIMEOUT_MS
            )
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

static void mcp2518fd_unlock(void)
{
    if (s_mutex != NULL) {
        (void)xSemaphoreGive(s_mutex);
    }
}

#if CAN_FD_MCP2518FD_ENABLE_PROFILING
static uint64_t mcp2518fd_elapsed_us(
    int64_t started_at
)
{
    const int64_t elapsed =
        esp_timer_get_time() -
        started_at;

    return
        (elapsed > 0)
            ? (uint64_t)elapsed
            : 0U;
}
#endif

static void mcp2518fd_invalidate_rx_fifo_address(void)
{
    s_rx_fifo_address_valid = false;
    s_rx_fifo_base_address = 0U;
    s_rx_fifo_next_address = 0U;
    s_rx_fifo_end_address = 0U;
    s_rx_fifo_address_uses = 0U;
}

static esp_err_t mcp2518fd_initialize_rx_fifo_address_unlocked(
    void
)
{
    uint32_t user_address = 0U;

    const esp_err_t result =
        mcp2518fd_read_register_unlocked(
            MCP2518FD_REGISTER_CIFIFOUA2,
            &user_address
        );

    if (result != ESP_OK) {
        mcp2518fd_invalidate_rx_fifo_address();
        return result;
    }

    const size_t object_size =
        mcp2518fd_rx_object_size(
            &s_config
        );

    const size_t fifo_size =
        object_size *
        (size_t)s_config.rx_fifo_depth;

    const uint16_t base_address =
        (uint16_t)(
            MCP2518FD_MESSAGE_RAM_BASE +
            (
                user_address &
                MCP2518FD_MESSAGE_RAM_OFFSET_MASK
            )
        );

    const uint32_t end_address =
        (uint32_t)base_address +
        fifo_size;

    if ((object_size == 0U) ||
        (fifo_size == 0U) ||
        (end_address >
         (MCP2518FD_MESSAGE_RAM_BASE +
          MCP2518FD_MESSAGE_RAM_SIZE))) {

        mcp2518fd_invalidate_rx_fifo_address();

        ESP_LOGE(
            TAG,
            "Invalid RX FIFO address range: "
            "base=0x%03X, size=%u",
            (unsigned int)base_address,
            (unsigned int)fifo_size
        );

        return ESP_ERR_INVALID_RESPONSE;
    }

    s_rx_fifo_base_address =
        base_address;

    s_rx_fifo_next_address =
        base_address;

    s_rx_fifo_end_address =
        (uint16_t)end_address;

    s_rx_fifo_address_uses = 0U;
    s_rx_fifo_address_valid = true;

    ESP_LOGI(
        TAG,
        "RX FIFO address cache initialized: "
        "base=0x%03X, end=0x%03X, object=%u",
        (unsigned int)s_rx_fifo_base_address,
        (unsigned int)s_rx_fifo_end_address,
        (unsigned int)object_size
    );

    return ESP_OK;
}

static esp_err_t mcp2518fd_get_rx_fifo_address_unlocked(
    uint16_t *address
)
{
    if (address == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_rx_fifo_address_valid) {
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * Periodically compare the software address with the controller UA.
     * This protects against unexpected FIFO resets or lost UINC writes.
     */
    if (s_rx_fifo_address_uses >=
        MCP2518FD_RX_ADDRESS_VERIFY_INTERVAL) {

        uint32_t user_address = 0U;

        const esp_err_t result =
            mcp2518fd_read_register_unlocked(
                MCP2518FD_REGISTER_CIFIFOUA2,
                &user_address
            );

        if (result != ESP_OK) {
            return result;
        }

        const uint16_t hardware_address =
            (uint16_t)(
                MCP2518FD_MESSAGE_RAM_BASE +
                (
                    user_address &
                    MCP2518FD_MESSAGE_RAM_OFFSET_MASK
                )
            );

        if (hardware_address !=
            s_rx_fifo_next_address) {

            const size_t object_size =
                mcp2518fd_rx_object_size(
                    &s_config
                );

            const bool address_in_range =
                (hardware_address >=
                s_rx_fifo_base_address) &&
                (hardware_address <
                s_rx_fifo_end_address);

            /*
             * address_in_range protects the subtraction from unsigned
             * underflow through short-circuit evaluation.
             */
            const bool address_aligned =
                address_in_range &&
                (object_size > 0U) &&
                ((((uint32_t)hardware_address -
                s_rx_fifo_base_address) %
                object_size) == 0U);

            if (!address_aligned) {
                ESP_LOGE(
                    TAG,
                    "Invalid RX FIFO hardware address: "
                    "cached=0x%03X, hardware=0x%03X, "
                    "base=0x%03X, end=0x%03X, object=%u",
                    (unsigned int)s_rx_fifo_next_address,
                    (unsigned int)hardware_address,
                    (unsigned int)s_rx_fifo_base_address,
                    (unsigned int)s_rx_fifo_end_address,
                    (unsigned int)object_size
                );

                mcp2518fd_invalidate_rx_fifo_address();

                return ESP_ERR_INVALID_RESPONSE;
            }

            ESP_LOGW(
                TAG,
                "RX FIFO address cache resynchronized: "
                "cached=0x%03X, hardware=0x%03X",
                (unsigned int)s_rx_fifo_next_address,
                (unsigned int)hardware_address
            );

            s_rx_fifo_next_address =
                hardware_address;
        }

        s_rx_fifo_address_uses = 0U;
    }

    *address =
        s_rx_fifo_next_address;

    return ESP_OK;
}

static void mcp2518fd_advance_rx_fifo_address_unlocked(
    void
)
{
    if (!s_rx_fifo_address_valid) {
        return;
    }

    const size_t object_size =
        mcp2518fd_rx_object_size(
            &s_config
        );

    uint32_t next_address =
        (uint32_t)s_rx_fifo_next_address +
        object_size;

    if (next_address >=
        s_rx_fifo_end_address) {

        next_address =
            s_rx_fifo_base_address;
    }

    s_rx_fifo_next_address =
        (uint16_t)next_address;

    ++s_rx_fifo_address_uses;
}

static void mcp2518fd_build_command(
    uint8_t instruction,
    uint16_t address,
    uint8_t command[MCP2518FD_SPI_COMMAND_SIZE]
)
{
    command[0] =
        (uint8_t)(
            (instruction << 4U) |
            ((address >> 8U) & 0x0FU)
        );

    command[1] =
        (uint8_t)(address & 0x00FFU);
}

static esp_err_t mcp2518fd_spi_transmit(
    const void *tx_data,
    void *rx_data,
    size_t length
)
{
    if ((s_spi_device == NULL) ||
        (tx_data == NULL) ||
        (length == 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    spi_transaction_t transaction = {
        .length = length * 8U,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data,
    };

    return spi_device_transmit(
        s_spi_device,
        &transaction
    );
}

static esp_err_t mcp2518fd_reset(void)
{
    uint8_t command[MCP2518FD_SPI_COMMAND_SIZE] = {0};

    mcp2518fd_build_command(
        MCP2518FD_SPI_INSTRUCTION_RESET,
        0U,
        command
    );

    const esp_err_t result =
        mcp2518fd_spi_transmit(
            command,
            NULL,
            sizeof(command)
        );

    if (result == ESP_OK) {
        vTaskDelay(
            MCP2518FD_DELAY_TICKS(
                MCP2518FD_RESET_DELAY_MS
            )
        );
    }

    return result;
}

static esp_err_t mcp2518fd_read_register_unlocked(
    uint16_t address,
    uint32_t *value
)
{
    if ((value == NULL) ||
        ((address & ~MCP2518FD_REGISTER_ADDRESS_MASK) != 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    uint8_t tx_buffer[
        MCP2518FD_SPI_REGISTER_TRANSFER_SIZE
    ] = {0};

    uint8_t rx_buffer[
        MCP2518FD_SPI_REGISTER_TRANSFER_SIZE
    ] = {0};

    mcp2518fd_build_command(
        MCP2518FD_SPI_INSTRUCTION_READ,
        address,
        tx_buffer
    );

    const esp_err_t result =
        mcp2518fd_spi_transmit(
            tx_buffer,
            rx_buffer,
            sizeof(tx_buffer)
        );

    if (result != ESP_OK) {
        return result;
    }

    /*
     * MCP2518FD register data is transferred least-significant byte
     * first after the two-byte SPI instruction.
     */
    *value =
        ((uint32_t)rx_buffer[2]) |
        ((uint32_t)rx_buffer[3] << 8U) |
        ((uint32_t)rx_buffer[4] << 16U) |
        ((uint32_t)rx_buffer[5] << 24U);

    return ESP_OK;
}

static esp_err_t mcp2518fd_write_register_unlocked(
    uint16_t address,
    uint32_t value
)
{
    if ((address & ~MCP2518FD_REGISTER_ADDRESS_MASK) != 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buffer[
        MCP2518FD_SPI_REGISTER_TRANSFER_SIZE
    ] = {0};

    mcp2518fd_build_command(
        MCP2518FD_SPI_INSTRUCTION_WRITE,
        address,
        buffer
    );

    buffer[2] = (uint8_t)(value & 0xFFU);
    buffer[3] = (uint8_t)((value >> 8U) & 0xFFU);
    buffer[4] = (uint8_t)((value >> 16U) & 0xFFU);
    buffer[5] = (uint8_t)((value >> 24U) & 0xFFU);

    return mcp2518fd_spi_transmit(
        buffer,
        NULL,
        sizeof(buffer)
    );
}

static esp_err_t mcp2518fd_request_mode_unlocked(
    uint8_t requested_mode
)
{
    uint32_t control = 0U;

    esp_err_t result =
        mcp2518fd_read_register_unlocked(
            MCP2518FD_REGISTER_CICON,
            &control
        );

    if (result != ESP_OK) {
        return result;
    }

    control &=
        ~MCP2518FD_CICON_REQOP_MASK;

    control |=
        ((uint32_t)requested_mode <<
         MCP2518FD_CICON_REQOP_SHIFT) &
        MCP2518FD_CICON_REQOP_MASK;

    result =
        mcp2518fd_write_register_unlocked(
            MCP2518FD_REGISTER_CICON,
            control
        );

    if (result != ESP_OK) {
        return result;
    }

    const TickType_t timeout =
        MCP2518FD_DELAY_TICKS(
            MCP2518FD_MODE_TIMEOUT_MS
        );

    const TickType_t started_at =
        xTaskGetTickCount();

    while (true) {
        result =
            mcp2518fd_read_register_unlocked(
                MCP2518FD_REGISTER_CICON,
                &control
            );

        if (result != ESP_OK) {
            return result;
        }

        const uint8_t current_mode =
            (uint8_t)(
                (control &
                 MCP2518FD_CICON_OPMOD_MASK) >>
                MCP2518FD_CICON_OPMOD_SHIFT
            );

        if (current_mode == requested_mode) {
            return ESP_OK;
        }

        if ((xTaskGetTickCount() -
             started_at) >= timeout) {

            return ESP_ERR_TIMEOUT;
        }

        vTaskDelay(1U);
    }
}

static esp_err_t mcp2518fd_write_bytes_unlocked(
    uint16_t address,
    const uint8_t *data,
    size_t data_size
)
{
    if ((data == NULL) ||
        (data_size == 0U) ||
        (data_size >
        MCP2518FD_MESSAGE_RAM_TRANSFER_MAX)) {

        return ESP_ERR_INVALID_ARG;
    }

    if (((size_t)address + data_size) > 0x1000U) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t transmit_buffer[
        MCP2518FD_MESSAGE_RAM_TRANSFER_MAX +
        MCP2518FD_SPI_COMMAND_SIZE
    ];

    mcp2518fd_build_command(
        MCP2518FD_SPI_INSTRUCTION_WRITE,
        address,
        transmit_buffer
    );

    memcpy(
        &transmit_buffer[
            MCP2518FD_SPI_COMMAND_SIZE
        ],
        data,
        data_size
    );

    const size_t transfer_size =
        MCP2518FD_SPI_COMMAND_SIZE +
        data_size;

    return mcp2518fd_spi_transmit(
        transmit_buffer,
        NULL,
        transfer_size
    );
}

static esp_err_t mcp2518fd_read_bytes_unlocked(
    uint16_t address,
    uint8_t *data,
    size_t data_size
)
{
    if ((data == NULL) ||
        (data_size == 0U) ||
        (data_size >
        MCP2518FD_MESSAGE_RAM_TRANSFER_MAX)) {

        return ESP_ERR_INVALID_ARG;
    }

    if (((size_t)address + data_size) >
        0x1000U) {

        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t transmit_buffer[
        MCP2518FD_MESSAGE_RAM_TRANSFER_MAX +
        MCP2518FD_SPI_COMMAND_SIZE
    ] = {0};

    uint8_t receive_buffer[
        MCP2518FD_MESSAGE_RAM_TRANSFER_MAX +
        MCP2518FD_SPI_COMMAND_SIZE
    ] = {0};

    mcp2518fd_build_command(
        MCP2518FD_SPI_INSTRUCTION_READ,
        address,
        transmit_buffer
    );

    const size_t transfer_size =
        MCP2518FD_SPI_COMMAND_SIZE +
        data_size;

    const esp_err_t result =
        mcp2518fd_spi_transmit(
            transmit_buffer,
            receive_buffer,
            transfer_size
        );

    if (result != ESP_OK) {
        return result;
    }

    memcpy(
        data,
        &receive_buffer[
            MCP2518FD_SPI_COMMAND_SIZE
        ],
        data_size
    );

    return ESP_OK;
}

static uint32_t mcp2518fd_read_uint32(
    const uint8_t *data
)
{
    if (data == NULL) {
        return 0U;
    }

    return
        ((uint32_t)data[0]) |
        ((uint32_t)data[1] << 8U) |
        ((uint32_t)data[2] << 16U) |
        ((uint32_t)data[3] << 24U);
}

static void mcp2518fd_write_uint32(
    uint8_t *data,
    uint32_t value
)
{
    if (data == NULL) {
        return;
    }

    data[0] =
        (uint8_t)(value & 0xFFU);

    data[1] =
        (uint8_t)((value >> 8U) & 0xFFU);

    data[2] =
        (uint8_t)((value >> 16U) & 0xFFU);

    data[3] =
        (uint8_t)((value >> 24U) & 0xFFU);
}

static size_t mcp2518fd_fifo_payload_size(
    const can_fd_mcp2518fd_config_t *config
)
{
    if ((config != NULL) &&
        config->fd_enabled) {

        return
            CAN_FD_MCP2518FD_DATA_MAX_LENGTH;
    }

    return
        CAN_FD_MCP2518FD_CLASSIC_DATA_MAX_LENGTH;
}

static uint32_t mcp2518fd_fifo_payload_encoding(
    const can_fd_mcp2518fd_config_t *config
)
{
    if ((config != NULL) &&
        config->fd_enabled) {

        return
            MCP2518FD_FIFO_PAYLOAD_64_BYTES;
    }

    return
        MCP2518FD_FIFO_PAYLOAD_8_BYTES;
}

static size_t mcp2518fd_tx_object_size(
    const can_fd_mcp2518fd_config_t *config
)
{
    return
        MCP2518FD_TX_OBJECT_HEADER_SIZE +
        mcp2518fd_fifo_payload_size(
            config
        );
}

static size_t mcp2518fd_rx_object_size(
    const can_fd_mcp2518fd_config_t *config
)
{
    return
        MCP2518FD_RX_OBJECT_HEADER_SIZE +
        mcp2518fd_fifo_payload_size(
            config
        );
}

static esp_err_t mcp2518fd_configure_timestamp_unlocked(void)
{
    if ((s_config.oscillator_hz %
         MCP2518FD_TIMESTAMP_FREQUENCY_HZ) != 0U) {

        return ESP_ERR_NOT_SUPPORTED;
    }

    const uint32_t divider =
        s_config.oscillator_hz /
        MCP2518FD_TIMESTAMP_FREQUENCY_HZ;

    if ((divider == 0U) ||
        (divider > 1024U)) {

        return ESP_ERR_NOT_SUPPORTED;
    }

    /*
     * TSEOF remains clear, so timestamps are captured at SOF.
     */
    const uint32_t timestamp_config =
        (
            (divider - 1U) &
            MCP2518FD_TSCON_TBCPRE_MASK
        ) |
        MCP2518FD_TSCON_TBCEN;

    const esp_err_t result =
        mcp2518fd_write_register_unlocked(
            MCP2518FD_REGISTER_CITSCON,
            timestamp_config
        );

    if (result == ESP_OK) {
        s_info.timestamp_frequency_hz =
            MCP2518FD_TIMESTAMP_FREQUENCY_HZ;

        s_info.last_transmit_timestamp = 0U;
        s_info.last_transmit_timestamp_valid = false;

        ESP_LOGI(
            TAG,
            "Hardware timestamps enabled: frequency=%lu Hz",
            (unsigned long)
                MCP2518FD_TIMESTAMP_FREQUENCY_HZ
        );
    }

    return result;
}

static uint16_t mcp2518fd_filter_control_address(
    uint8_t filter_index
)
{
    return (uint16_t)(
        MCP2518FD_REGISTER_CIFLTCON0 +
        (
            filter_index /
            MCP2518FD_FILTERS_PER_CONTROL_REGISTER
        ) *
        MCP2518FD_FILTER_CONTROL_STRIDE
    );
}

static uint16_t mcp2518fd_filter_object_address(
    uint8_t filter_index
)
{
    return (uint16_t)(
        MCP2518FD_REGISTER_CIFLTOBJ0 +
        filter_index *
        MCP2518FD_FILTER_OBJECT_STRIDE
    );
}

static uint16_t mcp2518fd_filter_mask_address(
    uint8_t filter_index
)
{
    return (uint16_t)(
        MCP2518FD_REGISTER_CIMASK0 +
        filter_index *
        MCP2518FD_FILTER_OBJECT_STRIDE
    );
}

static bool mcp2518fd_filter_is_valid(
    const can_fd_mcp2518fd_filter_t *filter
)
{
    if (filter == NULL) {
        return false;
    }

    if (filter->index >=
        CAN_FD_MCP2518FD_FILTER_COUNT) {

        return false;
    }

    const uint32_t identifier_max =
        filter->extended
            ? CAN_FD_MCP2518FD_EXTENDED_ID_MAX
            : CAN_FD_MCP2518FD_STANDARD_ID_MAX;

    if ((filter->identifier > identifier_max) ||
        (filter->mask > identifier_max)) {

        return false;
    }

    return true;
}

static uint32_t mcp2518fd_encode_filter_identifier(
    uint32_t identifier,
    bool extended
)
{
    if (!extended) {
        return identifier &
               MCP2518FD_OBJECT_SID_MASK;
    }

    const uint32_t standard_id =
        (
            identifier >>
            18U
        ) &
        MCP2518FD_OBJECT_SID_MASK;

    const uint32_t extended_id =
        identifier &
        MCP2518FD_OBJECT_EID_MASK;

    return standard_id |
           (
               extended_id <<
               MCP2518FD_OBJECT_EID_SHIFT
           ) |
           MCP2518FD_FILTER_EXIDE;
}

static uint32_t mcp2518fd_encode_filter_mask(
    uint32_t mask,
    bool extended
)
{
    uint32_t encoded = 0U;

    if (extended) {
        const uint32_t standard_mask =
            (
                mask >>
                18U
            ) &
            MCP2518FD_OBJECT_SID_MASK;

        const uint32_t extended_mask =
            mask &
            MCP2518FD_OBJECT_EID_MASK;

        encoded =
            standard_mask |
            (
                extended_mask <<
                MCP2518FD_OBJECT_EID_SHIFT
            );
    } else {
        encoded =
            mask &
            MCP2518FD_OBJECT_SID_MASK;
    }

    /*
     * Require the received frame format to correspond to EXIDE.
     */
    encoded |=
        MCP2518FD_FILTER_MIDE;

    return encoded;
}

static esp_err_t mcp2518fd_set_filter_control_unlocked(
    uint8_t filter_index,
    bool enabled
)
{
    const uint16_t register_address =
        mcp2518fd_filter_control_address(
            filter_index
        );

    const uint32_t shift =
        (
            filter_index %
            MCP2518FD_FILTERS_PER_CONTROL_REGISTER
        ) *
        8U;

    uint32_t control = 0U;

    esp_err_t result =
        mcp2518fd_read_register_unlocked(
            register_address,
            &control
        );

    if (result != ESP_OK) {
        return result;
    }

    control &=
        ~(
            MCP2518FD_FILTER_CONTROL_BYTE_MASK <<
            shift
        );

    if (enabled) {
        const uint32_t filter_control =
            MCP2518FD_FILTER_ENABLE |
            (
                MCP2518FD_RX_FIFO_NUMBER &
                MCP2518FD_FILTER_FIFO_MASK
            );

        control |=
            filter_control <<
            shift;
    }

    return mcp2518fd_write_register_unlocked(
        register_address,
        control
    );
}

static esp_err_t mcp2518fd_set_filter_unlocked(
    const can_fd_mcp2518fd_filter_t *filter
)
{
    /*
     * Filter and mask objects may only be changed while this filter
     * is disabled.
     */
    esp_err_t result =
        mcp2518fd_set_filter_control_unlocked(
            filter->index,
            false
        );

    if (result != ESP_OK) {
        return result;
    }

    const uint32_t identifier_object =
        mcp2518fd_encode_filter_identifier(
            filter->identifier,
            filter->extended
        );

    result =
        mcp2518fd_write_register_unlocked(
            mcp2518fd_filter_object_address(
                filter->index
            ),
            identifier_object
        );

    if (result != ESP_OK) {
        return result;
    }

    const uint32_t mask_object =
        mcp2518fd_encode_filter_mask(
            filter->mask,
            filter->extended
        );

    result =
        mcp2518fd_write_register_unlocked(
            mcp2518fd_filter_mask_address(
                filter->index
            ),
            mask_object
        );

    if (result != ESP_OK) {
        return result;
    }

    return mcp2518fd_set_filter_control_unlocked(
        filter->index,
        true
    );
}

static esp_err_t mcp2518fd_disable_all_filters_unlocked(void)
{
    for (
        uint8_t register_index = 0U;
        register_index <
            MCP2518FD_FILTER_CONTROL_REGISTER_COUNT;
        ++register_index
    ) {
        const uint16_t register_address =
            (uint16_t)(
                MCP2518FD_REGISTER_CIFLTCON0 +
                register_index *
                MCP2518FD_FILTER_CONTROL_STRIDE
            );

        const esp_err_t result =
            mcp2518fd_write_register_unlocked(
                register_address,
                0U
            );

        if (result != ESP_OK) {
            return result;
        }
    }

    return ESP_OK;
}

static esp_err_t mcp2518fd_accept_all_unlocked(void)
{
    esp_err_t result =
        mcp2518fd_disable_all_filters_unlocked();

    if (result != ESP_OK) {
        return result;
    }

    /*
     * MIDE remains clear and the mask is zero, therefore Filter 0
     * accepts both Standard and Extended frames.
     */
    result =
        mcp2518fd_write_register_unlocked(
            MCP2518FD_REGISTER_CIFLTOBJ0,
            0U
        );

    if (result != ESP_OK) {
        return result;
    }

    result =
        mcp2518fd_write_register_unlocked(
            MCP2518FD_REGISTER_CIMASK0,
            0U
        );

    if (result != ESP_OK) {
        return result;
    }

    return mcp2518fd_set_filter_control_unlocked(
        0U,
        true
    );
}

static esp_err_t mcp2518fd_process_bus_diagnostics_unlocked(
    void
)
{
    uint32_t diagnostic_0 = 0U;
    uint32_t diagnostic_1 = 0U;

    esp_err_t result =
        mcp2518fd_read_register_unlocked(
            MCP2518FD_REGISTER_CIBDIAG0,
            &diagnostic_0
        );

    if (result != ESP_OK) {
        return result;
    }

    result =
        mcp2518fd_read_register_unlocked(
            MCP2518FD_REGISTER_CIBDIAG1,
            &diagnostic_1
        );

    if (result != ESP_OK) {
        return result;
    }

    /*
     * Preserve the most recently processed hardware snapshots before
     * clearing the diagnostic registers.
     */
    s_info.diagnostic_register_0 =
        diagnostic_0;

    s_info.diagnostic_register_1 =
        diagnostic_1;

    const uint32_t nominal_receive_errors =
        diagnostic_0 &
        MCP2518FD_BDIAG0_NRERRCNT_MASK;

    const uint32_t nominal_transmit_errors =
        (
            diagnostic_0 &
            MCP2518FD_BDIAG0_NTERRCNT_MASK
        ) >>
        MCP2518FD_BDIAG0_NTERRCNT_SHIFT;

    const uint32_t data_receive_errors =
        (
            diagnostic_0 &
            MCP2518FD_BDIAG0_DRERRCNT_MASK
        ) >>
        MCP2518FD_BDIAG0_DRERRCNT_SHIFT;

    const uint32_t data_transmit_errors =
        (
            diagnostic_0 &
            MCP2518FD_BDIAG0_DTERRCNT_MASK
        ) >>
        MCP2518FD_BDIAG0_DTERRCNT_SHIFT;

    s_info.receive_error_events +=
        nominal_receive_errors +
        data_receive_errors;

    s_info.transmit_error_events +=
        nominal_transmit_errors +
        data_transmit_errors;

    s_info.acknowledge_error =
        (diagnostic_1 &
         MCP2518FD_BDIAG1_NACKERR) != 0U;

    s_info.can_crc_error =
        (diagnostic_1 &
         MCP2518FD_BDIAG1_CRC_ERROR_MASK) != 0U;

    s_info.stuff_error =
        (diagnostic_1 &
         MCP2518FD_BDIAG1_STUFF_ERROR_MASK) != 0U;

    s_info.form_error =
        (diagnostic_1 &
         MCP2518FD_BDIAG1_FORM_ERROR_MASK) != 0U;

    s_info.bit_error =
        (diagnostic_1 &
         MCP2518FD_BDIAG1_BIT_ERROR_MASK) != 0U;

    if ((diagnostic_1 &
         MCP2518FD_BDIAG1_TXBOERR) != 0U) {

        ++s_info.bus_off_recovery_count;
    }

    if ((diagnostic_0 != 0U) ||
        ((diagnostic_1 & 0xFFFF0000UL) != 0U)) {

        ESP_LOGD(
            TAG,
            "CAN diagnostics: BDIAG0=0x%08lX, "
            "BDIAG1=0x%08lX",
            (unsigned long)diagnostic_0,
            (unsigned long)diagnostic_1
        );
    }

    /*
     * CiBDIAG0 counters and CiBDIAG1 diagnostic flags are writable.
     * Clear them after accumulating their values so the next
     * interrupt represents new events.
     */
    result =
        mcp2518fd_write_register_unlocked(
            MCP2518FD_REGISTER_CIBDIAG0,
            0U
        );

    if (result != ESP_OK) {
        return result;
    }

    s_info.error_free_frames +=
        diagnostic_1 & 0x0000FFFFUL;

    /*
     * Clear diagnostic flags and the hardware error-free message
     * counter. The driver currently does not expose EFMSGCNT.
     */
    return mcp2518fd_write_register_unlocked(
        MCP2518FD_REGISTER_CIBDIAG1,
        0U
    );
}

static esp_err_t mcp2518fd_process_ecc_status_unlocked(
    void
)
{
    uint32_t ecc_status = 0U;

    esp_err_t result =
        mcp2518fd_read_register_unlocked(
            MCP2518FD_REGISTER_ECCSTAT,
            &ecc_status
        );

    if (result != ESP_OK) {
        return result;
    }

    s_info.ecc_status_register =
        ecc_status;

    const bool single_error =
        (ecc_status &
        MCP2518FD_ECCSTAT_SECIF) != 0U;

    const bool double_error =
        (ecc_status &
         MCP2518FD_ECCSTAT_DEDIF) != 0U;

    if (!single_error &&
        !double_error) {

        return ESP_OK;
    }

    s_info.last_ecc_error_address =
        (uint16_t)(
            (
                ecc_status &
                MCP2518FD_ECCSTAT_ERROR_ADDRESS_MASK
            ) >>
            MCP2518FD_ECCSTAT_ERROR_ADDRESS_SHIFT
        );

    if (single_error) {
        ++s_info.ecc_single_error_count;

        ESP_LOGW(
            TAG,
            "Corrected single-bit Message RAM ECC error: "
            "address=0x%03X",
            (unsigned int)
                s_info.last_ecc_error_address
        );
    }

    if (double_error) {
        ++s_info.ecc_double_error_count;

        ESP_LOGE(
            TAG,
            "Detected double-bit Message RAM ECC error: "
            "address=0x%03X",
            (unsigned int)
                s_info.last_ecc_error_address
        );
    }

    /*
     * SECIF and DEDIF are hardware-set, software-clear flags.
     * Writing zero clears both flags. ERRADDR is read-only.
     */
    result =
        mcp2518fd_write_register_unlocked(
            MCP2518FD_REGISTER_ECCSTAT,
            0U
        );

    return result;
}

static esp_err_t mcp2518fd_configure_tef_unlocked(void)
{
    uint32_t controller_config = 0U;

    esp_err_t result =
        mcp2518fd_read_register_unlocked(
            MCP2518FD_REGISTER_CICON,
            &controller_config
        );

    if (result != ESP_OK) {
        return result;
    }

    /*
     * Enable the Transmit Event FIFO and keep the dedicated TX Queue
     * disabled. FIFO1 remains our transmit FIFO.
     */
    controller_config |=
        MCP2518FD_CICON_STEF;

    controller_config &=
        ~MCP2518FD_CICON_TXQEN;

    result =
        mcp2518fd_write_register_unlocked(
            MCP2518FD_REGISTER_CICON,
            controller_config
        );

    if (result != ESP_OK) {
        return result;
    }

    const uint32_t tef_config =
        (
            ((uint32_t)s_config.tx_fifo_depth - 1U)
                << MCP2518FD_TEF_FSIZE_SHIFT
        ) |
        MCP2518FD_TEF_FRESET |
        MCP2518FD_TEF_TEFOVIE |
        MCP2518FD_TEF_TEFTSEN |
        MCP2518FD_TEF_TEFNEIE;

    result =
        mcp2518fd_write_register_unlocked(
            MCP2518FD_REGISTER_CITEFCON,
            tef_config
        );

    if (result == ESP_OK) {
        ESP_LOGI(
            TAG,
            "Transmit Event FIFO configured: depth=%u",
            (unsigned int)s_config.tx_fifo_depth
        );
    }

    return result;
}

static esp_err_t mcp2518fd_update_error_state_unlocked(void)
{
    uint32_t error_counters = 0U;

    const esp_err_t result =
        mcp2518fd_read_register_unlocked(
            MCP2518FD_REGISTER_CITREC,
            &error_counters
        );

    if (result != ESP_OK) {
        return result;
    }

    const can_fd_mcp2518fd_state_t previous_state =
        s_info.state;

    s_info.receive_error_count =
        (uint8_t)(
            error_counters &
            MCP2518FD_TREC_REC_MASK
        );

    s_info.transmit_error_count =
        (uint8_t)(
            (error_counters &
             MCP2518FD_TREC_TEC_MASK) >>
            MCP2518FD_TREC_TEC_SHIFT
        );

    if ((error_counters &
         MCP2518FD_TREC_TXBO) != 0U) {

        s_info.state =
            CAN_FD_MCP2518FD_STATE_BUS_OFF;

        if (previous_state !=
            CAN_FD_MCP2518FD_STATE_BUS_OFF) {

            /*
             * Hardware resets every TX FIFO when entering Bus-off.
             */
            s_pending_tx_frames = 0U;

            ESP_LOGW(
                TAG,
                "MCP2518FD entered Bus-off: TEC=%u, REC=%u",
                (unsigned int)
                    s_info.transmit_error_count,
                (unsigned int)
                    s_info.receive_error_count
            );
        }
    } else if ((error_counters &
                (MCP2518FD_TREC_TXBP |
                 MCP2518FD_TREC_RXBP)) != 0U) {

        s_info.state =
            CAN_FD_MCP2518FD_STATE_ERROR_PASSIVE;
    } else if ((error_counters &
                (MCP2518FD_TREC_EWARN |
                 MCP2518FD_TREC_TXWARN |
                 MCP2518FD_TREC_RXWARN)) != 0U) {

        s_info.state =
            CAN_FD_MCP2518FD_STATE_ERROR_WARNING;
    } else {
        s_info.state =
            CAN_FD_MCP2518FD_STATE_ERROR_ACTIVE;

        if (previous_state ==
            CAN_FD_MCP2518FD_STATE_BUS_OFF) {

            ESP_LOGI(
                TAG,
                "MCP2518FD recovered from Bus-off"
            );
        }
    }

    return ESP_OK;
}

static esp_err_t mcp2518fd_process_tx_status_unlocked(void)
{
    uint32_t fifo_status = 0U;

    esp_err_t result =
        mcp2518fd_read_register_unlocked(
            MCP2518FD_REGISTER_CIFIFOSTA1,
            &fifo_status
        );

    if (result != ESP_OK) {
        return result;
    }

    const uint32_t failure_flags =
        MCP2518FD_FIFO_STATUS_TXABT |
        MCP2518FD_FIFO_STATUS_TXERR |
        MCP2518FD_FIFO_STATUS_TXATIF;

    if ((fifo_status &
         failure_flags) != 0U) {

        ++s_info.transmit_failures;

        if (s_pending_tx_frames > 0U) {
            --s_pending_tx_frames;
        }

        ESP_LOGW(
            TAG,
            "MCP2518FD TX failed: status=0x%08lX",
            (unsigned long)fifo_status
        );
    }

    if ((fifo_status &
         MCP2518FD_FIFO_STATUS_TXLARB) != 0U) {

        /*
         * Lost arbitration is normal CAN behavior. The controller can
         * retry the frame, so it is not counted as a final failure.
         */
        ESP_LOGD(
            TAG,
            "MCP2518FD transmission lost arbitration"
        );
    }

    const uint32_t clearable_flags =
        MCP2518FD_FIFO_STATUS_TXABT |
        MCP2518FD_FIFO_STATUS_TXLARB |
        MCP2518FD_FIFO_STATUS_TXERR |
        MCP2518FD_FIFO_STATUS_TXATIF;

    if ((fifo_status &
         clearable_flags) != 0U) {

        result =
            mcp2518fd_write_register_unlocked(
                MCP2518FD_REGISTER_CIFIFOSTA1,
                fifo_status &
                ~clearable_flags
            );
    }

    return result;
}

static esp_err_t mcp2518fd_handle_rx_overflow_unlocked(
    uint32_t fifo_status
)
{
    if ((fifo_status &
         MCP2518FD_FIFO_STATUS_RXOVIF) == 0U) {

        return ESP_OK;
    }

    ++s_info.receive_overflow_count;
    ++s_info.dropped_rx_frames;

    const uint32_t count =
        s_info.receive_overflow_count;

    if ((count == 1U) ||
        ((count & (count - 1U)) == 0U)) {

        ESP_LOGW(
            TAG,
            "MCP2518FD RX FIFO2 overflow: count=%lu",
            (unsigned long)count
        );
    }

    return mcp2518fd_write_register_unlocked(
        MCP2518FD_REGISTER_CIFIFOSTA2,
        fifo_status &
        ~MCP2518FD_FIFO_STATUS_RXOVIF
    );
}

static esp_err_t mcp2518fd_process_rx_overflow_unlocked(void)
{
    uint32_t fifo_status = 0U;

    const esp_err_t result =
        mcp2518fd_read_register_unlocked(
            MCP2518FD_REGISTER_CIFIFOSTA2,
            &fifo_status
        );

    if (result != ESP_OK) {
        return result;
    }

    return mcp2518fd_handle_rx_overflow_unlocked(
        fifo_status
    );
}

static esp_err_t mcp2518fd_process_interrupt_internal_unlocked(
    bool *rx_pending
)
{
    if (rx_pending == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *rx_pending = false;

    uint32_t interrupt_register = 0U;

    esp_err_t result =
        mcp2518fd_read_register_unlocked(
            MCP2518FD_REGISTER_CIINT,
            &interrupt_register
        );

    if (result != ESP_OK) {
        return result;
    }

    const uint32_t interrupt_flags =
        interrupt_register & 0x0000FFFFUL;

    s_info.interrupt_flags =
        interrupt_flags;

    if ((interrupt_flags &
         MCP2518FD_CIINT_RXIF) != 0U) {

        *rx_pending = true;

        if (s_rx_ready_semaphore != NULL) {
            (void)xSemaphoreGive(
                s_rx_ready_semaphore
            );
        }
    }

    if ((interrupt_flags &
         MCP2518FD_CIINT_TEFIF) != 0U) {

        result =
            mcp2518fd_process_tef_unlocked();

        if (result != ESP_OK) {
            return result;
        }
    }

    if ((interrupt_flags &
         MCP2518FD_CIINT_TXATIF) != 0U) {

        result =
            mcp2518fd_process_tx_status_unlocked();

        if (result != ESP_OK) {
            return result;
        }
    }

    if ((interrupt_flags &
         MCP2518FD_CIINT_RXOVIF) != 0U) {

        result =
            mcp2518fd_process_rx_overflow_unlocked();

        if (result != ESP_OK) {
            return result;
        }
    }

    if ((interrupt_flags &
         (MCP2518FD_CIINT_CERRIF |
          MCP2518FD_CIINT_SERRIF)) != 0U) {

        result =
            mcp2518fd_process_bus_diagnostics_unlocked();

        if (result != ESP_OK) {
            return result;
        }
    }

    if ((interrupt_flags &
         MCP2518FD_CIINT_ECCIF) != 0U) {

        result =
            mcp2518fd_process_ecc_status_unlocked();

        if (result != ESP_OK) {
            return result;
        }
    }

    if ((interrupt_flags &
         MCP2518FD_CIINT_CERRIF) != 0U) {

        ++s_info.bus_error_count;
    }

    const uint32_t error_flags =
        interrupt_flags &
        (
            MCP2518FD_CIINT_CERRIF |
            MCP2518FD_CIINT_SERRIF |
            MCP2518FD_CIINT_RXOVIF |
            MCP2518FD_CIINT_TXATIF |
            MCP2518FD_CIINT_ECCIF
        );

    if (error_flags != 0U) {
        result =
            mcp2518fd_update_error_state_unlocked();

        if (result != ESP_OK) {
            return result;
        }
    }

    const uint32_t flags_to_clear =
        interrupt_flags &
        MCP2518FD_CIINT_ERROR_FLAGS;

    if (flags_to_clear != 0U) {
        result =
            mcp2518fd_write_register_unlocked(
                MCP2518FD_REGISTER_CIINT,
                interrupt_register &
                ~flags_to_clear
            );
    }

    return result;
}

static esp_err_t mcp2518fd_process_interrupt_unlocked(
    bool *rx_pending
)
{
#if CAN_FD_MCP2518FD_ENABLE_PROFILING
    const int64_t started_at =
        esp_timer_get_time();
#endif

    const esp_err_t result =
        mcp2518fd_process_interrupt_internal_unlocked(
            rx_pending
        );

#if CAN_FD_MCP2518FD_ENABLE_PROFILING
    s_profile.interrupt_time_us +=
        mcp2518fd_elapsed_us(
            started_at
        );

    ++s_profile.interrupt_count;
#endif
    return result;
}

static void IRAM_ATTR mcp2518fd_gpio_interrupt(
    void *argument
)
{
    (void)argument;

    BaseType_t higher_priority_task_woken =
        pdFALSE;

    if (s_interrupt_task != NULL) {
        vTaskNotifyGiveFromISR(
            s_interrupt_task,
            &higher_priority_task_woken
        );
    }

    if (higher_priority_task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void mcp2518fd_interrupt_task(
    void *argument
)
{
    (void)argument;

    while (!atomic_load(
               &s_interrupt_task_stop
           )) {

        (void)ulTaskNotifyTake(
            pdTRUE,
            portMAX_DELAY
        );

        if (atomic_load(
                &s_interrupt_task_stop
            )) {

            break;
        }

        esp_err_t result = ESP_OK;

        bool rx_pending = false;

        for (uint32_t iteration = 0U;
            iteration < 16U;
            ++iteration) {

            const esp_err_t lock_result =
                mcp2518fd_lock();

            if (lock_result != ESP_OK) {
                result = lock_result;
                break;
            }

            result =
                mcp2518fd_process_interrupt_unlocked(
                    &rx_pending
                );

            mcp2518fd_unlock();

            if (result != ESP_OK) {
                break;
            }

            if (rx_pending) {
                break;
            }

            /*
             * INT is active low. Continue processing if another source became
             * pending while the previous snapshot was handled.
             */
            if (gpio_get_level(
                    CAN_FD_PIN_INT
                ) != 0) {

                break;
            }
        }

        /*
         * If more interrupt sources remain after the processing limit, queue
         * another task iteration. The INT line is level-active low and may not
         * generate another falling edge while it remains asserted.
         */
        if ((result == ESP_OK) &&
            !rx_pending &&
            (gpio_get_level(
                CAN_FD_PIN_INT
            ) == 0)) {

            xTaskNotifyGive(
                s_interrupt_task
            );
        }

        if (result != ESP_OK) {
            ESP_LOGW(
                TAG,
                "Failed to process MCP2518FD interrupt: %s",
                esp_err_to_name(result)
            );
        }
    }

    s_interrupt_task = NULL;
    vTaskDelete(NULL);
}

static esp_err_t mcp2518fd_enable_interrupts_unlocked(void)
{
    uint32_t tx_fifo_control = 0U;

    esp_err_t result =
        mcp2518fd_read_register_unlocked(
            MCP2518FD_REGISTER_CIFIFOCON1,
            &tx_fifo_control
        );

    if (result != ESP_OK) {
        return result;
    }

    tx_fifo_control |=
        MCP2518FD_FIFO_TXATIE;

    result =
        mcp2518fd_write_register_unlocked(
            MCP2518FD_REGISTER_CIFIFOCON1,
            tx_fifo_control
        );

    if (result != ESP_OK) {
        return result;
    }

    uint32_t rx_fifo_control = 0U;

    result =
        mcp2518fd_read_register_unlocked(
            MCP2518FD_REGISTER_CIFIFOCON2,
            &rx_fifo_control
        );

    if (result != ESP_OK) {
        return result;
    }

    rx_fifo_control |=
        MCP2518FD_FIFO_RXOVIE;

    result =
        mcp2518fd_write_register_unlocked(
            MCP2518FD_REGISTER_CIFIFOCON2,
            rx_fifo_control
        );

    if (result != ESP_OK) {
        return result;
    }

    const uint32_t interrupt_enable =
        MCP2518FD_CIINT_RXIE |
        MCP2518FD_CIINT_CERRIE |
        MCP2518FD_CIINT_SERRIE |
        MCP2518FD_CIINT_RXOVIE |
        MCP2518FD_CIINT_TEFIE |
        MCP2518FD_CIINT_TXATIE |
        MCP2518FD_CIINT_ECCIE;

    return mcp2518fd_write_register_unlocked(
        MCP2518FD_REGISTER_CIINT,
        interrupt_enable
    );
}

static bool mcp2518fd_transmit_frame_is_valid(
    const can_fd_mcp2518fd_frame_t *frame
)
{
    if (frame == NULL) {
        return false;
    }

    if (frame->extended) {
        if (frame->identifier >
            CAN_FD_MCP2518FD_EXTENDED_ID_MAX) {

            return false;
        }
    } else {
        if (frame->identifier >
            CAN_FD_MCP2518FD_STANDARD_ID_MAX) {

            return false;
        }
    }

    if (frame->fd_frame) {
        uint8_t dlc = 0U;

        if (!s_config.fd_enabled ||
            frame->remote ||
            !mcp2518fd_length_to_dlc(
                frame->data_length,
                &dlc
            )) {

            return false;
        }

        if (frame->bit_rate_switch &&
            !s_config.brs_enabled) {

            return false;
        }
    } else {
        if ((frame->data_length >
             CAN_FD_MCP2518FD_CLASSIC_DATA_MAX_LENGTH) ||
            frame->bit_rate_switch ||
            frame->error_state_indicator) {

            return false;
        }
    }

    return true;
}

static esp_err_t mcp2518fd_encode_tx_object(
    const can_fd_mcp2518fd_frame_t *frame,
    uint8_t *object,
    size_t object_size
)
{
    const size_t required_size =
        mcp2518fd_tx_object_size(
            &s_config
        );

    if (!mcp2518fd_transmit_frame_is_valid(frame) ||
        (object == NULL) ||
        (object_size < required_size)) {

        return ESP_ERR_INVALID_ARG;
    }

    uint8_t dlc = 0U;

    if (!mcp2518fd_length_to_dlc(
            frame->data_length,
            &dlc
        )) {

        return ESP_ERR_INVALID_ARG;
    }

    memset(
        object,
        0,
        required_size
    );

    uint32_t identifier_word = 0U;

    if (frame->extended) {
        const uint32_t standard_id =
            (
                frame->identifier >>
                18U
            ) &
            MCP2518FD_OBJECT_SID_MASK;

        const uint32_t extended_id =
            frame->identifier &
            MCP2518FD_OBJECT_EID_MASK;

        identifier_word =
            standard_id |
            (
                extended_id <<
                MCP2518FD_OBJECT_EID_SHIFT
            );
    } else {
        identifier_word =
            frame->identifier &
            MCP2518FD_OBJECT_SID_MASK;
    }

    uint32_t control_word =
        (uint32_t)dlc &
        MCP2518FD_OBJECT_DLC_MASK;

    if (frame->extended) {
        control_word |=
            MCP2518FD_OBJECT_IDE;
    }

    if (frame->remote) {
        control_word |=
            MCP2518FD_OBJECT_RTR;
    }

    if (frame->fd_frame) {
        control_word |=
            MCP2518FD_OBJECT_FDF;
    }

    if (frame->bit_rate_switch) {
        control_word |=
            MCP2518FD_OBJECT_BRS;
    }

    if (frame->error_state_indicator) {
        control_word |=
            MCP2518FD_OBJECT_ESI;
    }

    mcp2518fd_write_uint32(
        &object[0],
        identifier_word
    );

    mcp2518fd_write_uint32(
        &object[4],
        control_word
    );

    /*
     * Remote frames carry a DLC but no payload bytes.
     */
    if (!frame->remote &&
        (frame->data_length > 0U)) {

        memcpy(
            &object[
                MCP2518FD_TX_OBJECT_HEADER_SIZE
            ],
            frame->data,
            frame->data_length
        );
    }

    return ESP_OK;
}

static esp_err_t mcp2518fd_request_tx_unlocked(void)
{
    uint32_t fifo_control = 0U;

    esp_err_t result =
        mcp2518fd_read_register_unlocked(
            MCP2518FD_REGISTER_CIFIFOCON1,
            &fifo_control
        );

    if (result != ESP_OK) {
        return result;
    }

    /*
     * UINC moves the FIFO head to the next object. TXREQ requests
     * transmission of all pending objects in this TX FIFO.
     */
    fifo_control |=
        MCP2518FD_FIFO_UINC |
        MCP2518FD_FIFO_TXREQ;

    return mcp2518fd_write_register_unlocked(
        MCP2518FD_REGISTER_CIFIFOCON1,
        fifo_control
    );
}

static esp_err_t mcp2518fd_increment_rx_fifo_unlocked(void)
{
    const uint8_t command =
        MCP2518FD_FIFO_UINC_BYTE;

    /*
     * UINC is bit 8 of CiFIFOCON2, therefore it is bit 0 of the second
     * little-endian register byte. Writing only that byte avoids sending
     * the other three control bytes for every received CAN frame. UINC
     * is a command bit and is cleared by the controller after execution.
     */
    return mcp2518fd_write_bytes_unlocked(
        (uint16_t)(
            MCP2518FD_REGISTER_CIFIFOCON2 +
            MCP2518FD_FIFO_COMMAND_BYTE_OFFSET
        ),
        &command,
        sizeof(command)
    );
}

static esp_err_t mcp2518fd_decode_rx_object(
    const uint8_t *object,
    can_fd_mcp2518fd_frame_t *frame
)
{
    if ((object == NULL) ||
        (frame == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    memset(
        frame,
        0,
        sizeof(*frame)
    );

    const uint32_t identifier_word =
        mcp2518fd_read_uint32(
            &object[0]
        );

    const uint32_t control_word =
        mcp2518fd_read_uint32(
            &object[4]
        );

    frame->timestamp =
        mcp2518fd_read_uint32(
            &object[8]
        );

    frame->timestamp_valid = true;

    frame->extended =
        (control_word &
         MCP2518FD_OBJECT_IDE) != 0U;

    frame->remote =
        (control_word &
         MCP2518FD_OBJECT_RTR) != 0U;

    frame->fd_frame =
        (control_word &
         MCP2518FD_OBJECT_FDF) != 0U;

    frame->bit_rate_switch =
        (control_word &
         MCP2518FD_OBJECT_BRS) != 0U;

    frame->error_state_indicator =
        (control_word &
         MCP2518FD_OBJECT_ESI) != 0U;

    const uint8_t dlc =
        (uint8_t)(
            control_word &
            MCP2518FD_OBJECT_DLC_MASK
        );

    if (!mcp2518fd_dlc_to_length(
            dlc,
            &frame->data_length
        )) {

        return ESP_ERR_INVALID_RESPONSE;
    }

    if (frame->fd_frame) {
        if (frame->remote) {
            return ESP_ERR_INVALID_RESPONSE;
        }
    } else {
        if ((frame->data_length >
            CAN_FD_MCP2518FD_CLASSIC_DATA_MAX_LENGTH) ||
            frame->bit_rate_switch ||
            frame->error_state_indicator) {

            return ESP_ERR_INVALID_RESPONSE;
        }
    }

    const uint32_t standard_id =
        identifier_word &
        MCP2518FD_OBJECT_SID_MASK;

    if (frame->extended) {
        const uint32_t extended_id =
            (
                identifier_word >>
                MCP2518FD_OBJECT_EID_SHIFT
            ) &
            MCP2518FD_OBJECT_EID_MASK;

        frame->identifier =
            (standard_id << 18U) |
            extended_id;
    } else {
        frame->identifier =
            standard_id;
    }

    /*
     * RTR frames carry a DLC but do not contain application payload.
     */
    if (!frame->remote &&
        (frame->data_length > 0U)) {

        memcpy(
            frame->data,
            &object[
                MCP2518FD_RX_OBJECT_HEADER_SIZE
            ],
            frame->data_length
        );
    }

    return ESP_OK;
}

static esp_err_t mcp2518fd_initialize_message_ram_unlocked(void)
{
    /*
     * Enable ECC before initializing RAM so parity bits are generated
     * together with the initial RAM contents.
     */
    esp_err_t result =
        mcp2518fd_write_register_unlocked(
            MCP2518FD_REGISTER_ECCCON,
            MCP2518FD_ECCCON_ECCEN
        );

    if (result != ESP_OK) {
        return result;
    }

    const uint8_t zero_buffer[
        MCP2518FD_MESSAGE_RAM_CLEAR_CHUNK
    ] = {0};

    size_t offset = 0U;

    while (offset < MCP2518FD_MESSAGE_RAM_SIZE) {
        size_t chunk_size =
            MCP2518FD_MESSAGE_RAM_SIZE - offset;

        if (chunk_size >
            MCP2518FD_MESSAGE_RAM_CLEAR_CHUNK) {

            chunk_size =
                MCP2518FD_MESSAGE_RAM_CLEAR_CHUNK;
        }

        result =
            mcp2518fd_write_bytes_unlocked(
                (uint16_t)(
                    MCP2518FD_MESSAGE_RAM_BASE +
                    offset
                ),
                zero_buffer,
                chunk_size
            );

        if (result != ESP_OK) {
            ESP_LOGE(
                TAG,
                "Failed to initialize Message RAM at 0x%03X: %s",
                (unsigned int)(
                    MCP2518FD_MESSAGE_RAM_BASE +
                    offset
                ),
                esp_err_to_name(result)
            );

            return result;
        }

        offset += chunk_size;
    }

    return ESP_OK;
}

static esp_err_t mcp2518fd_validate_message_ram(
    const can_fd_mcp2518fd_config_t *config
)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if ((config->tx_fifo_depth == 0U) ||
        (config->tx_fifo_depth > 32U) ||
        (config->rx_fifo_depth == 0U) ||
        (config->rx_fifo_depth > 32U)) {

        return ESP_ERR_INVALID_ARG;
    }

    const size_t transmit_event_size =
        (size_t)config->tx_fifo_depth *
        MCP2518FD_TEF_OBJECT_SIZE;

    const size_t transmit_fifo_size =
        (size_t)config->tx_fifo_depth *
        mcp2518fd_tx_object_size(
            config
        );

    const size_t receive_fifo_size =
        (size_t)config->rx_fifo_depth *
        mcp2518fd_rx_object_size(
            config
        );

    const size_t required_size =
        transmit_event_size +
        transmit_fifo_size +
        receive_fifo_size;

    if (required_size >
        MCP2518FD_MESSAGE_RAM_SIZE) {

        ESP_LOGE(
            TAG,
            "Message RAM overflow: required=%u, available=%u, "
            "TX object=%u, RX object=%u",
            (unsigned int)required_size,
            (unsigned int)MCP2518FD_MESSAGE_RAM_SIZE,
            (unsigned int)mcp2518fd_tx_object_size(
                config
            ),
            (unsigned int)mcp2518fd_rx_object_size(
                config
            )
        );

        return ESP_ERR_INVALID_SIZE;
    }

    return ESP_OK;
}

static esp_err_t mcp2518fd_configure_fifos_unlocked(void)
{
    const uint32_t payload_encoding =
        mcp2518fd_fifo_payload_encoding(
            &s_config
        );

    esp_err_t result =
        mcp2518fd_validate_message_ram(
            &s_config
        );

    if (result != ESP_OK) {
        return result;
    }

    uint32_t controller_config = 0U;

    result =
        mcp2518fd_read_register_unlocked(
            MCP2518FD_REGISTER_CICON,
            &controller_config
        );

    if (result != ESP_OK) {
        return result;
    }

    /*
     * FIFO1 is used for transmission instead of the dedicated TX Queue.
     * The Transmit Event FIFO is enabled for tracked transmissions and
     * hardware timestamps.
     */
    controller_config &=
        ~MCP2518FD_CICON_TXQEN;

    controller_config |=
        MCP2518FD_CICON_STEF;

    if (s_config.retransmission ==
        CAN_FD_MCP2518FD_RETRANSMISSION_UNLIMITED) {

        controller_config &=
            ~MCP2518FD_CICON_RTXAT;
    } else {
        controller_config |=
            MCP2518FD_CICON_RTXAT;
    }

    result =
        mcp2518fd_write_register_unlocked(
            MCP2518FD_REGISTER_CICON,
            controller_config
        );

    if (result != ESP_OK) {
        return result;
    }

    const uint32_t tx_fifo_config =
        (
            payload_encoding <<
            MCP2518FD_FIFO_PLSIZE_SHIFT
        ) |
        (
            ((uint32_t)s_config.tx_fifo_depth - 1U)
                << MCP2518FD_FIFO_FSIZE_SHIFT
        ) |
        mcp2518fd_get_tx_attempt_configuration() |
        MCP2518FD_FIFO_TXEN |
        MCP2518FD_FIFO_TXATIE |
        MCP2518FD_FIFO_FRESET;

    result =
        mcp2518fd_write_register_unlocked(
            MCP2518FD_REGISTER_CIFIFOCON1,
            tx_fifo_config
        );

    if (result != ESP_OK) {
        return result;
    }

    const uint32_t rx_fifo_config =
        (
            payload_encoding <<
            MCP2518FD_FIFO_PLSIZE_SHIFT
        ) |
        (
            ((uint32_t)s_config.rx_fifo_depth - 1U)
                << MCP2518FD_FIFO_FSIZE_SHIFT
        ) |
        MCP2518FD_FIFO_RXTSEN |
        MCP2518FD_FIFO_RXOVIE |
        MCP2518FD_FIFO_TFNRFNIE |
        MCP2518FD_FIFO_FRESET;

    result =
        mcp2518fd_write_register_unlocked(
            MCP2518FD_REGISTER_CIFIFOCON2,
            rx_fifo_config
        );

    if (result != ESP_OK) {
        return result;
    }

    mcp2518fd_invalidate_rx_fifo_address();

    result =
        mcp2518fd_initialize_rx_fifo_address_unlocked();

    if (result != ESP_OK) {
        return result;
    }

    ESP_LOGI(
        TAG,
        "FIFO configured: payload=%u, "
        "TX FIFO1 depth=%u, RX FIFO2 depth=%u",
        (unsigned int)mcp2518fd_fifo_payload_size(
            &s_config
        ),
        (unsigned int)s_config.tx_fifo_depth,
        (unsigned int)s_config.rx_fifo_depth
    );

    return ESP_OK;
}

static bool mcp2518fd_length_to_dlc(
    uint8_t length,
    uint8_t *dlc
)
{
    if (dlc == NULL) {
        return false;
    }

    if (length <= 8U) {
        *dlc = length;
        return true;
    }

    switch (length) {
        case 12U:
            *dlc = 9U;
            return true;

        case 16U:
            *dlc = 10U;
            return true;

        case 20U:
            *dlc = 11U;
            return true;

        case 24U:
            *dlc = 12U;
            return true;

        case 32U:
            *dlc = 13U;
            return true;

        case 48U:
            *dlc = 14U;
            return true;

        case 64U:
            *dlc = 15U;
            return true;

        default:
            return false;
    }
}

static bool mcp2518fd_dlc_to_length(
    uint8_t dlc,
    uint8_t *length
)
{
    if ((length == NULL) ||
        (dlc > 15U)) {

        return false;
    }

    static const uint8_t lengths[16] = {
        0U, 1U, 2U, 3U,
        4U, 5U, 6U, 7U,
        8U, 12U, 16U, 20U,
        24U, 32U, 48U, 64U,
    };

    *length = lengths[dlc];

    return true;
}

static const char *mcp2518fd_retransmission_name(
    can_fd_mcp2518fd_retransmission_t policy
)
{
    switch (policy) {
        case CAN_FD_MCP2518FD_RETRANSMISSION_THREE_ATTEMPTS:
            return "three attempts";

        case CAN_FD_MCP2518FD_RETRANSMISSION_DISABLED:
            return "disabled";

        case CAN_FD_MCP2518FD_RETRANSMISSION_UNLIMITED:
        default:
            return "unlimited";
    }
}

static esp_err_t mcp2518fd_configure_acceptance_filter_unlocked(void)
{
    const esp_err_t result =
        mcp2518fd_accept_all_unlocked();

    if (result == ESP_OK) {
        ESP_LOGI(
            TAG,
            "Acceptance Filter 0 configured: all frames -> FIFO2"
        );
    }

    return result;
}

static esp_err_t mcp2518fd_configure_system_clock_unlocked(void)
{
    uint32_t oscillator = 0U;

    esp_err_t result =
        mcp2518fd_read_register_unlocked(
            MCP2518FD_REGISTER_OSC,
            &oscillator
        );

    if (result != ESP_OK) {
        return result;
    }

    /*
     * Use the external 20 MHz oscillator directly:
     *
     * PLLEN = 0
     * SCLKDIV = 0
     * Fsys = 20 MHz
     */
    oscillator &=
        ~MCP2518FD_OSC_SCLKDIV;

    oscillator &=
        ~MCP2518FD_OSC_PLL_ENABLE; /* PLLEN */

    result =
        mcp2518fd_write_register_unlocked(
            MCP2518FD_REGISTER_OSC,
            oscillator &
            MCP2518FD_OSC_CONFIGURATION_MASK
        );

    if (result != ESP_OK) {
        return result;
    }

    const TickType_t timeout =
        MCP2518FD_DELAY_TICKS(
            MCP2518FD_OSC_READY_TIMEOUT_MS
        );

    const TickType_t started_at =
        xTaskGetTickCount();

    while (true) {
        result =
            mcp2518fd_read_register_unlocked(
                MCP2518FD_REGISTER_OSC,
                &oscillator
            );

        if (result != ESP_OK) {
            return result;
        }

        s_info.oscillator_register =
            oscillator;

        const bool oscillator_ready =
            (oscillator &
             MCP2518FD_OSC_READY) != 0U;

        const bool divider_disabled =
            (oscillator &
             MCP2518FD_OSC_SCLKDIV_STATUS) == 0U;

        if (oscillator_ready &&
            divider_disabled) {

            s_info.oscillator_ready = true;

            return ESP_OK;
        }

        if ((xTaskGetTickCount() -
             started_at) >= timeout) {

            return ESP_ERR_TIMEOUT;
        }

        vTaskDelay(1U);
    }
}

static bool mcp2518fd_calculate_nominal_timing(
    uint32_t clock_hz,
    uint32_t requested_bitrate,
    mcp2518fd_nominal_timing_t *timing
)
{
    if ((clock_hz == 0U) ||
        (requested_bitrate == 0U) ||
        (timing == NULL)) {

        return false;
    }

    memset(
        timing,
        0,
        sizeof(*timing)
    );

    bool found = false;
    uint32_t best_sample_error = UINT32_MAX;
    uint32_t best_total_tq = 0U;

    for (uint32_t brp = 1U;
         brp <= MCP2518FD_NOMINAL_BRP_MAX;
         ++brp) {

        const uint64_t denominator =
            (uint64_t)requested_bitrate *
            brp;

        if ((denominator == 0U) ||
            ((uint64_t)clock_hz %
             denominator) != 0U) {

            continue;
        }

        const uint32_t total_tq =
            (uint32_t)(
                (uint64_t)clock_hz /
                denominator
            );

        if ((total_tq <
             MCP2518FD_NOMINAL_TQ_MIN) ||
            (total_tq >
             MCP2518FD_NOMINAL_TQ_MAX)) {

            continue;
        }

        uint32_t tseg1 =
            ((total_tq *
              MCP2518FD_NOMINAL_SAMPLE_POINT) /
             1000U);

        if (tseg1 == 0U) {
            continue;
        }

        /*
         * Sample point includes the one-TQ synchronization segment.
         */
        tseg1 -= 1U;

        if (tseg1 >
            MCP2518FD_NOMINAL_TSEG1_MAX) {

            tseg1 =
                MCP2518FD_NOMINAL_TSEG1_MAX;
        }

        const uint32_t tseg2 =
            total_tq -
            1U -
            tseg1;

        if ((tseg1 == 0U) ||
            (tseg2 == 0U) ||
            (tseg2 >
             MCP2518FD_NOMINAL_TSEG2_MAX)) {

            continue;
        }

        const uint32_t sample_point =
            ((1U + tseg1) * 1000U) /
            total_tq;

        const uint32_t sample_error =
            sample_point >
                MCP2518FD_NOMINAL_SAMPLE_POINT
                ? sample_point -
                    MCP2518FD_NOMINAL_SAMPLE_POINT
                : MCP2518FD_NOMINAL_SAMPLE_POINT -
                    sample_point;

        if (!found ||
            (sample_error <
             best_sample_error) ||
            ((sample_error ==
              best_sample_error) &&
             (total_tq >
              best_total_tq))) {

            timing->brp =
                (uint16_t)brp;

            timing->tseg1 =
                (uint16_t)tseg1;

            timing->tseg2 =
                (uint16_t)tseg2;

            timing->sjw =
                (uint16_t)(
                    tseg2 < 4U
                        ? tseg2
                        : 4U
                );

            timing->sample_point_permill =
                (uint16_t)sample_point;

            timing->bitrate =
                requested_bitrate;

            best_sample_error =
                sample_error;

            best_total_tq =
                total_tq;

            found = true;
        }
    }

    return found;
}

static bool mcp2518fd_calculate_data_timing(
    uint32_t clock_hz,
    uint32_t requested_bitrate,
    mcp2518fd_data_timing_t *timing
)
{
    if ((clock_hz == 0U) ||
        (requested_bitrate == 0U) ||
        (timing == NULL)) {

        return false;
    }

    memset(
        timing,
        0,
        sizeof(*timing)
    );

    bool found = false;
    uint32_t best_sample_error = UINT32_MAX;
    uint32_t best_total_tq = 0U;

    for (uint32_t brp = 1U;
         brp <= MCP2518FD_DATA_BRP_MAX;
         ++brp) {

        const uint64_t denominator =
            (uint64_t)requested_bitrate *
            brp;

        if ((denominator == 0U) ||
            (((uint64_t)clock_hz %
              denominator) != 0U)) {

            continue;
        }

        const uint32_t total_tq =
            (uint32_t)(
                (uint64_t)clock_hz /
                denominator
            );

        if ((total_tq <
             MCP2518FD_DATA_TQ_MIN) ||
            (total_tq >
             MCP2518FD_DATA_TQ_MAX)) {

            continue;
        }

        uint32_t tseg1 =
            (
                total_tq *
                MCP2518FD_DATA_SAMPLE_POINT
            ) / 1000U;

        if (tseg1 == 0U) {
            continue;
        }

        /*
         * The sample point includes the one-DTQ synchronization
         * segment.
         */
        --tseg1;

        if (tseg1 >
            MCP2518FD_DATA_TSEG1_MAX) {

            tseg1 =
                MCP2518FD_DATA_TSEG1_MAX;
        }

        const uint32_t tseg2 =
            total_tq -
            1U -
            tseg1;

        if ((tseg1 == 0U) ||
            (tseg2 == 0U) ||
            (tseg2 >
             MCP2518FD_DATA_TSEG2_MAX)) {

            continue;
        }

        const uint32_t sample_point =
            (
                (1U + tseg1) *
                1000U
            ) / total_tq;

        const uint32_t sample_error =
            sample_point >
                MCP2518FD_DATA_SAMPLE_POINT
                ? sample_point -
                    MCP2518FD_DATA_SAMPLE_POINT
                : MCP2518FD_DATA_SAMPLE_POINT -
                    sample_point;

        uint32_t tdc_offset =
            brp * tseg1;

        if (tdc_offset >
            MCP2518FD_TDC_TDCO_MAX) {

            continue;
        }

        if (!found ||
            (sample_error <
             best_sample_error) ||
            ((sample_error ==
              best_sample_error) &&
             (total_tq >
              best_total_tq))) {

            timing->brp =
                (uint16_t)brp;

            timing->tseg1 =
                (uint16_t)tseg1;

            timing->tseg2 =
                (uint16_t)tseg2;

            uint32_t sjw = tseg2;

            if (sjw >
                MCP2518FD_DATA_SJW_MAX) {

                sjw =
                    MCP2518FD_DATA_SJW_MAX;
            }

            timing->sjw =
                (uint16_t)sjw;

            timing->sample_point_permill =
                (uint16_t)sample_point;

            timing->tdc_offset =
                (uint16_t)tdc_offset;

            timing->bitrate =
                requested_bitrate;

            best_sample_error =
                sample_error;

            best_total_tq =
                total_tq;

            found = true;
        }
    }

    return found;
}

static esp_err_t mcp2518fd_configure_data_timing_unlocked(
    const mcp2518fd_data_timing_t *timing
)
{
    if ((timing == NULL) ||
        (timing->brp == 0U) ||
        (timing->brp >
         MCP2518FD_DATA_BRP_MAX) ||
        (timing->tseg1 == 0U) ||
        (timing->tseg1 >
         MCP2518FD_DATA_TSEG1_MAX) ||
        (timing->tseg2 == 0U) ||
        (timing->tseg2 >
         MCP2518FD_DATA_TSEG2_MAX) ||
        (timing->sjw == 0U) ||
        (timing->sjw >
         MCP2518FD_DATA_SJW_MAX) ||
        (timing->sjw >
         timing->tseg1) ||
        (timing->sjw >
         timing->tseg2)) {

        return ESP_ERR_INVALID_ARG;
    }

    const uint32_t register_value =
        ((uint32_t)(timing->brp - 1U) <<
         MCP2518FD_DBTCFG_BRP_SHIFT) |

        ((uint32_t)(timing->tseg1 - 1U) <<
         MCP2518FD_DBTCFG_TSEG1_SHIFT) |

        ((uint32_t)(timing->tseg2 - 1U) <<
         MCP2518FD_DBTCFG_TSEG2_SHIFT) |

        ((uint32_t)(timing->sjw - 1U) <<
         MCP2518FD_DBTCFG_SJW_SHIFT);

    return mcp2518fd_write_register_unlocked(
        MCP2518FD_REGISTER_CIDBTCFG,
        register_value
    );
}

static esp_err_t mcp2518fd_configure_nominal_timing_unlocked(
    const mcp2518fd_nominal_timing_t *timing
)
{
    if ((timing == NULL) ||
        (timing->brp == 0U) ||
        (timing->tseg1 == 0U) ||
        (timing->tseg2 == 0U) ||
        (timing->sjw == 0U)) {

        return ESP_ERR_INVALID_ARG;
    }

    /*
     * MCP2518FD stores each timing value minus one.
     */
    const uint32_t register_value =
        ((uint32_t)(timing->brp - 1U) <<
         MCP2518FD_NBTCFG_BRP_SHIFT) |

        ((uint32_t)(timing->tseg1 - 1U) <<
         MCP2518FD_NBTCFG_TSEG1_SHIFT) |

        ((uint32_t)(timing->tseg2 - 1U) <<
         MCP2518FD_NBTCFG_TSEG2_SHIFT) |

        ((uint32_t)(timing->sjw - 1U) <<
         MCP2518FD_NBTCFG_SJW_SHIFT);

    return mcp2518fd_write_register_unlocked(
        MCP2518FD_REGISTER_CINBTCFG,
        register_value
    );
}

static esp_err_t mcp2518fd_configure_tdc_unlocked(
    const mcp2518fd_data_timing_t *timing,
    bool enabled
)
{
    if (timing == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!enabled) {
        return mcp2518fd_write_register_unlocked(
            MCP2518FD_REGISTER_CITDC,
            0U
        );
    }

    if (timing->tdc_offset >
        MCP2518FD_TDC_TDCO_MAX) {

        return ESP_ERR_INVALID_ARG;
    }

    const uint32_t register_value =
        (
            (uint32_t)
            MCP2518FD_TDC_TDCMOD_AUTOMATIC
            <<
            MCP2518FD_TDC_TDCMOD_SHIFT
        ) |
        (
            (uint32_t)timing->tdc_offset
            <<
            MCP2518FD_TDC_TDCO_SHIFT
        );

    return mcp2518fd_write_register_unlocked(
        MCP2518FD_REGISTER_CITDC,
        register_value
    );
}

static uint8_t mcp2518fd_get_requested_operation_mode(void)
{
    switch (s_config.mode) {
        case CAN_FD_MCP2518FD_MODE_LISTEN_ONLY:
            return
                MCP2518FD_OPERATION_MODE_LISTEN_ONLY;

        case CAN_FD_MCP2518FD_MODE_INTERNAL_LOOPBACK:
            return
                MCP2518FD_OPERATION_MODE_INTERNAL_LOOPBACK;

        case CAN_FD_MCP2518FD_MODE_EXTERNAL_LOOPBACK:
            return
                MCP2518FD_OPERATION_MODE_EXTERNAL_LOOPBACK;

        case CAN_FD_MCP2518FD_MODE_RESTRICTED:
            return
                MCP2518FD_OPERATION_MODE_RESTRICTED;

        case CAN_FD_MCP2518FD_MODE_NORMAL:
        default:
            return s_config.fd_enabled
                ? MCP2518FD_OPERATION_MODE_NORMAL_FD
                : MCP2518FD_OPERATION_MODE_NORMAL_20;
    }
}

static bool mcp2518fd_runtime_config_is_valid(
    const can_fd_mcp2518fd_runtime_config_t *config
)
{
    if ((config == NULL) ||
        (config->nominal_bitrate == 0U) ||
        (config->nominal_bitrate >
        MCP2518FD_CLASSIC_BITRATE_MAX) ||
        (config->data_bitrate == 0U) ||
        (config->data_bitrate >
        MCP2518FD_DATA_BITRATE_MAX)) {

        return false;
    }

    if (config->brs_enabled &&
        !config->fd_enabled) {

        return false;
    }

    if (config->brs_enabled &&
        (config->data_bitrate <
        config->nominal_bitrate)) {

        return false;
    }

    if (!config->fd_enabled &&
        (config->data_bitrate !=
        config->nominal_bitrate)) {

        return false;
    }

    if (config->fd_enabled &&
        !config->brs_enabled &&
        (config->data_bitrate !=
        config->nominal_bitrate)) {

        return false;
    }

    switch (config->mode) {
        case CAN_FD_MCP2518FD_MODE_NORMAL:
        case CAN_FD_MCP2518FD_MODE_LISTEN_ONLY:
        case CAN_FD_MCP2518FD_MODE_INTERNAL_LOOPBACK:
        case CAN_FD_MCP2518FD_MODE_EXTERNAL_LOOPBACK:
        case CAN_FD_MCP2518FD_MODE_RESTRICTED:
            break;

        default:
            return false;
    }

    switch (config->retransmission) {
        case CAN_FD_MCP2518FD_RETRANSMISSION_UNLIMITED:
        case CAN_FD_MCP2518FD_RETRANSMISSION_THREE_ATTEMPTS:
        case CAN_FD_MCP2518FD_RETRANSMISSION_DISABLED:
            break;

        default:
            return false;
    }

    return true;
}

static bool mcp2518fd_mode_allows_transmission(
    can_fd_mcp2518fd_mode_t mode
)
{
    switch (mode) {
        case CAN_FD_MCP2518FD_MODE_NORMAL:
        case CAN_FD_MCP2518FD_MODE_INTERNAL_LOOPBACK:
        case CAN_FD_MCP2518FD_MODE_EXTERNAL_LOOPBACK:
            return true;

        case CAN_FD_MCP2518FD_MODE_LISTEN_ONLY:
        case CAN_FD_MCP2518FD_MODE_RESTRICTED:
        default:
            return false;
    }
}

static bool mcp2518fd_config_is_valid(
    const can_fd_mcp2518fd_config_t *config
)
{
    if (config == NULL) {
        return false;
    }

    if ((config->oscillator_hz !=
        MCP2518FD_SUPPORTED_OSCILLATOR_HZ) ||
        (config->nominal_bitrate == 0U) ||
        (config->nominal_bitrate >
        MCP2518FD_CLASSIC_BITRATE_MAX) ||
        (config->data_bitrate == 0U) ||
        (config->tx_fifo_depth == 0U) ||
        (config->tx_fifo_depth > 32U) ||
        (config->rx_fifo_depth == 0U) ||
        (config->rx_fifo_depth > 32U)) {

        return false;
    }

    if (config->brs_enabled &&
        !config->fd_enabled) {

        return false;
    }

    if (config->brs_enabled &&
        (config->data_bitrate <
        config->nominal_bitrate)) {

        return false;
    }

    if (!config->fd_enabled &&
        (config->data_bitrate !=
        config->nominal_bitrate)) {

        return false;
    }

    if (config->fd_enabled) {
        if ((config->data_bitrate == 0U) ||
            (config->data_bitrate >
            MCP2518FD_DATA_BITRATE_MAX)) {

            return false;
        }

        if (!config->brs_enabled &&
            (config->data_bitrate !=
            config->nominal_bitrate)) {

            return false;
        }
    }

    if (config->spi_crc_enabled) {
        return false;
    }

    switch (config->retransmission) {
        case CAN_FD_MCP2518FD_RETRANSMISSION_UNLIMITED:
        case CAN_FD_MCP2518FD_RETRANSMISSION_THREE_ATTEMPTS:
        case CAN_FD_MCP2518FD_RETRANSMISSION_DISABLED:
            break;

        default:
            return false;
    }

    switch (config->mode) {
        case CAN_FD_MCP2518FD_MODE_NORMAL:
        case CAN_FD_MCP2518FD_MODE_LISTEN_ONLY:
        case CAN_FD_MCP2518FD_MODE_INTERNAL_LOOPBACK:
        case CAN_FD_MCP2518FD_MODE_EXTERNAL_LOOPBACK:
        case CAN_FD_MCP2518FD_MODE_RESTRICTED:
            return true;

        default:
            return false;
    }
}

static uint32_t mcp2518fd_get_tx_attempt_configuration(void)
{
    switch (s_config.retransmission) {
        case CAN_FD_MCP2518FD_RETRANSMISSION_THREE_ATTEMPTS:
            return
                MCP2518FD_FIFO_TXAT_THREE_ATTEMPTS
                << MCP2518FD_FIFO_TXAT_SHIFT;

        case CAN_FD_MCP2518FD_RETRANSMISSION_DISABLED:
            return
                MCP2518FD_FIFO_TXAT_DISABLED
                << MCP2518FD_FIFO_TXAT_SHIFT;

        case CAN_FD_MCP2518FD_RETRANSMISSION_UNLIMITED:
        default:
            return
                MCP2518FD_FIFO_TXAT_UNLIMITED
                << MCP2518FD_FIFO_TXAT_SHIFT;
    }
}

static esp_err_t mcp2518fd_configure_protocol_unlocked(void)
{
    uint32_t control = 0U;

    esp_err_t result =
        mcp2518fd_read_register_unlocked(
            MCP2518FD_REGISTER_CICON,
            &control
        );

    if (result != ESP_OK) {
        return result;
    }

    control |=
        MCP2518FD_CICON_ISOCRCEN;

    if (s_config.fd_enabled &&
        s_config.brs_enabled) {

        control &=
            ~MCP2518FD_CICON_BRSDIS;
    } else {
        control |=
            MCP2518FD_CICON_BRSDIS;
    }

    return mcp2518fd_write_register_unlocked(
        MCP2518FD_REGISTER_CICON,
        control
    );
}

static esp_err_t mcp2518fd_apply_runtime_config_unlocked(
    const can_fd_mcp2518fd_config_t *config,
    const mcp2518fd_nominal_timing_t *nominal_timing,
    const mcp2518fd_data_timing_t *data_timing
)
{
    if ((config == NULL) ||
        (nominal_timing == NULL) ||
        (data_timing == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    s_config = *config;

    esp_err_t result =
        mcp2518fd_configure_nominal_timing_unlocked(
            nominal_timing
        );

    if (result == ESP_OK) {
        result =
            mcp2518fd_configure_data_timing_unlocked(
                data_timing
            );
    }

    if (result == ESP_OK) {
        result =
            mcp2518fd_configure_tdc_unlocked(
                data_timing,
                config->fd_enabled &&
                config->brs_enabled
            );
    }

    if (result == ESP_OK) {
        result =
            mcp2518fd_configure_protocol_unlocked();
    }

    if (result == ESP_OK) {
        result =
            mcp2518fd_configure_tef_unlocked();
    }

    if (result == ESP_OK) {
        result =
            mcp2518fd_configure_fifos_unlocked();
    }

    if (result == ESP_OK) {
        result =
            mcp2518fd_enable_interrupts_unlocked();
    }

    if (result == ESP_OK) {
        result =
            mcp2518fd_request_mode_unlocked(
                mcp2518fd_get_requested_operation_mode()
            );
    }

    return result;
}

static void mcp2518fd_update_runtime_info(
    const mcp2518fd_nominal_timing_t *nominal_timing,
    const mcp2518fd_data_timing_t *data_timing
)
{
    if ((nominal_timing == NULL) ||
        (data_timing == NULL)) {

        return;
    }

    s_info.mode =
        s_config.mode;

    s_info.fd_enabled =
        s_config.fd_enabled;

    s_info.brs_enabled =
        s_config.brs_enabled;

    s_info.nominal_bitrate =
        nominal_timing->bitrate;

    s_info.data_bitrate =
        data_timing->bitrate;

    s_info.data_sample_point_permill =
        data_timing->sample_point_permill;

    s_info.data_brp =
        data_timing->brp;

    s_info.data_tseg1 =
        data_timing->tseg1;

    s_info.data_tseg2 =
        data_timing->tseg2;

    s_info.data_sjw =
        data_timing->sjw;

    s_info.tdc_enabled =
        s_config.fd_enabled &&
        s_config.brs_enabled;

    s_info.tdc_offset =
        s_info.tdc_enabled
            ? data_timing->tdc_offset
            : 0U;

    s_info.retransmission =
        s_config.retransmission;

    s_info.state =
        CAN_FD_MCP2518FD_STATE_ERROR_ACTIVE;
}

static esp_err_t mcp2518fd_wait_for_oscillator(void)
{
    const TickType_t poll_delay =
        MCP2518FD_DELAY_TICKS(
            MCP2518FD_OSC_READY_POLL_MS
        );

    const TickType_t timeout =
        MCP2518FD_DELAY_TICKS(
            MCP2518FD_OSC_READY_TIMEOUT_MS
        );

    const TickType_t started_at =
        xTaskGetTickCount();

    while (true) {
        uint32_t oscillator = 0U;

        const esp_err_t result =
            mcp2518fd_read_register_unlocked(
                MCP2518FD_REGISTER_OSC,
                &oscillator
            );

        if (result != ESP_OK) {
            return result;
        }

        s_info.oscillator_register =
            oscillator;

        if ((oscillator &
             MCP2518FD_OSC_READY) != 0U) {

            s_info.oscillator_ready = true;

            return ESP_OK;
        }

        if ((xTaskGetTickCount() -
             started_at) >= timeout) {

            s_info.oscillator_ready = false;

            return ESP_ERR_TIMEOUT;
        }

        vTaskDelay(
            poll_delay
        );
    }
}

static void mcp2518fd_release_resources(void)
{
    mcp2518fd_invalidate_rx_fifo_address();

    (void)gpio_intr_disable(
        CAN_FD_PIN_INT
    );

    if (s_gpio_handler_registered) {
        (void)gpio_isr_handler_remove(
            CAN_FD_PIN_INT
        );

        s_gpio_handler_registered = false;
    }

    if (s_interrupt_task != NULL) {
        atomic_store(
            &s_interrupt_task_stop,
            true
        );

        xTaskNotifyGive(
            s_interrupt_task
        );

        const TickType_t started_at =
            xTaskGetTickCount();

        while (s_interrupt_task != NULL) {
            if ((xTaskGetTickCount() -
                started_at) >=
                pdMS_TO_TICKS(
                    MCP2518FD_INTERRUPT_STOP_TIMEOUT_MS
                )) {

                break;
            }

            vTaskDelay(1U);
        }
    }

    if (s_interrupt_task != NULL) {
        ESP_LOGW(
            TAG,
            "MCP2518FD interrupt task did not stop in time"
        );

        vTaskDelete(
            s_interrupt_task
        );

        s_interrupt_task = NULL;
    }

    if (s_spi_device != NULL) {
        (void)spi_bus_remove_device(
            s_spi_device
        );

        s_spi_device = NULL;
    }

    if (s_spi_bus_owned) {
        (void)spi_bus_free(
            CAN_FD_SPI_HOST
        );

        s_spi_bus_owned = false;
    }

    (void)gpio_reset_pin(
        CAN_FD_PIN_INT
    );

    if (s_tx_event_queue != NULL) {
        vQueueDelete(
            s_tx_event_queue
        );

        s_tx_event_queue = NULL;
    }

    if (s_rx_ready_semaphore != NULL) {
        vSemaphoreDelete(
            s_rx_ready_semaphore
        );

        s_rx_ready_semaphore = NULL;
    }

    if (s_mutex != NULL) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }

    memset(&s_config, 0, sizeof(s_config));
    memset(&s_info, 0, sizeof(s_info));

    s_initialized = false;
    s_started = false;

    s_next_tx_sequence = 0U;
    s_pending_tx_frames = 0U;

}

static esp_err_t mcp2518fd_abort_transmissions_unlocked(
    uint32_t timeout_ms
)
{
    uint32_t control = 0U;

    esp_err_t result =
        mcp2518fd_read_register_unlocked(
            MCP2518FD_REGISTER_CICON,
            &control
        );

    if (result != ESP_OK) {
        return result;
    }

    /*
     * Signal every transmit FIFO and TX Queue to abort all pending
     * transmissions. A frame that has already transmitted SOF cannot
     * be aborted and is allowed to finish.
     */
    control |=
        MCP2518FD_CICON_ABAT;

    result =
        mcp2518fd_write_register_unlocked(
            MCP2518FD_REGISTER_CICON,
            control
        );

    if (result != ESP_OK) {
        return result;
    }

    TickType_t timeout_ticks =
        pdMS_TO_TICKS(
            timeout_ms
        );

    if ((timeout_ms > 0U) &&
        (timeout_ticks == 0U)) {

        timeout_ticks = 1U;
    }

    const TickType_t started_at =
        xTaskGetTickCount();

    while (true) {
        uint32_t transmit_requests = 0U;

        result =
            mcp2518fd_read_register_unlocked(
                MCP2518FD_REGISTER_CITXREQ,
                &transmit_requests
            );

        if (result != ESP_OK) {
            break;
        }

        if (transmit_requests == 0U) {
            break;
        }

        if ((timeout_ms == 0U) ||
            ((xTaskGetTickCount() -
              started_at) >= timeout_ticks)) {

            result = ESP_ERR_TIMEOUT;
            break;
        }

        vTaskDelay(1U);
    }

    /*
     * ABAT must be clear before the controller can accept new
     * transmission requests. Read CiCON again because the controller
     * may have modified ABAT while processing the abort request.
     */
    uint32_t current_control = 0U;

    const esp_err_t read_result =
        mcp2518fd_read_register_unlocked(
            MCP2518FD_REGISTER_CICON,
            &current_control
        );

    if (read_result != ESP_OK) {
        return read_result;
    }

    current_control &=
        ~MCP2518FD_CICON_ABAT;

    const esp_err_t clear_result =
        mcp2518fd_write_register_unlocked(
            MCP2518FD_REGISTER_CICON,
            current_control
        );

    if (clear_result != ESP_OK) {
        return clear_result;
    }

    if (result != ESP_OK) {
        return result;
    }

    /*
     * Some frames may have completed successfully before ABAT took
     * effect. Consume their TEF entries before discarding the
     * remaining pending software state.
     */
    result =
        mcp2518fd_process_tef_unlocked();

    if (result != ESP_OK) {
        return result;
    }

    /*
     * Reset FIFO1 so aborted objects cannot remain at the current FIFO
     * index and affect subsequent transmissions.
     */
    uint32_t fifo_control = 0U;

    result =
        mcp2518fd_read_register_unlocked(
            MCP2518FD_REGISTER_CIFIFOCON1,
            &fifo_control
        );

    if (result != ESP_OK) {
        return result;
    }

    fifo_control |=
        MCP2518FD_FIFO_FRESET;

    result =
        mcp2518fd_write_register_unlocked(
            MCP2518FD_REGISTER_CIFIFOCON1,
            fifo_control
        );

    if (result != ESP_OK) {
        return result;
    }

    s_pending_tx_frames = 0U;

    return ESP_OK;
}

esp_err_t can_fd_mcp2518fd_driver_abort_transmissions(
    void
)
{
    if (!s_initialized || !s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t lock_result =
        mcp2518fd_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    /*
     * stop() may have completed between the initial state check and
     * mutex acquisition.
     */
    if (!s_started) {
        mcp2518fd_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    const uint32_t pending_before =
        s_pending_tx_frames;

    const esp_err_t result =
        mcp2518fd_abort_transmissions_unlocked(
            MCP2518FD_ABORT_TIMEOUT_MS
        );

    mcp2518fd_unlock();

    if (result == ESP_OK) {
        ESP_LOGI(
            TAG,
            "MCP2518FD transmissions aborted: pending=%lu",
            (unsigned long)pending_before
        );
    } else {
        ESP_LOGW(
            TAG,
            "Failed to abort MCP2518FD transmissions: %s",
            esp_err_to_name(result)
        );
    }

    return result;
}

esp_err_t can_fd_mcp2518fd_driver_reconfigure(
    const can_fd_mcp2518fd_runtime_config_t *config
)
{
    if (!mcp2518fd_runtime_config_is_valid(config)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized || !s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t lock_result =
        mcp2518fd_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    if (!s_started) {
        mcp2518fd_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    can_fd_mcp2518fd_config_t candidate =
        s_config;

    candidate.nominal_bitrate =
        config->nominal_bitrate;

    candidate.mode =
        config->mode;

    candidate.retransmission =
        config->retransmission;

    candidate.data_bitrate =
        config->data_bitrate;

    candidate.fd_enabled =
        config->fd_enabled;

    candidate.brs_enabled =
        config->brs_enabled;

    if (!mcp2518fd_config_is_valid(
            &candidate
        )) {

        mcp2518fd_unlock();
        return ESP_ERR_INVALID_ARG;
    }

    mcp2518fd_nominal_timing_t new_timing = {0};
    mcp2518fd_data_timing_t new_data_timing = {0};

    if (!mcp2518fd_calculate_nominal_timing(
            candidate.oscillator_hz,
            candidate.nominal_bitrate,
            &new_timing
        )) {

        mcp2518fd_unlock();
        return ESP_ERR_NOT_SUPPORTED;
    }

    if (!mcp2518fd_calculate_data_timing(
            candidate.oscillator_hz,
            candidate.data_bitrate,
            &new_data_timing
        )) {

        mcp2518fd_unlock();
        return ESP_ERR_NOT_SUPPORTED;
    }

    const can_fd_mcp2518fd_config_t previous =
        s_config;

    mcp2518fd_nominal_timing_t previous_timing = {0};
    mcp2518fd_data_timing_t previous_data_timing = {0};

    if (!mcp2518fd_calculate_nominal_timing(
            previous.oscillator_hz,
            previous.nominal_bitrate,
            &previous_timing
        )) {

        mcp2518fd_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    if (!mcp2518fd_calculate_data_timing(
            previous.oscillator_hz,
            previous.data_bitrate,
            &previous_data_timing
        )) {

        mcp2518fd_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    (void)gpio_intr_disable(
        CAN_FD_PIN_INT
    );

    esp_err_t result =
        mcp2518fd_abort_transmissions_unlocked(
            MCP2518FD_ABORT_TIMEOUT_MS
        );

    if ((result != ESP_OK) &&
        (result != ESP_ERR_TIMEOUT)) {

        (void)gpio_intr_enable(
            CAN_FD_PIN_INT
        );

        mcp2518fd_unlock();
        return result;
    }

    result =
        mcp2518fd_request_mode_unlocked(
            MCP2518FD_OPERATION_MODE_CONFIG
        );

    if (result == ESP_OK) {
        result =
            mcp2518fd_apply_runtime_config_unlocked(
                &candidate,
                &new_timing,
                &new_data_timing
            );
    }

    if (result == ESP_OK) {
        result =
            gpio_intr_enable(
                CAN_FD_PIN_INT
            );
    }

    if (result == ESP_OK) {
        s_pending_tx_frames = 0U;
        s_next_tx_sequence = 0U;

        if (s_tx_event_queue != NULL) {
            (void)xQueueReset(
                s_tx_event_queue
            );
        }

        s_info.last_transmit_sequence = 0U;
        s_info.last_transmit_timestamp = 0U;
        s_info.last_transmit_timestamp_valid = false;

        mcp2518fd_update_runtime_info(
            &new_timing,
            &new_data_timing
        );

        mcp2518fd_unlock();

        ESP_LOGI(
            TAG,
            "MCP2518FD reconfigured: nominal=%lu, data=%lu, "
            "FD=%s, BRS=%s, mode=%u, retransmission=%s",
            (unsigned long)new_timing.bitrate,
            (unsigned long)new_data_timing.bitrate,
            candidate.fd_enabled
                ? "enabled"
                : "disabled",
            candidate.brs_enabled
                ? "enabled"
                : "disabled",
            (unsigned int)candidate.mode,
            mcp2518fd_retransmission_name(
                candidate.retransmission
            )
        );

        return ESP_OK;
    }

    const esp_err_t original_error =
        result;

    ESP_LOGW(
        TAG,
        "Failed to apply runtime configuration: %s; "
        "restoring previous configuration",
        esp_err_to_name(original_error)
    );

    /*
     * Return to Configuration mode before rollback. This is harmless
     * if the controller is already there.
     */
    esp_err_t restore_result =
        mcp2518fd_request_mode_unlocked(
            MCP2518FD_OPERATION_MODE_CONFIG
        );

    if (restore_result == ESP_OK) {
        restore_result =
            mcp2518fd_apply_runtime_config_unlocked(
                &previous,
                &previous_timing,
                &previous_data_timing
            );
    }

    if (restore_result == ESP_OK) {
        const esp_err_t interrupt_result =
            gpio_intr_enable(
                CAN_FD_PIN_INT
            );

        if (interrupt_result == ESP_OK) {
            s_pending_tx_frames = 0U;
            s_next_tx_sequence = 0U;

            if (s_tx_event_queue != NULL) {
                (void)xQueueReset(
                    s_tx_event_queue
                );
            }

            s_info.last_transmit_sequence = 0U;
            s_info.last_transmit_timestamp = 0U;
            s_info.last_transmit_timestamp_valid = false;

            mcp2518fd_update_runtime_info(
                &previous_timing,
                &previous_data_timing
            );

            mcp2518fd_unlock();

            return original_error;
        }
    }

    /*
     * Hardware state is no longer known. Leave the interrupt disabled
     * and expose the driver as stopped instead of pretending it is
     * operational.
     */
    s_started = false;
    s_info.started = false;
    s_info.state =
        CAN_FD_MCP2518FD_STATE_UNKNOWN;

    mcp2518fd_unlock();

    ESP_LOGE(
        TAG,
        "Failed to restore previous MCP2518FD configuration"
    );

    return ESP_FAIL;
}

static bool mcp2518fd_filters_are_available(void)
{
    return s_initialized &&
           s_started;
}

esp_err_t can_fd_mcp2518fd_driver_set_filter(
    const can_fd_mcp2518fd_filter_t *filter
)
{
    if (!mcp2518fd_filter_is_valid(filter)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!mcp2518fd_filters_are_available()) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t lock_result =
        mcp2518fd_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    if (!s_started) {
        mcp2518fd_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t result =
        mcp2518fd_set_filter_unlocked(
            filter
        );

    mcp2518fd_unlock();

    if (result == ESP_OK) {
        ESP_LOGI(
            TAG,
            "Acceptance Filter %u configured: "
            "ID=0x%08lX, mask=0x%08lX, format=%s",
            (unsigned int)filter->index,
            (unsigned long)filter->identifier,
            (unsigned long)filter->mask,
            filter->extended
                ? "extended"
                : "standard"
        );
    }

    return result;
}

esp_err_t can_fd_mcp2518fd_driver_disable_filter(
    uint8_t filter_index
)
{
    if (filter_index >=
        CAN_FD_MCP2518FD_FILTER_COUNT) {

        return ESP_ERR_INVALID_ARG;
    }

    if (!mcp2518fd_filters_are_available()) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t lock_result =
        mcp2518fd_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    if (!s_started) {
        mcp2518fd_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t result =
        mcp2518fd_set_filter_control_unlocked(
            filter_index,
            false
        );

    mcp2518fd_unlock();

    return result;
}

esp_err_t can_fd_mcp2518fd_driver_disable_all_filters(void)
{
    if (!mcp2518fd_filters_are_available()) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t lock_result =
        mcp2518fd_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    if (!s_started) {
        mcp2518fd_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t result =
        mcp2518fd_disable_all_filters_unlocked();

    mcp2518fd_unlock();

    return result;
}

esp_err_t can_fd_mcp2518fd_driver_accept_all(void)
{
    if (!mcp2518fd_filters_are_available()) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t lock_result =
        mcp2518fd_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    if (!s_started) {
        mcp2518fd_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t result =
        mcp2518fd_accept_all_unlocked();

    mcp2518fd_unlock();

    return result;
}

esp_err_t can_fd_mcp2518fd_driver_init(
    const can_fd_mcp2518fd_config_t *config
)
{
    if (!mcp2518fd_config_is_valid(config)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    s_mutex = xSemaphoreCreateMutex();

    if (s_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_rx_ready_semaphore =
        xSemaphoreCreateBinary();

    if (s_rx_ready_semaphore == NULL) {
        vSemaphoreDelete(
            s_mutex
        );

        s_mutex = NULL;

        return ESP_ERR_NO_MEM;
    }

    s_tx_event_queue =
        xQueueCreate(
            config->tx_fifo_depth,
            sizeof(can_fd_mcp2518fd_tx_event_t)
        );

    if (s_tx_event_queue == NULL) {
        mcp2518fd_release_resources();
        return ESP_ERR_NO_MEM;
    }

    const spi_bus_config_t bus_config = {
        .mosi_io_num = CAN_FD_PIN_MOSI,
        .miso_io_num = CAN_FD_PIN_MISO,
        .sclk_io_num = CAN_FD_PIN_SCLK,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = 80,
    };

    esp_err_t result =
        spi_bus_initialize(
            CAN_FD_SPI_HOST,
            &bus_config,
            SPI_DMA_CH_AUTO
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to initialize SPI bus: %s",
            esp_err_to_name(result)
        );

        mcp2518fd_release_resources();
        return result;
    }

    s_spi_bus_owned = true;

    const spi_device_interface_config_t device_config = {
        .clock_speed_hz = CAN_FD_SPI_CLOCK_HZ,
        .mode = 0,
        .spics_io_num = CAN_FD_PIN_CS,
        .queue_size = 1,
        .command_bits = 0,
        .address_bits = 0,
        .dummy_bits = 0,
    };

    result =
        spi_bus_add_device(
            CAN_FD_SPI_HOST,
            &device_config,
            &s_spi_device
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to add MCP2518FD SPI device: %s",
            esp_err_to_name(result)
        );

        mcp2518fd_release_resources();
        return result;
    }

    const gpio_config_t interrupt_config = {
        .pin_bit_mask = 1ULL << CAN_FD_PIN_INT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };

    result = gpio_config(&interrupt_config);

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to configure MCP2518FD interrupt pin: %s",
            esp_err_to_name(result)
        );

        mcp2518fd_release_resources();
        return result;
    }

    result =
        gpio_intr_disable(
            CAN_FD_PIN_INT
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to disable MCP2518FD GPIO interrupt: %s",
            esp_err_to_name(result)
        );

        mcp2518fd_release_resources();
        return result;
    }

    atomic_store(
        &s_interrupt_task_stop,
        false
    );

    const BaseType_t task_result =
        xTaskCreate(
            mcp2518fd_interrupt_task,
            "mcp2518fd_irq",
            MCP2518FD_INTERRUPT_TASK_STACK_SIZE,
            NULL,
            MCP2518FD_INTERRUPT_TASK_PRIORITY,
            &s_interrupt_task
        );

    if (task_result != pdPASS) {
        mcp2518fd_release_resources();
        return ESP_ERR_NO_MEM;
    }

    result =
        board_gpio_isr_service_init();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Shared GPIO ISR service is unavailable: %s",
            esp_err_to_name(result)
        );

        mcp2518fd_release_resources();
        return result;
    }

    result =
        gpio_isr_handler_add(
            CAN_FD_PIN_INT,
            mcp2518fd_gpio_interrupt,
            NULL
        );

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to register MCP2518FD GPIO interrupt handler: %s",
            esp_err_to_name(result)
        );

        mcp2518fd_release_resources();
        return result;
    }

    s_gpio_handler_registered = true;

    s_config = *config;

    result = mcp2518fd_reset();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "MCP2518FD reset failed: %s",
            esp_err_to_name(result)
        );

        mcp2518fd_release_resources();
        return result;
    }

    result = mcp2518fd_wait_for_oscillator();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "MCP2518FD oscillator is not ready: %s, OSC=0x%08lX",
            esp_err_to_name(result),
            (unsigned long)s_info.oscillator_register
        );

        mcp2518fd_release_resources();
        return result == ESP_ERR_TIMEOUT
            ? ESP_ERR_NOT_FOUND
            : result;
    }

    uint32_t device_id = 0U;

    result =
        mcp2518fd_read_register_unlocked(
            MCP2518FD_REGISTER_DEVID,
            &device_id
        );

    if (result != ESP_OK) {
        mcp2518fd_release_resources();
        return result;
    }

    /*
     * Verify that writable OSC configuration bits can be read back.
     * This is more reliable than requiring a particular DEVID revision.
     */
    const uint32_t oscillator_before =
        s_info.oscillator_register;

    result =
        mcp2518fd_write_register_unlocked(
            MCP2518FD_REGISTER_OSC,
            oscillator_before &
            MCP2518FD_OSC_CONFIGURATION_MASK
        );

    if (result == ESP_OK) {
        uint32_t oscillator_after = 0U;

        result =
            mcp2518fd_read_register_unlocked(
                MCP2518FD_REGISTER_OSC,
                &oscillator_after
            );

        if (result == ESP_OK) {
            s_info.oscillator_register =
                oscillator_after;

            if ((oscillator_after &
                 MCP2518FD_OSC_READY) == 0U) {

                result =
                    mcp2518fd_wait_for_oscillator();
            }
        }
    }

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "MCP2518FD SPI verification failed: %s",
            esp_err_to_name(result)
        );

        mcp2518fd_release_resources();
        return result;
    }

    s_info.initialized = true;
    s_info.started = false;
    s_info.controller_detected = true;
    s_info.oscillator_ready = true;
    s_info.fd_enabled =
        config->fd_enabled;

    s_info.brs_enabled =
        config->brs_enabled;

    s_info.state =
        CAN_FD_MCP2518FD_STATE_STOPPED;

    s_info.mode =
        config->mode;

    s_info.oscillator_hz =
        config->oscillator_hz;

    s_info.nominal_bitrate =
        config->nominal_bitrate;

    s_info.data_bitrate =
        config->data_bitrate;

    s_initialized = true;

    ESP_LOGI(
        TAG,
        "MCP2518FD detected: DEVID=0x%02lX, OSC=0x%08lX, crystal=%lu Hz",
        (unsigned long)(device_id & 0xFFU),
        (unsigned long)s_info.oscillator_register,
        (unsigned long)s_info.oscillator_hz
    );

    return ESP_OK;
}

esp_err_t can_fd_mcp2518fd_driver_start(void)
{
    if (!s_initialized || s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t lock_result =
        mcp2518fd_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    if (s_tx_event_queue == NULL) {
        mcp2518fd_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    (void)xQueueReset(
        s_tx_event_queue
    );

    s_info.dropped_tx_events = 0U;
    s_info.last_transmit_timestamp = 0U;
    s_info.last_transmit_timestamp_valid = false;
    s_info.last_transmit_sequence = 0U;

    esp_err_t result = ESP_OK;

    mcp2518fd_nominal_timing_t timing = {0};
    mcp2518fd_data_timing_t data_timing = {0};

    result =
        mcp2518fd_request_mode_unlocked(
            MCP2518FD_OPERATION_MODE_CONFIG
        );

    if (result == ESP_OK) {
        result =
            mcp2518fd_configure_system_clock_unlocked();
    }

    if (result == ESP_OK) {
        if (!mcp2518fd_calculate_nominal_timing(
                s_config.oscillator_hz,
                s_config.nominal_bitrate,
                &timing
            )) {

            result =
                ESP_ERR_NOT_SUPPORTED;
        }
    }

    if (result == ESP_OK) {
        if (!mcp2518fd_calculate_data_timing(
                s_config.oscillator_hz,
                s_config.data_bitrate,
                &data_timing
            )) {

            result =
                ESP_ERR_NOT_SUPPORTED;
        }
    }

    if (result == ESP_OK) {
        result =
            mcp2518fd_configure_nominal_timing_unlocked(
                &timing
            );
    }

    if (result == ESP_OK) {
        result =
            mcp2518fd_configure_data_timing_unlocked(
                &data_timing
            );
    }

    if (result == ESP_OK) {
        result =
            mcp2518fd_configure_tdc_unlocked(
                &data_timing,
                s_config.fd_enabled &&
                s_config.brs_enabled
            );
    }

    /*
     * Enable ECC and initialize all 2 KB of Message RAM before FIFO
     * objects are allocated.
     */
    if (result == ESP_OK) {
        result =
            mcp2518fd_initialize_message_ram_unlocked();
    }

    if (result == ESP_OK) {
        result =
            mcp2518fd_configure_timestamp_unlocked();
    }

    if (result == ESP_OK) {
        result =
            mcp2518fd_configure_tef_unlocked();
    }

    if (result == ESP_OK) {
        result =
            mcp2518fd_configure_fifos_unlocked();
    }

    if (result == ESP_OK) {
        result =
            mcp2518fd_configure_acceptance_filter_unlocked();
    }

    if (result == ESP_OK) {
        result =
            mcp2518fd_configure_protocol_unlocked();
    }

    /*
     * Configure controller interrupt sources while still in Configuration
     * mode.
     */
    if (result == ESP_OK) {
        result =
            mcp2518fd_enable_interrupts_unlocked();
    }

    /*
     * Leave Configuration mode after all controller configuration is
     * complete.
     */
    if (result == ESP_OK) {
        result =
            mcp2518fd_request_mode_unlocked(
                mcp2518fd_get_requested_operation_mode()
            );
    }

    if (result == ESP_OK) {
        result =
            gpio_intr_enable(
                CAN_FD_PIN_INT
            );
    }

    if (result == ESP_OK) {
        s_pending_tx_frames = 0U;
        s_next_tx_sequence = 0U;

        s_started = true;

        ESP_LOGI(
            TAG,
            "Retransmission policy: %s",
            mcp2518fd_retransmission_name(
                s_config.retransmission
            )
        );

        s_info.retransmission =
            s_config.retransmission;

        s_info.started = true;
        s_info.state =
            CAN_FD_MCP2518FD_STATE_ERROR_ACTIVE;

        s_info.nominal_bitrate =
            timing.bitrate;

        s_info.data_bitrate =
            data_timing.bitrate;

        s_info.data_sample_point_permill =
            data_timing.sample_point_permill;

        s_info.data_brp =
            data_timing.brp;

        s_info.data_tseg1 =
            data_timing.tseg1;

        s_info.data_tseg2 =
            data_timing.tseg2;

        s_info.data_sjw =
            data_timing.sjw;

        s_info.tdc_enabled =
            s_config.fd_enabled &&
            s_config.brs_enabled;

        s_info.tdc_offset =
            data_timing.tdc_offset;
    } else {
        (void)gpio_intr_disable(
            CAN_FD_PIN_INT
        );

        (void)mcp2518fd_write_register_unlocked(
            MCP2518FD_REGISTER_CIINT,
            0U
        );

        (void)mcp2518fd_request_mode_unlocked(
            MCP2518FD_OPERATION_MODE_CONFIG
        );

        s_pending_tx_frames = 0U;

        s_started = false;

        s_info.started = false;
        s_info.state =
            CAN_FD_MCP2518FD_STATE_STOPPED;
    }

    if (result == ESP_OK) {
        memset(
            &s_profile,
            0,
            sizeof(s_profile)
        );
    }

    mcp2518fd_unlock();

    if (result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to start MCP2518FD: %s",
            esp_err_to_name(result)
        );

        return result;
    }

    ESP_LOGI(
        TAG,
        "MCP2518FD started: nominal=%lu bit/s, "
        "sample=%u.%u%%, BRP=%u, TSEG1=%u, "
        "TSEG2=%u, SJW=%u, mode=%u, "
        "TX FIFO1=%u, RX FIFO2=%u",
        (unsigned long)timing.bitrate,
        (unsigned int)(
            timing.sample_point_permill / 10U
        ),
        (unsigned int)(
            timing.sample_point_permill % 10U
        ),
        (unsigned int)timing.brp,
        (unsigned int)timing.tseg1,
        (unsigned int)timing.tseg2,
        (unsigned int)timing.sjw,
        (unsigned int)s_config.mode,
        (unsigned int)s_config.tx_fifo_depth,
        (unsigned int)s_config.rx_fifo_depth
    );

    return ESP_OK;
}

esp_err_t can_fd_mcp2518fd_driver_stop(void)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_started) {
        return ESP_OK;
    }

    const esp_err_t lock_result =
        mcp2518fd_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    if (!s_started) {
        mcp2518fd_unlock();
        return ESP_OK;
    }

    esp_err_t result =
        mcp2518fd_abort_transmissions_unlocked(
            MCP2518FD_ABORT_TIMEOUT_MS
        );

    if (result == ESP_ERR_TIMEOUT) {
        ESP_LOGW(
            TAG,
            "Timed out while aborting pending transmissions"
        );
    } else if (result != ESP_OK) {
        mcp2518fd_unlock();
        return result;
    }

    const esp_err_t mode_result =
        mcp2518fd_request_mode_unlocked(
            MCP2518FD_OPERATION_MODE_CONFIG
        );

    if (mode_result != ESP_OK) {
        mcp2518fd_unlock();
        return mode_result;
    }

    (void)gpio_intr_disable(
        CAN_FD_PIN_INT
    );

    (void)mcp2518fd_write_register_unlocked(
        MCP2518FD_REGISTER_CIINT,
        0U
    );

    s_pending_tx_frames = 0U;

    s_started = false;
    s_info.started = false;
    s_info.state =
        CAN_FD_MCP2518FD_STATE_STOPPED;

    mcp2518fd_invalidate_rx_fifo_address();

    mcp2518fd_unlock();

    if (s_rx_ready_semaphore != NULL) {
        (void)xSemaphoreGive(
            s_rx_ready_semaphore
        );
    }

    return ESP_OK;
}

esp_err_t can_fd_mcp2518fd_driver_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    esp_err_t result = ESP_OK;

    if (s_started) {
        result =
            can_fd_mcp2518fd_driver_stop();

        if (result != ESP_OK) {
            return result;
        }
    }

    mcp2518fd_invalidate_rx_fifo_address();

    mcp2518fd_release_resources();

    return ESP_OK;
}

static void mcp2518fd_set_tx_sequence(
    uint8_t *object,
    uint32_t sequence
)
{
    if (object == NULL) {
        return;
    }

    uint32_t control_word =
        mcp2518fd_read_uint32(
            &object[4]
        );

    control_word &=
        ~(
            MCP2518FD_SEQUENCE_MASK <<
            MCP2518FD_SEQUENCE_SHIFT
        );

    control_word |=
        (
            sequence &
            MCP2518FD_SEQUENCE_MASK
        ) <<
        MCP2518FD_SEQUENCE_SHIFT;

    mcp2518fd_write_uint32(
        &object[4],
        control_word
    );
}

static esp_err_t mcp2518fd_transmit_internal(
    const can_fd_mcp2518fd_frame_t *frame,
    uint32_t timeout_ms,
    uint32_t *out_sequence
)
{
    if (frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!mcp2518fd_transmit_frame_is_valid(frame)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized || !s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * Listen-only and Restricted Operation modes do not permit
     * application frame transmission.
     */
    if (!mcp2518fd_mode_allows_transmission(
            s_config.mode
        )) {

        return ESP_ERR_INVALID_STATE;
    }

    uint8_t object[
        MCP2518FD_TX_OBJECT_MAX_SIZE
    ] = {0};

    esp_err_t result = ESP_OK;

    TickType_t timeout_ticks =
        pdMS_TO_TICKS(
            timeout_ms
        );

    if ((timeout_ms > 0U) &&
        (timeout_ticks == 0U)) {

        timeout_ticks = 1U;
    }

    const TickType_t started_at =
        xTaskGetTickCount();

    while (true) {
        const esp_err_t lock_result =
            mcp2518fd_lock();

        if (lock_result != ESP_OK) {
            return lock_result;
        }

        if (!s_started ||
            !mcp2518fd_mode_allows_transmission(
                s_config.mode
            )) {

            mcp2518fd_unlock();
            return ESP_ERR_INVALID_STATE;
        }

        const size_t object_size =
            mcp2518fd_tx_object_size(
                &s_config
            );

        result =
            mcp2518fd_encode_tx_object(
                frame,
                object,
                object_size
            );

        if (result != ESP_OK) {
            ++s_info.transmit_failures;

            mcp2518fd_unlock();
            return result;
        }

        uint32_t fifo_status = 0U;

        result =
            mcp2518fd_read_register_unlocked(
                MCP2518FD_REGISTER_CIFIFOSTA1,
                &fifo_status
            );

        if (result != ESP_OK) {
            ++s_info.transmit_failures;

            mcp2518fd_unlock();
            return result;
        }

        /*
         * For a TX FIFO, TFNRFNIF means that at least one object is
         * available for writing.
         */
        const bool space_available =
            (fifo_status &
             MCP2518FD_FIFO_STATUS_TFNRFNIF) != 0U;

        if (!space_available) {
            mcp2518fd_unlock();

            if (timeout_ms == 0U) {
                return ESP_ERR_TIMEOUT;
            }

            if ((xTaskGetTickCount() -
                 started_at) >=
                timeout_ticks) {

                const esp_err_t retry_lock =
                    mcp2518fd_lock();

                if (retry_lock == ESP_OK) {
                    ++s_info.transmit_failures;
                    mcp2518fd_unlock();
                }

                return ESP_ERR_TIMEOUT;
            }

            vTaskDelay(1U);
            continue;
        }

        const uint32_t sequence =
            s_next_tx_sequence &
            MCP2518FD_SEQUENCE_MASK;

        mcp2518fd_set_tx_sequence(
            object,
            sequence
        );

        uint32_t user_address = 0U;

        result =
            mcp2518fd_read_register_unlocked(
                MCP2518FD_REGISTER_CIFIFOUA1,
                &user_address
            );

        if (result != ESP_OK) {
            ++s_info.transmit_failures;

            mcp2518fd_unlock();
            return result;
        }

        const uint16_t ram_address =
            (uint16_t)(
                MCP2518FD_MESSAGE_RAM_BASE +
                (
                    user_address &
                    MCP2518FD_MESSAGE_RAM_OFFSET_MASK
                )
            );

        if (((uint32_t)ram_address +
            object_size) >
            (
                MCP2518FD_MESSAGE_RAM_BASE +
                MCP2518FD_MESSAGE_RAM_SIZE
            )) {

            ++s_info.transmit_failures;

            mcp2518fd_unlock();

            ESP_LOGE(
                TAG,
                "Invalid TX FIFO address: UA=0x%08lX",
                (unsigned long)user_address
            );

            return ESP_ERR_INVALID_RESPONSE;
        }

        result =
            mcp2518fd_write_bytes_unlocked(
                ram_address,
                object,
                object_size
            );

        if (result == ESP_OK) {
            result =
                mcp2518fd_request_tx_unlocked();
        }

        if (result == ESP_OK) {
            ++s_pending_tx_frames;

            if (out_sequence != NULL) {
                *out_sequence =
                    sequence;
            }

            s_next_tx_sequence =
                (s_next_tx_sequence + 1U) &
                MCP2518FD_SEQUENCE_MASK;
        } else {
            ++s_info.transmit_failures;
        }

        mcp2518fd_unlock();

        return result;
    }
}

esp_err_t can_fd_mcp2518fd_driver_transmit(
    const can_fd_mcp2518fd_frame_t *frame,
    uint32_t timeout_ms
)
{
    return mcp2518fd_transmit_internal(
        frame,
        timeout_ms,
        NULL
    );
}

esp_err_t can_fd_mcp2518fd_driver_transmit_tracked(
    const can_fd_mcp2518fd_frame_t *frame,
    uint32_t timeout_ms,
    uint32_t *sequence
)
{
    if (sequence == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * Do not expose an old sequence when transmission fails.
     */
    *sequence = 0U;

    return mcp2518fd_transmit_internal(
        frame,
        timeout_ms,
        sequence
    );
}

static esp_err_t mcp2518fd_read_tef_event_unlocked(
    uint32_t *sequence,
    uint32_t *timestamp
)
{
    if ((sequence == NULL) ||
        (timestamp == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    uint32_t user_address = 0U;

    esp_err_t result =
        mcp2518fd_read_register_unlocked(
            MCP2518FD_REGISTER_CITEFUA,
            &user_address
        );

    if (result != ESP_OK) {
        return result;
    }

    const uint16_t ram_address =
        (uint16_t)(
            MCP2518FD_MESSAGE_RAM_BASE +
            (
                user_address &
                MCP2518FD_MESSAGE_RAM_OFFSET_MASK
            )
        );

    if (((uint32_t)ram_address +
         MCP2518FD_TEF_OBJECT_SIZE) >
        (
            MCP2518FD_MESSAGE_RAM_BASE +
            MCP2518FD_MESSAGE_RAM_SIZE
        )) {

        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t event_object[
        MCP2518FD_TEF_OBJECT_SIZE
    ] = {0};

    result =
        mcp2518fd_read_bytes_unlocked(
            ram_address,
            event_object,
            sizeof(event_object)
        );

    if (result != ESP_OK) {
        return result;
    }

    const uint32_t control_word =
        mcp2518fd_read_uint32(
            &event_object[4]
        );

    *sequence =
        (
            control_word >>
            MCP2518FD_SEQUENCE_SHIFT
        ) &
        MCP2518FD_SEQUENCE_MASK;

    *timestamp =
        mcp2518fd_read_uint32(
            &event_object[
                MCP2518FD_TEF_TIMESTAMP_OFFSET
            ]
        );

    uint32_t tef_control = 0U;

    result =
        mcp2518fd_read_register_unlocked(
            MCP2518FD_REGISTER_CITEFCON,
            &tef_control
        );

    if (result != ESP_OK) {
        return result;
    }

    tef_control |=
        MCP2518FD_TEF_UINC;

    return mcp2518fd_write_register_unlocked(
        MCP2518FD_REGISTER_CITEFCON,
        tef_control
    );
}

static esp_err_t mcp2518fd_process_tef_unlocked(void)
{
    while (true) {
        uint32_t tef_status = 0U;

        esp_err_t result =
            mcp2518fd_read_register_unlocked(
                MCP2518FD_REGISTER_CITEFSTA,
                &tef_status
            );

        if (result != ESP_OK) {
            return result;
        }

        if ((tef_status &
            MCP2518FD_TEF_STATUS_TEFOVIF) != 0U) {

            ++s_info.transmit_event_overflow_count;

            /*
             * One or more successful-transmission events were lost, so the
             * software pending counter can no longer be reconciled with TEF.
             */
            s_pending_tx_frames = 0U;

            ESP_LOGW(
                TAG,
                "Transmit Event FIFO overflow"
            );

            result =
                mcp2518fd_write_register_unlocked(
                    MCP2518FD_REGISTER_CITEFSTA,
                    tef_status &
                    ~MCP2518FD_TEF_STATUS_TEFOVIF
                );

            if (result != ESP_OK) {
                return result;
            }
        }

        if ((tef_status &
             MCP2518FD_TEF_STATUS_TEFNEIF) == 0U) {

            return ESP_OK;
        }

        uint32_t sequence = 0U;
        uint32_t timestamp = 0U;

        result =
            mcp2518fd_read_tef_event_unlocked(
                &sequence,
                &timestamp
            );

        if (result != ESP_OK) {
            return result;
        }

        s_info.last_transmit_sequence =
            sequence;

        s_info.last_transmit_timestamp =
            timestamp;

        s_info.last_transmit_timestamp_valid =
            true;

        const can_fd_mcp2518fd_tx_event_t event = {
            .sequence = sequence,
            .timestamp = timestamp,
        };

        if ((s_tx_event_queue == NULL) ||
            (xQueueSend(
                s_tx_event_queue,
                &event,
                0U
            ) != pdTRUE)) {

            ++s_info.dropped_tx_events;

            const uint32_t dropped =
                s_info.dropped_tx_events;

            if ((dropped == 1U) ||
                ((dropped & (dropped - 1U)) == 0U)) {

                ESP_LOGW(
                    TAG,
                    "TX event queue overflow: dropped=%lu",
                    (unsigned long)dropped
                );
            }
        }

        ++s_info.transmitted_frames;

        if (s_pending_tx_frames > 0U) {
            --s_pending_tx_frames;
        }
    }
}

static esp_err_t mcp2518fd_get_rx_fifo_available_unlocked(
    size_t *available
)
{
    if (available == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *available = 0U;

    uint32_t fifo_status = 0U;

#if CAN_FD_MCP2518FD_ENABLE_PROFILING
    const int64_t status_started_at =
        esp_timer_get_time();
#endif

    esp_err_t result =
        mcp2518fd_read_register_unlocked(
            MCP2518FD_REGISTER_CIFIFOSTA2,
            &fifo_status
        );

#if CAN_FD_MCP2518FD_ENABLE_PROFILING
    s_profile.fifo_status_time_us +=
        mcp2518fd_elapsed_us(
            status_started_at
        );
#endif

    if (result != ESP_OK) {
        return result;
    }

    result =
        mcp2518fd_handle_rx_overflow_unlocked(
            fifo_status
        );

    if (result != ESP_OK) {
        return result;
    }

    if ((fifo_status &
         MCP2518FD_FIFO_STATUS_TFNRFNIF) == 0U) {

        return ESP_OK;
    }

    if (!s_rx_fifo_address_valid ||
        (s_config.rx_fifo_depth == 0U)) {

        return ESP_ERR_INVALID_STATE;
    }

    const size_t object_size =
        mcp2518fd_rx_object_size(
            &s_config
        );

    if ((object_size == 0U) ||
        (s_rx_fifo_next_address <
         s_rx_fifo_base_address) ||
        (s_rx_fifo_next_address >=
         s_rx_fifo_end_address)) {

        return ESP_ERR_INVALID_RESPONSE;
    }

    const size_t read_index =
        (
            (size_t)s_rx_fifo_next_address -
            (size_t)s_rx_fifo_base_address
        ) /
        object_size;

    const size_t write_index =
        (size_t)(
            (fifo_status &
             MCP2518FD_FIFO_STATUS_FIFOCI_MASK) >>
            MCP2518FD_FIFO_STATUS_FIFOCI_SHIFT
        );

    const size_t depth =
        (size_t)s_config.rx_fifo_depth;

    if ((read_index >= depth) ||
        (write_index >= depth)) {

        ESP_LOGE(
            TAG,
            "Invalid RX FIFO indices: read=%u, write=%u, depth=%u",
            (unsigned int)read_index,
            (unsigned int)write_index,
            (unsigned int)depth
        );

        return ESP_ERR_INVALID_RESPONSE;
    }

    /*
     * FIFOCI is the next hardware write index for an RX FIFO. The
     * software read index follows the cached UA address. Equal indices
     * mean either empty or full, which is disambiguated by TFERFFIF;
     * the earlier TFNRFNIF check already excludes the empty case.
     */
    if ((fifo_status &
         MCP2518FD_FIFO_STATUS_TFERFFIF) != 0U) {

        *available = depth;
    } else {
        *available =
            (write_index + depth - read_index) %
            depth;
    }

    if (*available == 0U) {
        ESP_LOGE(
            TAG,
            "Inconsistent RX FIFO status: "
            "status=0x%08lX, read=%u, write=%u, depth=%u",
            (unsigned long)fifo_status,
            (unsigned int)read_index,
            (unsigned int)write_index,
            (unsigned int)depth
        );

        return ESP_ERR_INVALID_RESPONSE;
    }

    return ESP_OK;
}

static esp_err_t mcp2518fd_receive_pending_unlocked(
    can_fd_mcp2518fd_frame_t *frame
)
{
    if (frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(
        frame,
        0,
        sizeof(*frame)
    );

    const size_t object_size =
        mcp2518fd_rx_object_size(
            &s_config
        );

    uint16_t ram_address = 0U;

    esp_err_t result =
        mcp2518fd_get_rx_fifo_address_unlocked(
            &ram_address
        );

    if (result != ESP_OK) {
        return result;
    }

    if (!s_rx_fifo_address_valid ||
        (ram_address <
        s_rx_fifo_base_address) ||
        (((uint32_t)ram_address +
        object_size) >
        s_rx_fifo_end_address)) {

        ESP_LOGE(
            TAG,
            "Invalid cached RX FIFO address: "
            "address=0x%03X, base=0x%03X, end=0x%03X",
            (unsigned int)ram_address,
            (unsigned int)s_rx_fifo_base_address,
            (unsigned int)s_rx_fifo_end_address
        );

        mcp2518fd_invalidate_rx_fifo_address();

        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t object[
        MCP2518FD_RX_OBJECT_MAX_SIZE
    ] = {0};

#if CAN_FD_MCP2518FD_ENABLE_PROFILING
    const int64_t object_started_at =
        esp_timer_get_time();
#endif

    result =
        mcp2518fd_read_bytes_unlocked(
            ram_address,
            object,
            object_size
        );

#if CAN_FD_MCP2518FD_ENABLE_PROFILING
    s_profile.object_read_time_us +=
        mcp2518fd_elapsed_us(
            object_started_at
        );
#endif

    if (result != ESP_OK) {
        ++s_info.dropped_rx_frames;
        return result;
    }

    /*
     * The FIFO object must be released only after its complete contents
     * have been copied from Message RAM.
     */
#if CAN_FD_MCP2518FD_ENABLE_PROFILING
    const int64_t increment_started_at =
        esp_timer_get_time();
#endif

    result =
        mcp2518fd_increment_rx_fifo_unlocked();

#if CAN_FD_MCP2518FD_ENABLE_PROFILING
    s_profile.fifo_increment_time_us +=
        mcp2518fd_elapsed_us(
            increment_started_at
        );
#endif

    if (result != ESP_OK) {
        ++s_info.dropped_rx_frames;
        return result;
    }

    mcp2518fd_advance_rx_fifo_address_unlocked();

#if CAN_FD_MCP2518FD_ENABLE_PROFILING
    const int64_t decode_started_at =
        esp_timer_get_time();
#endif

    result =
        mcp2518fd_decode_rx_object(
            object,
            frame
        );

#if CAN_FD_MCP2518FD_ENABLE_PROFILING
    s_profile.decode_time_us +=
        mcp2518fd_elapsed_us(
            decode_started_at
        );
#endif

    if (result == ESP_OK) {
        ++s_info.received_frames;

#if CAN_FD_MCP2518FD_ENABLE_PROFILING
        ++s_profile.received_frames;
#endif
    } else {
        ++s_info.dropped_rx_frames;
    }

    return result;
}

esp_err_t can_fd_mcp2518fd_driver_receive_batch(
    can_fd_mcp2518fd_frame_t *frames,
    size_t frame_capacity,
    size_t *received_count,
    uint32_t timeout_ms
)
{
    if ((frames == NULL) ||
        (frame_capacity == 0U) ||
        (received_count == NULL)) {

        return ESP_ERR_INVALID_ARG;
    }

    *received_count = 0U;

    if (!s_initialized ||
        !s_started) {

        return ESP_ERR_INVALID_STATE;
    }

    /*
     * A blocking receive waits for the interrupt task to report RX
     * activity before performing any SPI access. This prevents
     * periodic CiFIFOSTA2 polling while the CAN bus is idle.
     *
     * If INT is already active low, inspect the controller immediately.
     * This also covers a frame that arrived before the caller started
     * waiting.
     */
    if ((timeout_ms > 0U) &&
        (gpio_get_level(
            CAN_FD_PIN_INT
        ) != 0)) {

        TickType_t timeout_ticks =
            pdMS_TO_TICKS(
                timeout_ms
            );

        if (timeout_ticks == 0U) {
            timeout_ticks = 1U;
        }

        if ((s_rx_ready_semaphore == NULL) ||
            (xSemaphoreTake(
                s_rx_ready_semaphore,
                timeout_ticks
            ) != pdTRUE)) {

            return ESP_ERR_TIMEOUT;
        }
    }

    const esp_err_t lock_result =
        mcp2518fd_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    if (!s_started) {
        mcp2518fd_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    size_t available = 0U;

    esp_err_t result =
        mcp2518fd_get_rx_fifo_available_unlocked(
            &available
        );

    if (result == ESP_OK) {
        const size_t frames_to_receive =
            (available < frame_capacity)
                ? available
                : frame_capacity;

        for (size_t index = 0U;
             index < frames_to_receive;
             ++index) {

            result =
                mcp2518fd_receive_pending_unlocked(
                    &frames[*received_count]
                );

            if (result != ESP_OK) {
                break;
            }

            (*received_count)++;
        }
    }

    mcp2518fd_unlock();

    if (*received_count > 0U) {
        return ESP_OK;
    }

    return
        (result == ESP_OK)
            ? ESP_ERR_TIMEOUT
            : result;
}

esp_err_t can_fd_mcp2518fd_driver_receive(
    can_fd_mcp2518fd_frame_t *frame,
    uint32_t timeout_ms
)
{
    if (frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t received_count = 0U;

    const esp_err_t result =
        can_fd_mcp2518fd_driver_receive_batch(
            frame,
            1U,
            &received_count,
            timeout_ms
        );

    if (result != ESP_OK) {
        return result;
    }

    return
        (received_count == 1U)
            ? ESP_OK
            : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t can_fd_mcp2518fd_driver_recover(void)
{
    if (!s_initialized || !s_started) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t lock_result =
        mcp2518fd_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    /*
     * stop() may have completed between the initial state check and
     * mutex acquisition.
     */
    if (!s_started) {
        mcp2518fd_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result =
        mcp2518fd_update_error_state_unlocked();

    if (result != ESP_OK) {
        mcp2518fd_unlock();
        return result;
    }

    if (s_info.state !=
        CAN_FD_MCP2518FD_STATE_BUS_OFF) {

        mcp2518fd_unlock();
        return ESP_ERR_INVALID_STATE;
    }

    /*
     * MCP2518FD automatically resets all transmit FIFOs when entering
     * Bus-off. Therefore queued software transmissions can no longer
     * be matched with hardware FIFO objects.
     *
     * Hardware recovery is already running and completes after the
     * controller detects 128 occurrences of 11 consecutive recessive
     * bits.
     */
    s_pending_tx_frames = 0U;

    mcp2518fd_unlock();

    ESP_LOGI(
        TAG,
        "MCP2518FD Bus-off recovery is in progress"
    );

    return ESP_OK;
}

esp_err_t can_fd_mcp2518fd_driver_receive_tx_event(
    can_fd_mcp2518fd_tx_event_t *event,
    uint32_t timeout_ms
)
{
    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(
        event,
        0,
        sizeof(*event)
    );

    if (!s_initialized ||
        !s_started ||
        (s_tx_event_queue == NULL)) {

        return ESP_ERR_INVALID_STATE;
    }

    TickType_t timeout_ticks =
        pdMS_TO_TICKS(
            timeout_ms
        );

    if ((timeout_ms > 0U) &&
        (timeout_ticks == 0U)) {

        timeout_ticks = 1U;
    }

    if (xQueueReceive(
            s_tx_event_queue,
            event,
            timeout_ticks
        ) != pdTRUE) {

        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

esp_err_t can_fd_mcp2518fd_driver_get_info(
    can_fd_mcp2518fd_info_t *info
)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_initialized) {
        memset(info, 0, sizeof(*info));
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t lock_result =
        mcp2518fd_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    uint32_t oscillator = 0U;

    const esp_err_t read_result =
        mcp2518fd_read_register_unlocked(
            MCP2518FD_REGISTER_OSC,
            &oscillator
        );

    if (read_result == ESP_OK) {
        s_info.oscillator_register = oscillator;
        s_info.oscillator_ready =
            (oscillator & MCP2518FD_OSC_READY) != 0U;
    }

    *info = s_info;

    mcp2518fd_unlock();

    return read_result;
}

bool can_fd_mcp2518fd_driver_is_initialized(void)
{
    return s_initialized;
}

bool can_fd_mcp2518fd_driver_is_started(void)
{
    return s_initialized && s_started;
}

esp_err_t can_fd_mcp2518fd_driver_get_profile(
    can_fd_mcp2518fd_profile_t *profile
)
{
    if (profile == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

#if !CAN_FD_MCP2518FD_ENABLE_PROFILING

    memset(
        profile,
        0,
        sizeof(*profile)
    );

    return ESP_ERR_NOT_SUPPORTED;

#else

    if (!s_initialized) {
        memset(
            profile,
            0,
            sizeof(*profile)
        );

        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t lock_result =
        mcp2518fd_lock();

    if (lock_result != ESP_OK) {
        return lock_result;
    }

    *profile = s_profile;

    mcp2518fd_unlock();

    return ESP_OK;

#endif
}
