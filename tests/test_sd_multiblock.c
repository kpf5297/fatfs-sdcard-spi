/*
 * tests/test_sd_multiblock.c
 *
 * Tests for the multi-block read/write paths:
 *   SD_ReadBlocks  (count > 1 → CMD18)
 *   SD_WriteBlocks (count > 1 → CMD25)
 *   SD_ReadMultiBlocks  (public direct-call API)
 *   SD_WriteMultiBlocks (public direct-call API)
 */

#include "unity.h"
#include "mock_hal.h"
#include "test_helpers.h"
#include "sd_spi.h"
#include <string.h>

static SD_Handle_t sd;

void setUp(void) {
    mock_hal_reset();
    memset(&sd, 0, sizeof(sd));
}

void tearDown(void) {}

/* -----------------------------------------------------------------------
 * Helpers: push a complete CMD18 multi-block read sequence
 * ----------------------------------------------------------------------- */

/*
 * Load the queue for a successful CMD18 read of `count` blocks.
 * Each block: data token (0xFE) + 512 bytes (fill_byte) + 2 CRC bytes.
 * Terminated with a CMD12 exchange.
 */
static void push_multi_read(uint32_t count, uint8_t fill_byte) {
    uint8_t data[512];
    memset(data, fill_byte, sizeof(data));

    push_cmd_exchange(0x00U); /* CMD18 response */

    for (uint32_t i = 0; i < count; i++) {
        push_data_token();
        mock_hal_push_bytes(data, 512);
        push_crc();
    }

    push_cmd_exchange(0x00U); /* CMD12 (stop transmission) response */
}

/*
 * Load the queue for a successful CMD25 write of `count` blocks.
 * For each block: CMD24-style data response (0x05) + WaitReady (0xFF).
 * Terminated with the stop token (transmitted, no rx) + final WaitReady.
 */
static void push_multi_write(uint32_t count) {
    push_cmd_exchange(0x00U); /* CMD25 response */

    for (uint32_t i = 0; i < count; i++) {
        mock_hal_push_byte(0x05U); /* data accepted */
        push_wait_ready();         /* card not busy after block write */
    }

    /* Stop token 0xFD is transmitted (no rx), but WaitReady after stop needs
       one 0xFF in the queue. */
    push_wait_ready();
}

/* -----------------------------------------------------------------------
 * Multi-block read via SD_ReadBlocks (count > 1)
 * ----------------------------------------------------------------------- */

void test_ReadBlocks_MultiBlock_TwoBlocks_HappyPath(void) {
    do_sdhc_init(&sd, 8192U);
    push_multi_read(2, 0x77U);

    uint8_t buf[1024];
    memset(buf, 0, sizeof(buf));
    TEST_ASSERT_EQUAL(SD_OK, SD_ReadBlocks(&sd, buf, 0, 2));

    /* Both blocks should contain 0x77 */
    for (int i = 0; i < 1024; i++) {
        TEST_ASSERT_EQUAL_UINT8(0x77U, buf[i]);
    }
}

void test_ReadBlocks_MultiBlock_ThreeBlocks_HappyPath(void) {
    do_sdhc_init(&sd, 8192U);
    push_multi_read(3, 0x33U);

    uint8_t buf[1536];
    TEST_ASSERT_EQUAL(SD_OK, SD_ReadBlocks(&sd, buf, 0, 3));
    TEST_ASSERT_EQUAL_UINT8(0x33U, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x33U, buf[1535]);
}

void test_ReadBlocks_MultiBlock_StatsUpdated(void) {
    do_sdhc_init(&sd, 8192U);
    push_multi_read(3, 0x00U);

    uint8_t buf[1536];
    SD_ReadBlocks(&sd, buf, 0, 3);

    SD_Stats s;
    SD_GetStats(&sd, &s);
    TEST_ASSERT_EQUAL_UINT32(1U, s.read_ops);
    TEST_ASSERT_EQUAL_UINT32(3U, s.read_blocks);
}

void test_ReadBlocks_MultiBlock_CMD18_ErrorResponse_ReturnsError(void) {
    do_sdhc_init(&sd, 8192U);

    /* CMD18 response has bit 2 set (parameter error) */
    push_wait_ready();
    push_r1(0x04U);

    uint8_t buf[1024];
    TEST_ASSERT_EQUAL(SD_ERROR, SD_ReadBlocks(&sd, buf, 0, 2));
}

void test_ReadBlocks_MultiBlock_SecondBlockTokenFail_ReturnsTimeout(void) {
    /*
     * First block succeeds; second block's data token never arrives
     * (queue empties → 0xFF returned, which is never 0xFE).
     * CMD12 is still sent after the loop (stop token).
     */
    do_sdhc_init(&sd, 8192U);

    push_cmd_exchange(0x00U); /* CMD18 response */

    /* Block 1: complete */
    uint8_t data[512];
    memset(data, 0x11U, sizeof(data));
    push_data_token();
    mock_hal_push_bytes(data, 512);
    push_crc();

    /* Block 2: no token — WaitDataToken will time out */
    /* (CMD12 WaitReady+response still needed after the loop) */
    push_cmd_exchange(0x00U); /* CMD12 */

    uint8_t buf[1024];
    SD_Status result = SD_ReadBlocks(&sd, buf, 0, 2);
    TEST_ASSERT_EQUAL(SD_TIMEOUT, result);

    /* First block was successfully read */
    TEST_ASSERT_EQUAL_UINT8(0x11U, buf[0]);
}

void test_ReadBlocks_MultiBlock_SDSC_HappyPath(void) {
    do_sdsc_init(&sd);
    push_multi_read(2, 0x44U);

    uint8_t buf[1024];
    TEST_ASSERT_EQUAL(SD_OK, SD_ReadBlocks(&sd, buf, 0, 2));
    TEST_ASSERT_EQUAL_UINT8(0x44U, buf[0]);
}

/* -----------------------------------------------------------------------
 * Multi-block write via SD_WriteBlocks (count > 1)
 * ----------------------------------------------------------------------- */

void test_WriteBlocks_MultiBlock_TwoBlocks_HappyPath(void) {
    do_sdhc_init(&sd, 8192U);
    push_multi_write(2);

    uint8_t buf[1024];
    memset(buf, 0xA5U, sizeof(buf));
    TEST_ASSERT_EQUAL(SD_OK, SD_WriteBlocks(&sd, buf, 0, 2));
}

void test_WriteBlocks_MultiBlock_StatsUpdated(void) {
    do_sdhc_init(&sd, 8192U);
    push_multi_write(2);

    uint8_t buf[1024];
    SD_WriteBlocks(&sd, buf, 0, 2);

    SD_Stats s;
    SD_GetStats(&sd, &s);
    TEST_ASSERT_EQUAL_UINT32(1U, s.write_ops);
    TEST_ASSERT_EQUAL_UINT32(2U, s.write_blocks);
}

void test_WriteBlocks_MultiBlock_FirstBlockCrcError_ReturnsError(void) {
    do_sdhc_init(&sd, 8192U);

    push_cmd_exchange(0x00U);   /* CMD25 response */
    mock_hal_push_byte(0x0BU);  /* block 1 data response: CRC error */
    /* loop breaks — stop token is still sent, then WaitReady */
    push_wait_ready();

    uint8_t buf[1024];
    TEST_ASSERT_EQUAL(SD_CRC_ERROR, SD_WriteBlocks(&sd, buf, 0, 2));
}

void test_WriteBlocks_MultiBlock_SecondBlockWriteError(void) {
    do_sdhc_init(&sd, 8192U);

    push_cmd_exchange(0x00U);   /* CMD25 response */
    mock_hal_push_byte(0x05U);  /* block 1 accepted */
    push_wait_ready();
    mock_hal_push_byte(0x0DU);  /* block 2 write error */
    push_wait_ready();          /* stop token WaitReady */

    uint8_t buf[1024];
    TEST_ASSERT_EQUAL(SD_WRITE_ERROR, SD_WriteBlocks(&sd, buf, 0, 2));
}

/* -----------------------------------------------------------------------
 * SD_ReadMultiBlocks — direct public API (bypasses single-block path)
 * ----------------------------------------------------------------------- */

void test_ReadMultiBlocks_NotInitialized_ReturnsError(void) {
    /* This is the bug we fixed: SD_ReadMultiBlocks lacked the initialized check */
    SD_Init(&sd, &g_test_hspi, &g_test_cs, 0, false);
    /* do NOT call SD_SPI_Init — leave initialized = false */
    uint8_t buf[512];
    TEST_ASSERT_EQUAL(SD_ERROR, SD_ReadMultiBlocks(&sd, buf, 0, 1));
}

void test_ReadMultiBlocks_NullHandle_ReturnsParam(void) {
    uint8_t buf[512];
    TEST_ASSERT_EQUAL(SD_PARAM, SD_ReadMultiBlocks(NULL, buf, 0, 1));
}

void test_ReadMultiBlocks_NullBuffer_ReturnsParam(void) {
    SD_Init(&sd, &g_test_hspi, &g_test_cs, 0, false);
    TEST_ASSERT_EQUAL(SD_PARAM, SD_ReadMultiBlocks(&sd, NULL, 0, 1));
}

void test_ReadMultiBlocks_HappyPath(void) {
    do_sdhc_init(&sd, 8192U);
    push_multi_read(2, 0x55U);

    uint8_t buf[1024];
    TEST_ASSERT_EQUAL(SD_OK, SD_ReadMultiBlocks(&sd, buf, 0, 2));
    TEST_ASSERT_EQUAL_UINT8(0x55U, buf[0]);
}

/* -----------------------------------------------------------------------
 * SD_WriteMultiBlocks — direct public API
 * ----------------------------------------------------------------------- */

void test_WriteMultiBlocks_NotInitialized_HappyPath(void) {
    /* WriteMultiBlocks does not have a separate initialized check at the
       public level; it delegates to WriteMultiBlocksInternal which does check.
       Verify SD_ERROR is returned when not initialized. */
    SD_Init(&sd, &g_test_hspi, &g_test_cs, 0, false);
    uint8_t buf[512];
    /* WriteMultiBlocksInternal checks sd_handle->initialized */
    TEST_ASSERT_EQUAL(SD_ERROR, SD_WriteMultiBlocks(&sd, buf, 0, 1));
}

void test_WriteMultiBlocks_NullHandle_ReturnsParam(void) {
    uint8_t buf[512];
    TEST_ASSERT_EQUAL(SD_PARAM, SD_WriteMultiBlocks(NULL, buf, 0, 1));
}

void test_WriteMultiBlocks_HappyPath(void) {
    do_sdhc_init(&sd, 8192U);
    push_multi_write(2);

    uint8_t buf[1024];
    memset(buf, 0xEEU, sizeof(buf));
    TEST_ASSERT_EQUAL(SD_OK, SD_WriteMultiBlocks(&sd, buf, 0, 2));
}

/* -----------------------------------------------------------------------
 * Main
 * ----------------------------------------------------------------------- */

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_ReadBlocks_MultiBlock_TwoBlocks_HappyPath);
    RUN_TEST(test_ReadBlocks_MultiBlock_ThreeBlocks_HappyPath);
    RUN_TEST(test_ReadBlocks_MultiBlock_StatsUpdated);
    RUN_TEST(test_ReadBlocks_MultiBlock_CMD18_ErrorResponse_ReturnsError);
    RUN_TEST(test_ReadBlocks_MultiBlock_SecondBlockTokenFail_ReturnsTimeout);
    RUN_TEST(test_ReadBlocks_MultiBlock_SDSC_HappyPath);

    RUN_TEST(test_WriteBlocks_MultiBlock_TwoBlocks_HappyPath);
    RUN_TEST(test_WriteBlocks_MultiBlock_StatsUpdated);
    RUN_TEST(test_WriteBlocks_MultiBlock_FirstBlockCrcError_ReturnsError);
    RUN_TEST(test_WriteBlocks_MultiBlock_SecondBlockWriteError);

    RUN_TEST(test_ReadMultiBlocks_NotInitialized_ReturnsError);
    RUN_TEST(test_ReadMultiBlocks_NullHandle_ReturnsParam);
    RUN_TEST(test_ReadMultiBlocks_NullBuffer_ReturnsParam);
    RUN_TEST(test_ReadMultiBlocks_HappyPath);

    RUN_TEST(test_WriteMultiBlocks_NotInitialized_HappyPath);
    RUN_TEST(test_WriteMultiBlocks_NullHandle_ReturnsParam);
    RUN_TEST(test_WriteMultiBlocks_HappyPath);

    return UNITY_END();
}
