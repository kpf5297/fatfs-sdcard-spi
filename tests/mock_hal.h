/*
 * tests/mock_hal.h
 *
 * Control interface for the HAL mock. Tests use these functions to configure
 * what the SPI bus "returns" and to inspect call counts and GPIO state.
 *
 * Architecture
 * ------------
 * HAL_SPI_TransmitReceive pops bytes from a circular queue into pRxData.
 * If the queue is empty, 0xFF is returned (SD idle-line default), so tests
 * only need to queue bytes that deviate from 0xFF.
 *
 * HAL_SPI_Transmit is a true no-op: command bytes sent to the card carry no
 * protocol information in the receive direction.
 *
 * HAL_GetTick returns a software counter; HAL_Delay advances it by the
 * requested number of milliseconds. Tests can teleport the tick via
 * mock_hal_set_tick() to trigger timeout paths without real sleeping.
 */

#ifndef __MOCK_HAL_H__
#define __MOCK_HAL_H__

#include "mocks/main.h"
#include <stddef.h>

/* -----------------------------------------------------------------------
 * Lifecycle
 * ----------------------------------------------------------------------- */

/* Reset all mock state: clear queue, counters, GPIO, tick. */
void mock_hal_reset(void);

/* -----------------------------------------------------------------------
 * SPI receive queue
 * ----------------------------------------------------------------------- */

/* Push one byte that will be returned by the next HAL_SPI_TransmitReceive
 * call (for that byte position). */
void mock_hal_push_byte(uint8_t b);

/* Push a byte array. */
void mock_hal_push_bytes(const uint8_t *data, size_t len);

/* How many bytes are left in the queue. */
int mock_hal_queue_depth(void);

/* Override the return status of HAL_SPI_TransmitReceive.
 * Default: HAL_OK. Set to HAL_TIMEOUT to simulate SPI bus hang. */
void mock_hal_set_spi_return(HAL_StatusTypeDef status);

/* -----------------------------------------------------------------------
 * GPIO
 * ----------------------------------------------------------------------- */

/* Set the value that HAL_GPIO_ReadPin will return for any port/pin. */
void mock_hal_set_gpio_read(GPIO_PinState state);

/* -----------------------------------------------------------------------
 * Tick control
 * ----------------------------------------------------------------------- */

void     mock_hal_set_tick(uint32_t tick);
uint32_t mock_hal_get_tick(void);

/* -----------------------------------------------------------------------
 * Observability counters (reset by mock_hal_reset)
 * ----------------------------------------------------------------------- */

extern int mock_hal_transmit_calls;
extern int mock_hal_transmitrec_calls;
extern int mock_hal_gpio_write_calls;

#endif /* __MOCK_HAL_H__ */
