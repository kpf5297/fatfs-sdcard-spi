/*
 * sd_spi.h
 *
 * SPI SD card driver (SDSC/SDHC) for STM32 + FreeRTOS.
 * Production-focused API: deterministic timeouts, thread-safe access,
 * and explicit error reporting. All APIs are blocking and require task context.
 */

#ifndef __SD_SPI_H__
#define __SD_SPI_H__

#include "main.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef USE_FREERTOS
#include "FreeRTOS.h"
#include "semphr.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define CMD0 (0)
#define CMD8 (8)
#define CMD17 (17)
#define CMD24 (24)
#define CMD55 (55)
#define CMD58 (58)
#define ACMD41 (41)

typedef enum {
    SD_OK = 0,
    SD_ERROR,
    SD_TIMEOUT,
    SD_BUSY,
    SD_PARAM,
    SD_NO_MEDIA,
    SD_CRC_ERROR,
    SD_WRITE_ERROR,
    SD_UNSUPPORTED
} SD_Status;

typedef struct {
    uint32_t read_ops;
    uint32_t write_ops;
    uint32_t read_blocks;
    uint32_t write_blocks;
    uint32_t init_attempts;
    uint32_t error_count;
    uint32_t timeout_count;
} SD_Stats;

typedef struct {
    SPI_HandleTypeDef *hspi;   // SPI handle
    GPIO_TypeDef *cs_port;     // Chip select GPIO port
    uint16_t cs_pin;           // Chip select GPIO pin
    GPIO_TypeDef *cd_port;     // Optional card-detect port
    uint16_t cd_pin;           // Optional card-detect pin
    bool cd_active_low;        // Card-detect polarity
    bool has_cd;               // Card-detect enabled
    bool initialized;          // Card initialization status
    bool is_sdhc;              // SDHC/SDXC card flag
    bool use_dma;              // DMA usage flag
    volatile bool dma_tx_done; // DMA TX completion flag
    volatile bool dma_rx_done; // DMA RX completion flag
#ifdef USE_FREERTOS
    SemaphoreHandle_t mutex;      // FreeRTOS mutex for thread safety
    SemaphoreHandle_t dma_tx_sem; // DMA TX completion semaphore
    SemaphoreHandle_t dma_rx_sem; // DMA RX completion semaphore
#endif
    SD_Status last_status;    // Last operation status
    uint32_t capacity_blocks; // Card capacity in 512-byte blocks
    uint32_t block_size;      // Logical block size (bytes)
    SD_Stats stats;           // Driver statistics
} SD_Handle_t;

/* Configuration defaults (override in build system or before include). */
#ifndef SD_BLOCK_SIZE
#define SD_BLOCK_SIZE 512U
#endif

#ifndef SD_SPI_IO_TIMEOUT_MS
#define SD_SPI_IO_TIMEOUT_MS 50U
#endif

#ifndef SD_CMD_TIMEOUT_MS
#define SD_CMD_TIMEOUT_MS 100U
#endif

#ifndef SD_DATA_TOKEN_TIMEOUT_MS
#define SD_DATA_TOKEN_TIMEOUT_MS 200U
#endif

#ifndef SD_WRITE_BUSY_TIMEOUT_MS
#define SD_WRITE_BUSY_TIMEOUT_MS 500U
#endif

#ifndef SD_INIT_TIMEOUT_MS
#define SD_INIT_TIMEOUT_MS 1000U
#endif

#ifndef SD_DMA_TIMEOUT_MS
#define SD_DMA_TIMEOUT_MS 500U
#endif

#ifndef SD_MUTEX_TIMEOUT_MS
#define SD_MUTEX_TIMEOUT_MS 1000U
#endif

#ifndef SD_MAX_RETRIES
#define SD_MAX_RETRIES 2U
#endif

#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
#ifndef SD_DMA_ALIGNMENT
#define SD_DMA_ALIGNMENT 32U
#endif
#else
#ifndef SD_DMA_ALIGNMENT
#define SD_DMA_ALIGNMENT 4U
#endif
#endif

#ifndef SD_LOG_ENABLED
#define SD_LOG_ENABLED 0
#endif

#if SD_LOG_ENABLED
#include <stdio.h>
#define SD_LOG(fmt, ...) printf(fmt, ##__VA_ARGS__)
#define SD_LOG_ERROR(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#define SD_LOG(...)                                                                                \
    do {                                                                                           \
    } while (0)
#define SD_LOG_ERROR(...)                                                                          \
    do {                                                                                           \
    } while (0)
#endif

/* Alignment and cache maintenance requirements apply when DMA is enabled. */

/**
 * @brief Initialize SD card handle
 * @param sd_handle Pointer to SD handle structure
 * @param hspi SPI handle
 * @param cs_port Chip select GPIO port
 * @param cs_pin Chip select GPIO pin
 * @param use_dma Whether to use DMA for transfers
 * @return SD_Status
 */
SD_Status SD_Init(SD_Handle_t *sd_handle, SPI_HandleTypeDef *hspi, GPIO_TypeDef *cs_port,
                  uint16_t cs_pin, bool use_dma);

/**
 * @brief Initialize SD card communication
 * @param sd_handle Pointer to SD handle structure
 * @return SD_Status
 */
SD_Status SD_SPI_Init(SD_Handle_t *sd_handle);

/**
 * @brief Configure optional card-detect pin
 * @param sd_handle Pointer to SD handle structure
 * @param cd_port Card-detect GPIO port
 * @param cd_pin Card-detect GPIO pin
 * @param active_low true if CD is active-low, false if active-high
 * @return SD_Status
 */
SD_Status SD_SetCardDetect(SD_Handle_t *sd_handle, GPIO_TypeDef *cd_port, uint16_t cd_pin,
                           bool active_low);

/**
 * @brief Check if card is present (uses card-detect if configured)
 * @param sd_handle Pointer to SD handle structure
 * @return true if present or no card-detect configured
 */
bool SD_IsCardPresent(SD_Handle_t *sd_handle);

/**
 * @brief Read blocks from SD card
 * @param sd_handle Pointer to SD handle structure
 * @param buff Buffer to store read data
 * @param sector Starting sector
 * @param count Number of sectors to read
 * @return SD_Status
 *
 * Note: If DMA is enabled and buffer alignment is insufficient, polling is used.
 */
SD_Status SD_ReadBlocks(SD_Handle_t *sd_handle, uint8_t *buff, uint32_t sector, uint32_t count);

/**
 * @brief Write blocks to SD card
 * @param sd_handle Pointer to SD handle structure
 * @param buff Buffer containing data to write
 * @param sector Starting sector
 * @param count Number of sectors to write
 * @return SD_Status
 *
 * Note: If DMA is enabled and buffer alignment is insufficient, polling is used.
 */
SD_Status SD_WriteBlocks(SD_Handle_t *sd_handle, const uint8_t *buff, uint32_t sector,
                         uint32_t count);

/**
 * @brief Read multiple blocks from SD card
 * @param sd_handle Pointer to SD handle structure
 * @param buff Buffer to store read data
 * @param sector Starting sector
 * @param count Number of sectors to read
 * @return SD_Status
 */
SD_Status SD_ReadMultiBlocks(SD_Handle_t *sd_handle, uint8_t *buff, uint32_t sector,
                             uint32_t count);

/**
 * @brief Write multiple blocks to SD card
 * @param sd_handle Pointer to SD handle structure
 * @param buff Buffer containing data to write
 * @param sector Starting sector
 * @param count Number of sectors to write
 * @return SD_Status
 */
SD_Status SD_WriteMultiBlocks(SD_Handle_t *sd_handle, const uint8_t *buff, uint32_t sector,
                              uint32_t count);

/**
 * @brief Check if card is SDHC/SDXC
 * @param sd_handle Pointer to SD handle structure
 * @return true if SDHC/SDXC, false otherwise
 */
bool SD_IsSDHC(SD_Handle_t *sd_handle);

/**
 * @brief Check if card is initialized
 * @param sd_handle Pointer to SD handle structure
 * @return true if initialized, false otherwise
 */
bool SD_IsInitialized(SD_Handle_t *sd_handle);

/**
 * @brief Get card capacity in 512-byte blocks
 * @param sd_handle Pointer to SD handle structure
 * @return Block count or 0 if unknown
 */
uint32_t SD_GetBlockCount(SD_Handle_t *sd_handle);

/**
 * @brief Ensure the card is not busy after a write
 * @param sd_handle Pointer to SD handle structure
 * @return SD_Status
 */
SD_Status SD_Sync(SD_Handle_t *sd_handle);

/**
 * @brief Get a snapshot of driver statistics
 * @param sd_handle Pointer to SD handle structure
 * @param stats Output stats (must be non-NULL)
 */
void SD_GetStats(SD_Handle_t *sd_handle, SD_Stats *stats);

/**
 * @brief Reset driver statistics counters
 * @param sd_handle Pointer to SD handle structure
 */
void SD_ResetStats(SD_Handle_t *sd_handle);

/**
 * @brief Deinitialize SD card handle (free resources)
 * @param sd_handle Pointer to SD handle structure
 */
void SD_DeInit(SD_Handle_t *sd_handle);

#ifdef __cplusplus
}
#endif

#endif // __SD_SPI_H__
