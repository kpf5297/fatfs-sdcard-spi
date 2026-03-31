/*
 * tests/mocks/main.h
 *
 * Host-native stub that replaces the STM32 main.h (which normally pulls in
 * CMSIS and HAL headers). Provides the minimal types and declarations that
 * sd_spi.c needs to compile without any embedded toolchain.
 */

#ifndef __MOCK_MAIN_H__
#define __MOCK_MAIN_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

/* HAL status codes */
typedef enum {
    HAL_OK      = 0x00U,
    HAL_ERROR   = 0x01U,
    HAL_BUSY    = 0x02U,
    HAL_TIMEOUT = 0x03U
} HAL_StatusTypeDef;

/* GPIO pin state */
typedef enum {
    GPIO_PIN_RESET = 0,
    GPIO_PIN_SET   = 1
} GPIO_PinState;

/* Minimal SPI handle — only existence as a pointer target matters */
typedef struct {
    uint32_t instance;
} SPI_HandleTypeDef;

/* Minimal GPIO peripheral type */
typedef struct {
    uint32_t instance;
} GPIO_TypeDef;

/* HAL function declarations */
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *hspi,
                                    uint8_t *pData, uint16_t Size,
                                    uint32_t Timeout);

HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *hspi,
                                           uint8_t *pTxData, uint8_t *pRxData,
                                           uint16_t Size, uint32_t Timeout);

HAL_StatusTypeDef HAL_SPI_Transmit_DMA(SPI_HandleTypeDef *hspi,
                                        uint8_t *pData, uint16_t Size);

HAL_StatusTypeDef HAL_SPI_TransmitReceive_DMA(SPI_HandleTypeDef *hspi,
                                               uint8_t *pTxData, uint8_t *pRxData,
                                               uint16_t Size);

HAL_StatusTypeDef HAL_SPI_Abort(SPI_HandleTypeDef *hspi);

void          HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin,
                                GPIO_PinState PinState);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);
uint32_t      HAL_GetTick(void);
void          HAL_Delay(uint32_t Delay);

/*
 * ARM Cortex-M IPSR register intrinsic.
 * Returns 0 on host → SD_InISR() always returns false.
 */
static inline uint32_t __get_IPSR(void) { return 0U; }

/*
 * __DCACHE_PRESENT is not defined for host builds, so SD_CacheClean /
 * SD_CacheInvalidate compile to no-ops via the #else branch in sd_spi.c.
 */

#endif /* __MOCK_MAIN_H__ */
