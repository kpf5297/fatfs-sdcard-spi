/*
 * sd_spi.c
 *
 * SPI SD card driver with deterministic timeouts and FreeRTOS-safe access.
 */

#include "sd_spi.h"
#include "main.h"
#include <string.h>

#if defined(USE_FREERTOS)
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#endif

#define SD_CMD9  (9)
#define SD_CMD12 (12)
#define SD_CMD16 (16)
#define SD_CMD18 (18)
#define SD_CMD25 (25)

#define SD_TOKEN_START_BLOCK       0xFEU
#define SD_TOKEN_START_MULTI_WRITE 0xFCU
#define SD_TOKEN_STOP_TRAN         0xFDU
#define SD_DATA_RESP_MASK          0x1FU
#define SD_DATA_RESP_ACCEPTED      0x05U
#define SD_DATA_RESP_CRC_ERR       0x0BU
#define SD_DATA_RESP_WRITE_ERR     0x0DU

#if defined(USE_FREERTOS) && (configSUPPORT_STATIC_ALLOCATION == 1)
static StaticSemaphore_t sd_mutex_buffer;
static StaticSemaphore_t sd_dma_tx_buffer;
static StaticSemaphore_t sd_dma_rx_buffer;
#endif

/* Single SPI SD instance for DMA callbacks. */
static SD_Handle_t *s_dma_owner = NULL;

static uint8_t s_dummy_tx[SD_BLOCK_SIZE] __attribute__((aligned(SD_DMA_ALIGNMENT)));
static uint8_t s_dummy_init = 0;

static SD_Status SD_RecordStatus(SD_Handle_t *sd_handle, SD_Status status) {
    if (sd_handle) {
        sd_handle->last_status = status;
        if (status != SD_OK) {
            sd_handle->stats.error_count++;
        }
        if (status == SD_TIMEOUT) {
            sd_handle->stats.timeout_count++;
        }
    }
    return status;
}

static bool SD_InISR(void) {
#if defined(USE_FREERTOS)
    return (__get_IPSR() != 0U);
#else
    return false;
#endif
}

static void SD_BackoffDelay(void) {
#if defined(USE_FREERTOS)
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        vTaskDelay(pdMS_TO_TICKS(1));
    } else {
        HAL_Delay(1);
    }
#else
    HAL_Delay(1);
#endif
}

static bool SD_IsAligned(const void *ptr, size_t align) {
    return ((uintptr_t)ptr % align) == 0U;
}

#if defined(__DCACHE_PRESENT) && (__DCACHE_PRESENT == 1U)
static void SD_CacheClean(const void *addr, size_t size) {
    uintptr_t start = (uintptr_t)addr & ~(SD_DMA_ALIGNMENT - 1U);
    uintptr_t end = ((uintptr_t)addr + size + SD_DMA_ALIGNMENT - 1U) & ~(SD_DMA_ALIGNMENT - 1U);
    SCB_CleanDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
}

static void SD_CacheInvalidate(void *addr, size_t size) {
    uintptr_t start = (uintptr_t)addr & ~(SD_DMA_ALIGNMENT - 1U);
    uintptr_t end = ((uintptr_t)addr + size + SD_DMA_ALIGNMENT - 1U) & ~(SD_DMA_ALIGNMENT - 1U);
    SCB_InvalidateDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
}
#else
static void SD_CacheClean(const void *addr, size_t size) {
    (void)addr;
    (void)size;
}

static void SD_CacheInvalidate(void *addr, size_t size) {
    (void)addr;
    (void)size;
}
#endif

static SD_Status SD_Lock(SD_Handle_t *sd_handle) {
#if defined(USE_FREERTOS)
    if (SD_InISR()) {
        return SD_BUSY;
    }
    if (sd_handle->mutex == NULL) {
        return SD_ERROR;
    }
    if (xSemaphoreTake(sd_handle->mutex, pdMS_TO_TICKS(SD_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return SD_BUSY;
    }
#endif
    return SD_OK;
}

static void SD_Unlock(SD_Handle_t *sd_handle) {
#if defined(USE_FREERTOS)
    if (sd_handle && sd_handle->mutex) {
        xSemaphoreGive(sd_handle->mutex);
    }
#else
    (void)sd_handle;
#endif
}

static SD_Status SD_FromHalStatus(HAL_StatusTypeDef status) {
    switch (status) {
    case HAL_OK:
        return SD_OK;
    case HAL_TIMEOUT:
        return SD_TIMEOUT;
    default:
        return SD_ERROR;
    }
}

static void SD_Select(SD_Handle_t *sd_handle) {
    HAL_GPIO_WritePin(sd_handle->cs_port, sd_handle->cs_pin, GPIO_PIN_RESET);
}

static void SD_Deselect(SD_Handle_t *sd_handle) {
    HAL_GPIO_WritePin(sd_handle->cs_port, sd_handle->cs_pin, GPIO_PIN_SET);
}

static SD_Status SD_SPI_Transmit(SD_Handle_t *sd_handle, const uint8_t *buffer, uint16_t len, bool use_dma) {
    if (use_dma) {
#if defined(USE_FREERTOS)
        if (sd_handle->dma_tx_sem == NULL) {
            return SD_ERROR;
        }
        (void)xSemaphoreTake(sd_handle->dma_tx_sem, 0);
#endif
        SD_CacheClean(buffer, len);
        sd_handle->dma_tx_done = false;
        if (HAL_SPI_Transmit_DMA(sd_handle->hspi, (uint8_t *)buffer, len) != HAL_OK) {
            return SD_ERROR;
        }
#if defined(USE_FREERTOS)
        if (xSemaphoreTake(sd_handle->dma_tx_sem, pdMS_TO_TICKS(SD_DMA_TIMEOUT_MS)) != pdTRUE) {
            (void)HAL_SPI_Abort(sd_handle->hspi);
            return SD_TIMEOUT;
        }
#else
        uint32_t timeout = HAL_GetTick() + SD_DMA_TIMEOUT_MS;
        while (!sd_handle->dma_tx_done && HAL_GetTick() < timeout) {
            SD_BackoffDelay();
        }
        if (!sd_handle->dma_tx_done) {
            (void)HAL_SPI_Abort(sd_handle->hspi);
            return SD_TIMEOUT;
        }
#endif
        return SD_OK;
    }

    return SD_FromHalStatus(HAL_SPI_Transmit(sd_handle->hspi, (uint8_t *)buffer, len, SD_SPI_IO_TIMEOUT_MS));
}

static SD_Status SD_SPI_TransmitReceive(SD_Handle_t *sd_handle, const uint8_t *tx, uint8_t *rx, uint16_t len, bool use_dma) {
    if (use_dma) {
#if defined(USE_FREERTOS)
        if (sd_handle->dma_rx_sem == NULL) {
            return SD_ERROR;
        }
        (void)xSemaphoreTake(sd_handle->dma_rx_sem, 0);
#endif
        SD_CacheClean(tx, len);
        SD_CacheInvalidate(rx, len);
        sd_handle->dma_rx_done = false;
        if (HAL_SPI_TransmitReceive_DMA(sd_handle->hspi, (uint8_t *)tx, rx, len) != HAL_OK) {
            return SD_ERROR;
        }
#if defined(USE_FREERTOS)
        if (xSemaphoreTake(sd_handle->dma_rx_sem, pdMS_TO_TICKS(SD_DMA_TIMEOUT_MS)) != pdTRUE) {
            (void)HAL_SPI_Abort(sd_handle->hspi);
            return SD_TIMEOUT;
        }
#else
        uint32_t timeout = HAL_GetTick() + SD_DMA_TIMEOUT_MS;
        while (!sd_handle->dma_rx_done && HAL_GetTick() < timeout) {
            SD_BackoffDelay();
        }
        if (!sd_handle->dma_rx_done) {
            (void)HAL_SPI_Abort(sd_handle->hspi);
            return SD_TIMEOUT;
        }
#endif
        SD_CacheInvalidate(rx, len);
        return SD_OK;
    }

    return SD_FromHalStatus(HAL_SPI_TransmitReceive(sd_handle->hspi, (uint8_t *)tx, rx, len, SD_SPI_IO_TIMEOUT_MS));
}

static SD_Status SD_TransmitByte(SD_Handle_t *sd_handle, uint8_t data) {
    return SD_SPI_Transmit(sd_handle, &data, 1, false);
}

static SD_Status SD_ReceiveByteTimeout(SD_Handle_t *sd_handle, uint8_t *data, uint32_t timeout_ms) {
    uint8_t dummy = 0xFFU;
    return SD_FromHalStatus(HAL_SPI_TransmitReceive(sd_handle->hspi, &dummy, data, 1, timeout_ms));
}

static SD_Status SD_ReceiveByte(SD_Handle_t *sd_handle, uint8_t *data) {
    return SD_ReceiveByteTimeout(sd_handle, data, SD_SPI_IO_TIMEOUT_MS);
}

static SD_Status SD_WaitReady(SD_Handle_t *sd_handle, uint32_t timeout_ms) {
    uint32_t timeout = HAL_GetTick() + timeout_ms;
    uint8_t resp = 0x00U;
    uint32_t io_timeout = (timeout_ms < SD_SPI_IO_TIMEOUT_MS) ? timeout_ms : SD_SPI_IO_TIMEOUT_MS;
    if (io_timeout == 0U) {
        io_timeout = 1U;
    }

    do {
        if (SD_ReceiveByteTimeout(sd_handle, &resp, io_timeout) != SD_OK) {
            return SD_ERROR;
        }
        if (resp == 0xFFU) {
            return SD_OK;
        }
        SD_BackoffDelay();
    } while (HAL_GetTick() < timeout);

    return SD_TIMEOUT;
}

static SD_Status SD_WaitDataToken(SD_Handle_t *sd_handle, uint32_t timeout_ms) {
    uint32_t timeout = HAL_GetTick() + timeout_ms;
    uint8_t token = 0x00U;
    uint32_t io_timeout = (timeout_ms < SD_SPI_IO_TIMEOUT_MS) ? timeout_ms : SD_SPI_IO_TIMEOUT_MS;
    if (io_timeout == 0U) {
        io_timeout = 1U;
    }

    do {
        if (SD_ReceiveByteTimeout(sd_handle, &token, io_timeout) != SD_OK) {
            return SD_ERROR;
        }
        if (token == SD_TOKEN_START_BLOCK) {
            return SD_OK;
        }
        SD_BackoffDelay();
    } while (HAL_GetTick() < timeout);

    return SD_TIMEOUT;
}

static SD_Status SD_SendCommand(SD_Handle_t *sd_handle, uint8_t cmd, uint32_t arg, uint8_t crc, uint8_t *response) {
    SD_Status status = SD_WaitReady(sd_handle, SD_CMD_TIMEOUT_MS);
    if (status != SD_OK) {
        return status;
    }

    if (SD_TransmitByte(sd_handle, 0xFFU) != SD_OK) return SD_ERROR;
    if (SD_TransmitByte(sd_handle, 0x40U | cmd) != SD_OK) return SD_ERROR;
    if (SD_TransmitByte(sd_handle, (uint8_t)(arg >> 24)) != SD_OK) return SD_ERROR;
    if (SD_TransmitByte(sd_handle, (uint8_t)(arg >> 16)) != SD_OK) return SD_ERROR;
    if (SD_TransmitByte(sd_handle, (uint8_t)(arg >> 8)) != SD_OK) return SD_ERROR;
    if (SD_TransmitByte(sd_handle, (uint8_t)arg) != SD_OK) return SD_ERROR;
    if (SD_TransmitByte(sd_handle, crc) != SD_OK) return SD_ERROR;

    uint8_t resp = 0xFFU;
    for (uint8_t retry = 0; retry < 10; retry++) {
        if (SD_ReceiveByte(sd_handle, &resp) != SD_OK) {
            return SD_ERROR;
        }
        if ((resp & 0x80U) == 0U) {
            if (response) {
                *response = resp;
            }
            return SD_OK;
        }
    }

    return SD_TIMEOUT;
}

static SD_Status SD_ReadCSD(SD_Handle_t *sd_handle, uint8_t *csd) {
    SD_Status status;
    uint8_t response = 0xFFU;

    SD_Select(sd_handle);
    status = SD_SendCommand(sd_handle, SD_CMD9, 0, 0xFFU, &response);
    if (status != SD_OK || response != 0x00U) {
        SD_Deselect(sd_handle);
        (void)SD_TransmitByte(sd_handle, 0xFFU);
        return SD_ERROR;
    }

    status = SD_WaitDataToken(sd_handle, SD_DATA_TOKEN_TIMEOUT_MS);
    if (status != SD_OK) {
        SD_Deselect(sd_handle);
        (void)SD_TransmitByte(sd_handle, 0xFFU);
        return status;
    }

    SD_Status rx_status = SD_SPI_TransmitReceive(sd_handle, s_dummy_tx, csd, 16, false);
    if (rx_status != SD_OK) {
        SD_Deselect(sd_handle);
        (void)SD_TransmitByte(sd_handle, 0xFFU);
        return rx_status;
    }

    (void)SD_ReceiveByte(sd_handle, &response);
    (void)SD_ReceiveByte(sd_handle, &response);
    SD_Deselect(sd_handle);
    (void)SD_TransmitByte(sd_handle, 0xFFU);

    return SD_OK;
}

static void SD_ParseCSD(SD_Handle_t *sd_handle, const uint8_t *csd) {
    uint8_t csd_structure = (csd[0] >> 6) & 0x3U;

    if (csd_structure == 1U) {
        uint32_t c_size = ((uint32_t)(csd[7] & 0x3FU) << 16) |
                          ((uint32_t)csd[8] << 8) |
                          (uint32_t)csd[9];
        sd_handle->capacity_blocks = (c_size + 1U) * 1024U;
    } else if (csd_structure == 0U) {
        uint32_t c_size = ((uint32_t)(csd[6] & 0x03U) << 10) |
                          ((uint32_t)csd[7] << 2) |
                          ((uint32_t)csd[8] >> 6);
        uint32_t c_size_mult = ((uint32_t)(csd[9] & 0x03U) << 1) |
                               ((uint32_t)(csd[10] >> 7) & 0x01U);
        uint32_t read_bl_len = csd[5] & 0x0FU;
        uint32_t block_len = 1UL << read_bl_len;
        uint32_t mult = 1UL << (c_size_mult + 2U);
        uint32_t blocknr = (c_size + 1U) * mult;
        uint32_t capacity_bytes = blocknr * block_len;
        sd_handle->capacity_blocks = capacity_bytes / SD_BLOCK_SIZE;
    } else {
        sd_handle->capacity_blocks = 0;
    }
}

static SD_Status SD_SetBlockLength(SD_Handle_t *sd_handle) {
    SD_Status status;
    uint8_t response = 0xFFU;

    SD_Select(sd_handle);
    status = SD_SendCommand(sd_handle, SD_CMD16, SD_BLOCK_SIZE, 0xFFU, &response);
    SD_Deselect(sd_handle);
    (void)SD_TransmitByte(sd_handle, 0xFFU);

    if (status != SD_OK || response != 0x00U) {
        return SD_ERROR;
    }

    return SD_OK;
}

static SD_Status SD_ReadSingleBlockInternal(SD_Handle_t *sd_handle, uint8_t *buff, uint32_t address) {
    SD_Select(sd_handle);
    uint8_t response = 0xFFU;
    SD_Status status = SD_SendCommand(sd_handle, CMD17, address, 0xFFU, &response);
    if (status != SD_OK || response != 0x00U) {
        SD_Deselect(sd_handle);
        (void)SD_TransmitByte(sd_handle, 0xFFU);
        return SD_ERROR;
    }

    status = SD_WaitDataToken(sd_handle, SD_DATA_TOKEN_TIMEOUT_MS);
    if (status != SD_OK) {
        SD_Deselect(sd_handle);
        (void)SD_TransmitByte(sd_handle, 0xFFU);
        return status;
    }

    bool use_dma = sd_handle->use_dma && SD_IsAligned(buff, SD_DMA_ALIGNMENT);
    status = SD_SPI_TransmitReceive(sd_handle, s_dummy_tx, buff, SD_BLOCK_SIZE, use_dma);
    if (status != SD_OK) {
        SD_Deselect(sd_handle);
        (void)SD_TransmitByte(sd_handle, 0xFFU);
        return status;
    }

    uint8_t crc[2];
    (void)SD_SPI_TransmitReceive(sd_handle, s_dummy_tx, crc, 2, false);
    SD_Deselect(sd_handle);
    (void)SD_TransmitByte(sd_handle, 0xFFU);

    return SD_OK;
}

static SD_Status SD_WriteSingleBlockInternal(SD_Handle_t *sd_handle, const uint8_t *buff, uint32_t address) {
    SD_Select(sd_handle);
    uint8_t response = 0xFFU;
    SD_Status status = SD_SendCommand(sd_handle, CMD24, address, 0xFFU, &response);
    if (status != SD_OK || response != 0x00U) {
        SD_Deselect(sd_handle);
        (void)SD_TransmitByte(sd_handle, 0xFFU);
        return SD_ERROR;
    }

    (void)SD_TransmitByte(sd_handle, SD_TOKEN_START_BLOCK);
    bool use_dma = sd_handle->use_dma && SD_IsAligned(buff, SD_DMA_ALIGNMENT);
    status = SD_SPI_Transmit(sd_handle, buff, SD_BLOCK_SIZE, use_dma);
    if (status != SD_OK) {
        SD_Deselect(sd_handle);
        (void)SD_TransmitByte(sd_handle, 0xFFU);
        return status;
    }

    (void)SD_TransmitByte(sd_handle, 0xFFU);
    (void)SD_TransmitByte(sd_handle, 0xFFU);

    (void)SD_ReceiveByte(sd_handle, &response);
    if ((response & SD_DATA_RESP_MASK) != SD_DATA_RESP_ACCEPTED) {
        SD_Deselect(sd_handle);
        (void)SD_TransmitByte(sd_handle, 0xFFU);
        return (response == SD_DATA_RESP_CRC_ERR) ? SD_CRC_ERROR : SD_WRITE_ERROR;
    }

    status = SD_WaitReady(sd_handle, SD_WRITE_BUSY_TIMEOUT_MS);
    SD_Deselect(sd_handle);
    (void)SD_TransmitByte(sd_handle, 0xFFU);

    return status;
}

static SD_Status SD_ReadMultiBlocksInternal(SD_Handle_t *sd_handle, uint8_t *buff, uint32_t sector, uint32_t count) {
    if (!sd_handle || !buff || count == 0) {
        return SD_PARAM;
    }
    if (!sd_handle->initialized) {
        return SD_ERROR;
    }

    uint32_t address = sd_handle->is_sdhc ? sector : (sector * SD_BLOCK_SIZE);
    SD_Select(sd_handle);

    uint8_t response = 0xFFU;
    SD_Status status = SD_SendCommand(sd_handle, SD_CMD18, address, 0xFFU, &response);
    if (status != SD_OK || response != 0x00U) {
        SD_Deselect(sd_handle);
        (void)SD_TransmitByte(sd_handle, 0xFFU);
        return SD_ERROR;
    }

    bool use_dma = sd_handle->use_dma && SD_IsAligned(buff, SD_DMA_ALIGNMENT);
    for (uint32_t i = 0; i < count; i++) {
        status = SD_WaitDataToken(sd_handle, SD_DATA_TOKEN_TIMEOUT_MS);
        if (status != SD_OK) {
            break;
        }

        if (use_dma) {
            status = SD_SPI_TransmitReceive(sd_handle, s_dummy_tx, buff, SD_BLOCK_SIZE, true);
        } else {
            status = SD_SPI_TransmitReceive(sd_handle, s_dummy_tx, buff, SD_BLOCK_SIZE, false);
        }
        if (status != SD_OK) {
            break;
        }

        uint8_t crc[2];
        (void)SD_SPI_TransmitReceive(sd_handle, s_dummy_tx, crc, 2, false);
        buff += SD_BLOCK_SIZE;
    }

    (void)SD_SendCommand(sd_handle, SD_CMD12, 0, 0xFFU, &response);
    SD_Deselect(sd_handle);
    (void)SD_TransmitByte(sd_handle, 0xFFU);

    return status;
}

static SD_Status SD_WriteMultiBlocksInternal(SD_Handle_t *sd_handle, const uint8_t *buff, uint32_t sector, uint32_t count) {
    if (!sd_handle || !buff || count == 0) {
        return SD_PARAM;
    }
    if (!sd_handle->initialized) {
        return SD_ERROR;
    }

    uint32_t address = sd_handle->is_sdhc ? sector : (sector * SD_BLOCK_SIZE);
    SD_Select(sd_handle);

    uint8_t response = 0xFFU;
    SD_Status status = SD_SendCommand(sd_handle, SD_CMD25, address, 0xFFU, &response);
    if (status != SD_OK || response != 0x00U) {
        SD_Deselect(sd_handle);
        (void)SD_TransmitByte(sd_handle, 0xFFU);
        return SD_ERROR;
    }

    bool use_dma = sd_handle->use_dma && SD_IsAligned(buff, SD_DMA_ALIGNMENT);
    for (uint32_t i = 0; i < count; i++) {
        (void)SD_TransmitByte(sd_handle, SD_TOKEN_START_MULTI_WRITE);
        status = SD_SPI_Transmit(sd_handle, buff, SD_BLOCK_SIZE, use_dma);
        if (status != SD_OK) {
            break;
        }

        (void)SD_TransmitByte(sd_handle, 0xFFU);
        (void)SD_TransmitByte(sd_handle, 0xFFU);

        (void)SD_ReceiveByte(sd_handle, &response);
        if ((response & SD_DATA_RESP_MASK) != SD_DATA_RESP_ACCEPTED) {
            status = (response == SD_DATA_RESP_CRC_ERR) ? SD_CRC_ERROR : SD_WRITE_ERROR;
            break;
        }

        status = SD_WaitReady(sd_handle, SD_WRITE_BUSY_TIMEOUT_MS);
        if (status != SD_OK) {
            break;
        }
        buff += SD_BLOCK_SIZE;
    }

    (void)SD_TransmitByte(sd_handle, SD_TOKEN_STOP_TRAN);
    (void)SD_WaitReady(sd_handle, SD_WRITE_BUSY_TIMEOUT_MS);
    SD_Deselect(sd_handle);
    (void)SD_TransmitByte(sd_handle, 0xFFU);

    return status;
}

/* DMA callbacks */
void HAL_SPI_TxCpltCallback(SPI_HandleTypeDef *hspi) {
    if (s_dma_owner && s_dma_owner->hspi == hspi) {
        s_dma_owner->dma_tx_done = true;
#if defined(USE_FREERTOS)
        if (s_dma_owner->dma_tx_sem) {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            xSemaphoreGiveFromISR(s_dma_owner->dma_tx_sem, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
#endif
    }
}

void HAL_SPI_RxCpltCallback(SPI_HandleTypeDef *hspi) {
    if (s_dma_owner && s_dma_owner->hspi == hspi) {
        s_dma_owner->dma_rx_done = true;
#if defined(USE_FREERTOS)
        if (s_dma_owner->dma_rx_sem) {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            xSemaphoreGiveFromISR(s_dma_owner->dma_rx_sem, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
#endif
    }
}

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi) {
    HAL_SPI_RxCpltCallback(hspi);
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi) {
    if (s_dma_owner && s_dma_owner->hspi == hspi) {
        s_dma_owner->dma_tx_done = true;
        s_dma_owner->dma_rx_done = true;
#if defined(USE_FREERTOS)
        if (s_dma_owner->dma_tx_sem) {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            xSemaphoreGiveFromISR(s_dma_owner->dma_tx_sem, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
        if (s_dma_owner->dma_rx_sem) {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            xSemaphoreGiveFromISR(s_dma_owner->dma_rx_sem, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
        }
#endif
    }
}

SD_Status SD_Init(SD_Handle_t *sd_handle, SPI_HandleTypeDef *hspi,
                  GPIO_TypeDef *cs_port, uint16_t cs_pin, bool use_dma) {
    if (!sd_handle || !hspi || !cs_port) {
        return SD_PARAM;
    }

    memset(sd_handle, 0, sizeof(SD_Handle_t));
    sd_handle->hspi = hspi;
    sd_handle->cs_port = cs_port;
    sd_handle->cs_pin = cs_pin;
    sd_handle->use_dma = use_dma;
    sd_handle->initialized = false;
    sd_handle->is_sdhc = false;
    sd_handle->block_size = SD_BLOCK_SIZE;
    sd_handle->last_status = SD_OK;

#if defined(USE_FREERTOS)
#if (configSUPPORT_STATIC_ALLOCATION == 1)
    sd_handle->mutex = xSemaphoreCreateMutexStatic(&sd_mutex_buffer);
    sd_handle->dma_tx_sem = xSemaphoreCreateBinaryStatic(&sd_dma_tx_buffer);
    sd_handle->dma_rx_sem = xSemaphoreCreateBinaryStatic(&sd_dma_rx_buffer);
#else
    sd_handle->mutex = xSemaphoreCreateMutex();
    sd_handle->dma_tx_sem = xSemaphoreCreateBinary();
    sd_handle->dma_rx_sem = xSemaphoreCreateBinary();
#endif
    if (sd_handle->mutex == NULL || sd_handle->dma_tx_sem == NULL || sd_handle->dma_rx_sem == NULL) {
        return SD_RecordStatus(sd_handle, SD_ERROR);
    }
    (void)xSemaphoreTake(sd_handle->dma_tx_sem, 0);
    (void)xSemaphoreTake(sd_handle->dma_rx_sem, 0);
#endif

    s_dma_owner = sd_handle;
    return SD_OK;
}

void SD_DeInit(SD_Handle_t *sd_handle) {
    if (!sd_handle) {
        return;
    }

#if defined(USE_FREERTOS)
    if (sd_handle->mutex) {
        vSemaphoreDelete(sd_handle->mutex);
        sd_handle->mutex = NULL;
    }
    if (sd_handle->dma_tx_sem) {
        vSemaphoreDelete(sd_handle->dma_tx_sem);
        sd_handle->dma_tx_sem = NULL;
    }
    if (sd_handle->dma_rx_sem) {
        vSemaphoreDelete(sd_handle->dma_rx_sem);
        sd_handle->dma_rx_sem = NULL;
    }
#endif

    sd_handle->initialized = false;
    if (s_dma_owner == sd_handle) {
        s_dma_owner = NULL;
    }
}

SD_Status SD_SetCardDetect(SD_Handle_t *sd_handle, GPIO_TypeDef *cd_port, uint16_t cd_pin, bool active_low) {
    if (!sd_handle || !cd_port) {
        return SD_PARAM;
    }
    sd_handle->cd_port = cd_port;
    sd_handle->cd_pin = cd_pin;
    sd_handle->cd_active_low = active_low;
    sd_handle->has_cd = true;
    return SD_OK;
}

bool SD_IsCardPresent(SD_Handle_t *sd_handle) {
    if (!sd_handle) {
        return false;
    }
    if (!sd_handle->has_cd) {
        return true;
    }
    GPIO_PinState state = HAL_GPIO_ReadPin(sd_handle->cd_port, sd_handle->cd_pin);
    return sd_handle->cd_active_low ? (state == GPIO_PIN_RESET) : (state == GPIO_PIN_SET);
}

SD_Status SD_SPI_Init(SD_Handle_t *sd_handle) {
    if (!sd_handle) {
        return SD_PARAM;
    }

    if (!SD_IsCardPresent(sd_handle)) {
        sd_handle->initialized = false;
        return SD_RecordStatus(sd_handle, SD_NO_MEDIA);
    }

    sd_handle->stats.init_attempts++;

    SD_Status lock_status = SD_Lock(sd_handle);
    if (lock_status != SD_OK) {
        return SD_RecordStatus(sd_handle, lock_status);
    }

    if (!s_dummy_init) {
        memset(s_dummy_tx, 0xFF, sizeof(s_dummy_tx));
        s_dummy_init = 1;
    }

    sd_handle->initialized = false;

    SD_Deselect(sd_handle);
    for (uint8_t i = 0; i < 10; i++) {
        (void)SD_TransmitByte(sd_handle, 0xFFU);
    }

    SD_Status status = SD_ERROR;
    uint8_t response = 0xFFU;
    uint8_t r7[4] = {0};
    uint32_t deadline = HAL_GetTick() + SD_INIT_TIMEOUT_MS;

    do {
        SD_Select(sd_handle);
        status = SD_SendCommand(sd_handle, CMD0, 0, 0x95U, &response);
        SD_Deselect(sd_handle);
        (void)SD_TransmitByte(sd_handle, 0xFFU);
        if (status == SD_OK && response == 0x01U) {
            break;
        }
        SD_BackoffDelay();
    } while (HAL_GetTick() < deadline);

    if (response != 0x01U) {
        SD_Unlock(sd_handle);
        return SD_RecordStatus(sd_handle, SD_ERROR);
    }

    SD_Select(sd_handle);
    status = SD_SendCommand(sd_handle, CMD8, 0x000001AAU, 0x87U, &response);
    if (status == SD_OK) {
        for (uint8_t i = 0; i < 4; i++) {
            (void)SD_ReceiveByte(sd_handle, &r7[i]);
        }
    }
    SD_Deselect(sd_handle);
    (void)SD_TransmitByte(sd_handle, 0xFFU);

    bool sdv2 = (status == SD_OK && response == 0x01U && r7[2] == 0x01U && r7[3] == 0xAAU);

    deadline = HAL_GetTick() + SD_INIT_TIMEOUT_MS;
    do {
        SD_Select(sd_handle);
        (void)SD_SendCommand(sd_handle, CMD55, 0, 0xFFU, &response);
        status = SD_SendCommand(sd_handle, ACMD41, sdv2 ? 0x40000000U : 0, 0xFFU, &response);
        SD_Deselect(sd_handle);
        (void)SD_TransmitByte(sd_handle, 0xFFU);
        if (status == SD_OK && response == 0x00U) {
            break;
        }
        SD_BackoffDelay();
    } while (HAL_GetTick() < deadline);

    if (response != 0x00U) {
        SD_Unlock(sd_handle);
        return SD_RecordStatus(sd_handle, SD_TIMEOUT);
    }

    sd_handle->is_sdhc = false;
    SD_Select(sd_handle);
    status = SD_SendCommand(sd_handle, CMD58, 0, 0xFFU, &response);
    if (status == SD_OK && response == 0x00U) {
        uint8_t ocr[4] = {0};
        for (uint8_t i = 0; i < 4; i++) {
            (void)SD_ReceiveByte(sd_handle, &ocr[i]);
        }
        if (ocr[0] & 0x40U) {
            sd_handle->is_sdhc = true;
        }
    }
    SD_Deselect(sd_handle);
    (void)SD_TransmitByte(sd_handle, 0xFFU);

    if (!sd_handle->is_sdhc) {
        status = SD_SetBlockLength(sd_handle);
        if (status != SD_OK) {
            SD_Unlock(sd_handle);
            return SD_RecordStatus(sd_handle, status);
        }
    }

    uint8_t csd[16];
    if (SD_ReadCSD(sd_handle, csd) == SD_OK) {
        SD_ParseCSD(sd_handle, csd);
    } else {
        sd_handle->capacity_blocks = 0;
    }

    sd_handle->initialized = true;
    SD_Unlock(sd_handle);
    return SD_RecordStatus(sd_handle, SD_OK);
}

SD_Status SD_ReadBlocks(SD_Handle_t *sd_handle, uint8_t *buff, uint32_t sector, uint32_t count) {
    if (!sd_handle) {
        return SD_PARAM;
    }
    if (!buff || count == 0) {
        return SD_RecordStatus(sd_handle, SD_PARAM);
    }
    if (!SD_IsCardPresent(sd_handle)) {
        sd_handle->initialized = false;
        return SD_RecordStatus(sd_handle, SD_NO_MEDIA);
    }

    SD_Status lock_status = SD_Lock(sd_handle);
    if (lock_status != SD_OK) {
        return SD_RecordStatus(sd_handle, lock_status);
    }

    if (!sd_handle->initialized) {
        SD_Unlock(sd_handle);
        return SD_RecordStatus(sd_handle, SD_ERROR);
    }

    sd_handle->stats.read_ops++;
    sd_handle->stats.read_blocks += count;

    uint32_t address = sd_handle->is_sdhc ? sector : (sector * SD_BLOCK_SIZE);
    SD_Status status = SD_OK;

    if (count == 1U) {
        for (uint32_t attempt = 0; attempt <= SD_MAX_RETRIES; attempt++) {
            status = SD_ReadSingleBlockInternal(sd_handle, buff, address);
            if (status == SD_OK) {
                break;
            }
            SD_BackoffDelay();
        }
    } else {
        status = SD_ReadMultiBlocksInternal(sd_handle, buff, sector, count);
    }

    SD_Unlock(sd_handle);
    return SD_RecordStatus(sd_handle, status);
}

SD_Status SD_ReadMultiBlocks(SD_Handle_t *sd_handle, uint8_t *buff, uint32_t sector, uint32_t count) {
    if (!sd_handle) {
        return SD_PARAM;
    }
    if (!buff || count == 0) {
        return SD_RecordStatus(sd_handle, SD_PARAM);
    }
    if (!SD_IsCardPresent(sd_handle)) {
        sd_handle->initialized = false;
        return SD_RecordStatus(sd_handle, SD_NO_MEDIA);
    }

    SD_Status lock_status = SD_Lock(sd_handle);
    if (lock_status != SD_OK) {
        return SD_RecordStatus(sd_handle, lock_status);
    }

    sd_handle->stats.read_ops++;
    sd_handle->stats.read_blocks += count;

    SD_Status status = SD_ReadMultiBlocksInternal(sd_handle, buff, sector, count);
    SD_Unlock(sd_handle);
    return SD_RecordStatus(sd_handle, status);
}

SD_Status SD_WriteBlocks(SD_Handle_t *sd_handle, const uint8_t *buff, uint32_t sector, uint32_t count) {
    if (!sd_handle) {
        return SD_PARAM;
    }
    if (!buff || count == 0) {
        return SD_RecordStatus(sd_handle, SD_PARAM);
    }
    if (!SD_IsCardPresent(sd_handle)) {
        sd_handle->initialized = false;
        return SD_RecordStatus(sd_handle, SD_NO_MEDIA);
    }

    SD_Status lock_status = SD_Lock(sd_handle);
    if (lock_status != SD_OK) {
        return SD_RecordStatus(sd_handle, lock_status);
    }

    if (!sd_handle->initialized) {
        SD_Unlock(sd_handle);
        return SD_RecordStatus(sd_handle, SD_ERROR);
    }

    sd_handle->stats.write_ops++;
    sd_handle->stats.write_blocks += count;

    uint32_t address = sd_handle->is_sdhc ? sector : (sector * SD_BLOCK_SIZE);
    SD_Status status = SD_OK;

    if (count == 1U) {
        for (uint32_t attempt = 0; attempt <= SD_MAX_RETRIES; attempt++) {
            status = SD_WriteSingleBlockInternal(sd_handle, buff, address);
            if (status == SD_OK) {
                break;
            }
            SD_BackoffDelay();
        }
    } else {
        status = SD_WriteMultiBlocksInternal(sd_handle, buff, sector, count);
    }

    SD_Unlock(sd_handle);
    return SD_RecordStatus(sd_handle, status);
}

SD_Status SD_WriteMultiBlocks(SD_Handle_t *sd_handle, const uint8_t *buff, uint32_t sector, uint32_t count) {
    if (!sd_handle) {
        return SD_PARAM;
    }
    if (!buff || count == 0) {
        return SD_RecordStatus(sd_handle, SD_PARAM);
    }
    if (!SD_IsCardPresent(sd_handle)) {
        sd_handle->initialized = false;
        return SD_RecordStatus(sd_handle, SD_NO_MEDIA);
    }

    SD_Status lock_status = SD_Lock(sd_handle);
    if (lock_status != SD_OK) {
        return SD_RecordStatus(sd_handle, lock_status);
    }

    sd_handle->stats.write_ops++;
    sd_handle->stats.write_blocks += count;

    SD_Status status = SD_WriteMultiBlocksInternal(sd_handle, buff, sector, count);
    SD_Unlock(sd_handle);
    return SD_RecordStatus(sd_handle, status);
}

bool SD_IsSDHC(SD_Handle_t *sd_handle) {
    return sd_handle ? sd_handle->is_sdhc : false;
}

bool SD_IsInitialized(SD_Handle_t *sd_handle) {
    return sd_handle ? sd_handle->initialized : false;
}

uint32_t SD_GetBlockCount(SD_Handle_t *sd_handle) {
    return sd_handle ? sd_handle->capacity_blocks : 0U;
}

SD_Status SD_Sync(SD_Handle_t *sd_handle) {
    if (!sd_handle) {
        return SD_PARAM;
    }
    if (!sd_handle->initialized) {
        return SD_RecordStatus(sd_handle, SD_ERROR);
    }
    if (!SD_IsCardPresent(sd_handle)) {
        sd_handle->initialized = false;
        return SD_RecordStatus(sd_handle, SD_NO_MEDIA);
    }

    SD_Status lock_status = SD_Lock(sd_handle);
    if (lock_status != SD_OK) {
        return SD_RecordStatus(sd_handle, lock_status);
    }

    SD_Select(sd_handle);
    SD_Status status = SD_WaitReady(sd_handle, SD_WRITE_BUSY_TIMEOUT_MS);
    SD_Deselect(sd_handle);
    (void)SD_TransmitByte(sd_handle, 0xFFU);

    SD_Unlock(sd_handle);
    return SD_RecordStatus(sd_handle, status);
}

void SD_GetStats(SD_Handle_t *sd_handle, SD_Stats *stats) {
    if (!sd_handle || !stats) {
        return;
    }
    *stats = sd_handle->stats;
}

void SD_ResetStats(SD_Handle_t *sd_handle) {
    if (!sd_handle) {
        return;
    }
    memset(&sd_handle->stats, 0, sizeof(sd_handle->stats));
}
