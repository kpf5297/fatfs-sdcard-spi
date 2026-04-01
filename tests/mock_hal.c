/*
 * tests/mock_hal.c
 *
 * HAL stub implementations for host-native SD card driver tests.
 */

#include "mock_hal.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

/* -----------------------------------------------------------------------
 * SPI queue
 * ----------------------------------------------------------------------- */

#define SPI_QUEUE_SIZE 8192

static uint8_t           s_queue[SPI_QUEUE_SIZE];
static int               s_head  = 0;
static int               s_count = 0;
static HAL_StatusTypeDef s_spi_ret = HAL_OK;

/* -----------------------------------------------------------------------
 * GPIO / Tick
 * ----------------------------------------------------------------------- */

static GPIO_PinState s_gpio_read = GPIO_PIN_RESET;
static uint32_t      s_tick      = 0;

/* -----------------------------------------------------------------------
 * Counters
 * ----------------------------------------------------------------------- */

int mock_hal_transmit_calls    = 0;
int mock_hal_transmitrec_calls = 0;
int mock_hal_gpio_write_calls  = 0;

/* -----------------------------------------------------------------------
 * Control API
 * ----------------------------------------------------------------------- */

void mock_hal_reset(void) {
    memset(s_queue, 0xFF, sizeof(s_queue));
    s_head     = 0;
    s_count    = 0;
    s_spi_ret  = HAL_OK;
    s_gpio_read = GPIO_PIN_RESET;
    s_tick     = 0;
    mock_hal_transmit_calls    = 0;
    mock_hal_transmitrec_calls = 0;
    mock_hal_gpio_write_calls  = 0;
}

void mock_hal_push_byte(uint8_t b) {
    assert(s_count < SPI_QUEUE_SIZE && "SPI queue overflow");
    int tail = (s_head + s_count) % SPI_QUEUE_SIZE;
    s_queue[tail] = b;
    s_count++;
}

void mock_hal_push_bytes(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        mock_hal_push_byte(data[i]);
    }
}

int mock_hal_queue_depth(void) {
    return s_count;
}

void mock_hal_set_spi_return(HAL_StatusTypeDef status) {
    s_spi_ret = status;
}

void mock_hal_set_gpio_read(GPIO_PinState state) {
    s_gpio_read = state;
}

void mock_hal_set_tick(uint32_t tick) {
    s_tick = tick;
}

uint32_t mock_hal_get_tick(void) {
    return s_tick;
}

/* -----------------------------------------------------------------------
 * HAL function stubs
 * ----------------------------------------------------------------------- */

HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *hspi,
                                    uint8_t *pData, uint16_t Size,
                                    uint32_t Timeout) {
    (void)hspi; (void)pData; (void)Size; (void)Timeout;
    mock_hal_transmit_calls++;
    return s_spi_ret;
}

HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *hspi,
                                           uint8_t *pTxData, uint8_t *pRxData,
                                           uint16_t Size, uint32_t Timeout) {
    (void)hspi; (void)pTxData; (void)Timeout;
    mock_hal_transmitrec_calls++;

    for (int i = 0; i < (int)Size; i++) {
        if (s_count > 0) {
            pRxData[i] = s_queue[s_head];
            s_head = (s_head + 1) % SPI_QUEUE_SIZE;
            s_count--;
        } else {
            pRxData[i] = 0xFFU; /* idle line default */
        }
    }
    return s_spi_ret;
}

/* DMA stubs: return HAL_ERROR so the driver always falls back to polling. */
HAL_StatusTypeDef HAL_SPI_Transmit_DMA(SPI_HandleTypeDef *hspi,
                                        uint8_t *pData, uint16_t Size) {
    (void)hspi; (void)pData; (void)Size;
    return HAL_ERROR;
}

HAL_StatusTypeDef HAL_SPI_TransmitReceive_DMA(SPI_HandleTypeDef *hspi,
                                               uint8_t *pTxData, uint8_t *pRxData,
                                               uint16_t Size) {
    (void)hspi; (void)pTxData; (void)pRxData; (void)Size;
    return HAL_ERROR;
}

HAL_StatusTypeDef HAL_SPI_Abort(SPI_HandleTypeDef *hspi) {
    (void)hspi;
    return HAL_OK;
}

void HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState) {
    (void)GPIOx; (void)GPIO_Pin; (void)PinState;
    mock_hal_gpio_write_calls++;
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin) {
    (void)GPIOx; (void)GPIO_Pin;
    return s_gpio_read;
}

uint32_t HAL_GetTick(void) {
    return s_tick;
}

void HAL_Delay(uint32_t Delay) {
    s_tick += Delay;
}
