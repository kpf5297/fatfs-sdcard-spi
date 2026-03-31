/*
 * tests/test_helpers.h
 *
 * Shared helpers that load SPI queue byte sequences matching the SD card SPI
 * protocol. Each helper corresponds to one SD protocol exchange or a composite
 * initialization flow.
 *
 * Byte sequence rules
 * -------------------
 * Only HAL_SPI_TransmitReceive consumes bytes from the queue.
 * HAL_SPI_Transmit (command bytes) is a no-op and consumes nothing.
 *
 * SD_WaitReady polls by calling ReceiveByteTimeout (= TransmitReceive 1 byte)
 * until it gets 0xFF. Push 0xFF once to let it pass immediately.
 *
 * SD_SendCommand polls for the R1 response up to 10 times. Push the R1 byte
 * directly (bit 7 must be 0 for a valid response).
 *
 * SD_WaitDataToken polls until 0xFE. Push 0xFE to pass immediately.
 */

#ifndef __TEST_HELPERS_H__
#define __TEST_HELPERS_H__

#include "mock_hal.h"
#include "sd_spi.h"
#include <stdint.h>
#include <string.h>

/* Test SD handle and peripheral stubs used across all test files. */
static SPI_HandleTypeDef g_test_hspi = {0};
static GPIO_TypeDef      g_test_cs   = {0};
static GPIO_TypeDef      g_test_cd   = {0};

/* -----------------------------------------------------------------------
 * Low-level protocol byte pushers
 * ----------------------------------------------------------------------- */

/* Push 0xFF so SD_WaitReady returns SD_OK on the first byte. */
static inline void push_wait_ready(void) {
    mock_hal_push_byte(0xFFU);
}

/* Push an R1 response byte (bit 7 must be 0 for the driver to accept it). */
static inline void push_r1(uint8_t r1) {
    mock_hal_push_byte(r1);
}

/* Push CMD8 R7 echo bytes confirming 3.3 V and 0xAA pattern (SDv2). */
static inline void push_r7_sdv2(void) {
    static const uint8_t r7[] = {0x00U, 0x00U, 0x01U, 0xAAU};
    mock_hal_push_bytes(r7, 4);
}

/* Push CMD8 R7 bytes for SDv1 (non-matching echo → sdv2 = false). */
static inline void push_r7_sdv1(void) {
    static const uint8_t r7[] = {0x00U, 0x00U, 0x00U, 0x00U};
    mock_hal_push_bytes(r7, 4);
}

/* Push OCR bytes with CCS bit set (bit 30 of OCR = bit 6 of byte 0) → SDHC. */
static inline void push_ocr_sdhc(void) {
    static const uint8_t ocr[] = {0x40U, 0xFFU, 0x80U, 0x00U};
    mock_hal_push_bytes(ocr, 4);
}

/* Push OCR bytes without CCS bit → SDSC. */
static inline void push_ocr_sdsc(void) {
    static const uint8_t ocr[] = {0x00U, 0xFFU, 0x80U, 0x00U};
    mock_hal_push_bytes(ocr, 4);
}

/* Push the data start token 0xFE so SD_WaitDataToken succeeds immediately. */
static inline void push_data_token(void) {
    mock_hal_push_byte(0xFEU);
}

/* Push two dummy CRC bytes after a data block. */
static inline void push_crc(void) {
    mock_hal_push_byte(0xFFU);
    mock_hal_push_byte(0xFFU);
}

/*
 * Push a 16-byte SDHC (CSD v2) register encoding capacity_blocks blocks.
 *
 * CSD v2 capacity: blocks = (c_size + 1) * 1024
 * c_size is encoded in bytes [7..9] as a 22-bit field:
 *   byte[7] bits[5:0]  = c_size[21:16]
 *   byte[8]            = c_size[15:8]
 *   byte[9]            = c_size[7:0]
 */
static inline void push_csd_sdhc(uint32_t capacity_blocks) {
    uint32_t c_size = (capacity_blocks / 1024U);
    if (c_size > 0U) c_size -= 1U;

    uint8_t csd[16];
    memset(csd, 0, sizeof(csd));
    csd[0] = 0x40U;                          /* CSD_STRUCTURE = 1 */
    csd[7] = (uint8_t)((c_size >> 16) & 0x3FU);
    csd[8] = (uint8_t)((c_size >>  8) & 0xFFU);
    csd[9] = (uint8_t)( c_size        & 0xFFU);
    mock_hal_push_bytes(csd, 16);
}

/*
 * Push a 16-byte SDSC (CSD v1) register encoding 8192 512-byte blocks (4 MB).
 * Uses fixed parameters: READ_BL_LEN=9, C_SIZE=1023, C_SIZE_MULT=1 → 8192 blocks.
 */
static inline void push_csd_sdsc_4mb(void) {
    static const uint8_t csd[16] = {
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x09U,        /* READ_BL_LEN = 9 */
        0x00U,        /* c_size[11:10] = 0 */
        0xFFU,        /* c_size[9:2]   = 0xFF */
        0xC0U,        /* c_size[1:0]   = 0b11; c_size_mult[2:1] = 0 */
        0x00U,        /* c_size_mult[2:1] = 0 */
        0x80U,        /* c_size_mult[0]   = 1 (bit 7) */
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U
    };
    mock_hal_push_bytes(csd, 16);
}

/*
 * Push a SendCommand exchange: WaitReady byte + R1 response byte.
 * This is the minimum required for every SD command in happy-path tests.
 */
static inline void push_cmd_exchange(uint8_t r1) {
    push_wait_ready();
    push_r1(r1);
}

/* -----------------------------------------------------------------------
 * Composite initialization sequences
 * ----------------------------------------------------------------------- */

/*
 * Load the SPI queue with all bytes required for a successful SDHC
 * initialization (SD_SPI_Init happy path).
 *
 * Sequence: CMD0 → CMD8 → CMD55+ACMD41 → CMD58 → CMD9 (CSD)
 */
static inline void push_sdhc_init(uint32_t capacity_blocks) {
    push_cmd_exchange(0x01U);             /* CMD0:   R1=0x01 in-idle     */
    push_cmd_exchange(0x01U);             /* CMD8:   R1=0x01 in-idle     */
    push_r7_sdv2();                       /* CMD8:   R7 echo             */
    push_cmd_exchange(0x01U);             /* CMD55:  R1=0x01 in-idle     */
    push_cmd_exchange(0x00U);             /* ACMD41: R1=0x00 ready       */
    push_cmd_exchange(0x00U);             /* CMD58:  R1=0x00             */
    push_ocr_sdhc();                      /* CMD58:  OCR (SDHC)          */
    push_cmd_exchange(0x00U);             /* CMD9:   R1=0x00             */
    push_data_token();                    /* CMD9:   data start token    */
    push_csd_sdhc(capacity_blocks);       /* CMD9:   16-byte CSD         */
    push_crc();                           /* CMD9:   2-byte CRC          */
}

/*
 * Load the SPI queue for a successful SDSC initialization.
 * Includes CMD16 (set block length) that SDHC does not need.
 */
static inline void push_sdsc_init(void) {
    push_cmd_exchange(0x01U);             /* CMD0:   R1=0x01 in-idle     */
    push_cmd_exchange(0x05U);             /* CMD8:   R1=0x05 illegal cmd */
    push_r7_sdv1();                       /* CMD8:   R7 (ignored)        */
    push_cmd_exchange(0x01U);             /* CMD55:  R1=0x01             */
    push_cmd_exchange(0x00U);             /* ACMD41: R1=0x00 ready       */
    push_cmd_exchange(0x00U);             /* CMD58:  R1=0x00             */
    push_ocr_sdsc();                      /* CMD58:  OCR (SDSC)          */
    push_cmd_exchange(0x00U);             /* CMD16:  R1=0x00             */
    push_cmd_exchange(0x00U);             /* CMD9:   R1=0x00             */
    push_data_token();                    /* CMD9:   data start token    */
    push_csd_sdsc_4mb();                  /* CMD9:   16-byte CSD         */
    push_crc();                           /* CMD9:   2-byte CRC          */
}

/* -----------------------------------------------------------------------
 * Single-block read / write helpers
 * ----------------------------------------------------------------------- */

/*
 * Push bytes for one CMD17 read of 512 bytes.
 * fill_byte: value written into all 512 data positions.
 */
static inline void push_single_read(uint8_t fill_byte) {
    uint8_t data[512];
    memset(data, fill_byte, sizeof(data));
    push_cmd_exchange(0x00U);    /* CMD17 response */
    push_data_token();           /* data start token */
    mock_hal_push_bytes(data, 512);
    push_crc();
}

/*
 * Push bytes for one CMD24 write that is accepted by the card.
 * Write data comes from the caller's buffer (transmit-only, not in queue).
 * The queue only needs the card's data-response token and the WaitReady byte.
 */
static inline void push_single_write_accepted(void) {
    push_cmd_exchange(0x00U);   /* CMD24 response */
    mock_hal_push_byte(0x05U);  /* data response: accepted */
    push_wait_ready();          /* write complete */
}

static inline void push_single_write_crc_error(void) {
    push_cmd_exchange(0x00U);
    mock_hal_push_byte(0x0BU);  /* data response: CRC error */
}

static inline void push_single_write_write_error(void) {
    push_cmd_exchange(0x00U);
    mock_hal_push_byte(0x0DU);  /* data response: write error */
}

/* -----------------------------------------------------------------------
 * Init + first operation helpers
 * ----------------------------------------------------------------------- */

/*
 * Fully initialize an SD_Handle_t and perform SDHC card init.
 * Returns SD_OK on success; aborts the test on failure.
 */
static inline SD_Status do_sdhc_init(SD_Handle_t *sd, uint32_t capacity_blocks) {
    SD_Status s = SD_Init(sd, &g_test_hspi, &g_test_cs, 0, false);
    if (s != SD_OK) return s;
    push_sdhc_init(capacity_blocks);
    return SD_SPI_Init(sd);
}

static inline SD_Status do_sdsc_init(SD_Handle_t *sd) {
    SD_Status s = SD_Init(sd, &g_test_hspi, &g_test_cs, 0, false);
    if (s != SD_OK) return s;
    push_sdsc_init();
    return SD_SPI_Init(sd);
}

#endif /* __TEST_HELPERS_H__ */
