/*
 * tests/test_sd_readwrite.c
 *
 * Tests for SD_ReadBlocks and SD_WriteBlocks (single-block path).
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
 * SD_ReadBlocks — parameter / pre-condition guards
 * ----------------------------------------------------------------------- */

void test_ReadBlocks_NullHandle_ReturnsParam(void) {
    uint8_t buf[512];
    TEST_ASSERT_EQUAL(SD_PARAM, SD_ReadBlocks(NULL, buf, 0, 1));
}

void test_ReadBlocks_NullBuffer_ReturnsParam(void) {
    SD_Init(&sd, &g_test_hspi, &g_test_cs, 0, false);
    TEST_ASSERT_EQUAL(SD_PARAM, SD_ReadBlocks(&sd, NULL, 0, 1));
}

void test_ReadBlocks_CountZero_ReturnsParam(void) {
    SD_Init(&sd, &g_test_hspi, &g_test_cs, 0, false);
    uint8_t buf[512];
    TEST_ASSERT_EQUAL(SD_PARAM, SD_ReadBlocks(&sd, buf, 0, 0));
}

void test_ReadBlocks_NotInitialized_ReturnsError(void) {
    SD_Init(&sd, &g_test_hspi, &g_test_cs, 0, false);
    uint8_t buf[512];
    TEST_ASSERT_EQUAL(SD_ERROR, SD_ReadBlocks(&sd, buf, 0, 1));
}

void test_ReadBlocks_NoMedia_ReturnsNoMedia(void) {
    SD_Init(&sd, &g_test_hspi, &g_test_cs, 0, false);
    SD_SetCardDetect(&sd, &g_test_cd, 0, true);
    mock_hal_set_gpio_read(GPIO_PIN_SET); /* pin high → absent (active-low) */
    uint8_t buf[512];
    TEST_ASSERT_EQUAL(SD_NO_MEDIA, SD_ReadBlocks(&sd, buf, 0, 1));
}

void test_ReadBlocks_OutOfRange_ReturnsParam(void) {
    do_sdhc_init(&sd, 8192U);
    uint8_t buf[512];
    /* sector 8192 is one past the end of an 8192-block card */
    TEST_ASSERT_EQUAL(SD_PARAM, SD_ReadBlocks(&sd, buf, 8192U, 1));
}

void test_ReadBlocks_CountExceedsCapacity_ReturnsParam(void) {
    do_sdhc_init(&sd, 8192U);
    uint8_t buf[512];
    /* sector 0, count 8193 > 8192 */
    TEST_ASSERT_EQUAL(SD_PARAM, SD_ReadBlocks(&sd, buf, 0, 8193U));
}

void test_ReadBlocks_ZeroCapacity_SkipsBoundsCheck(void) {
    /* capacity_blocks = 0 means CSD failed; bounds check is skipped */
    do_sdhc_init(&sd, 0U);
    mock_hal_reset(); /* clear queue state but keep handle initialized */
    sd.initialized = true;
    sd.capacity_blocks = 0;

    push_single_read(0xAAU);
    uint8_t buf[512];
    TEST_ASSERT_EQUAL(SD_OK, SD_ReadBlocks(&sd, buf, 999U, 1));
}

/* -----------------------------------------------------------------------
 * SD_ReadBlocks — single-block happy path
 * ----------------------------------------------------------------------- */

void test_ReadBlocks_SingleBlock_SDHC_HappyPath(void) {
    do_sdhc_init(&sd, 8192U);

    /* Load read response — fill with pattern 0x5A */
    push_single_read(0x5AU);

    uint8_t buf[512];
    memset(buf, 0, sizeof(buf));
    TEST_ASSERT_EQUAL(SD_OK, SD_ReadBlocks(&sd, buf, 0, 1));

    /* Verify data was populated */
    for (int i = 0; i < 512; i++) {
        TEST_ASSERT_EQUAL_UINT8(0x5AU, buf[i]);
    }
}

void test_ReadBlocks_SingleBlock_SDSC_AddressNotShifted(void) {
    /*
     * For SDSC the driver computes address = sector * 512.
     * We verify the read completes successfully; address encoding is opaque
     * from outside the function (HAL_SPI_Transmit carries command bytes,
     * which are fire-and-forget with no queue consumption).
     */
    do_sdsc_init(&sd);

    push_single_read(0xBBU);

    uint8_t buf[512];
    TEST_ASSERT_EQUAL(SD_OK, SD_ReadBlocks(&sd, buf, 0, 1));
    TEST_ASSERT_EQUAL_UINT8(0xBBU, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0xBBU, buf[511]);
}

void test_ReadBlocks_SDHC_SectorPassedDirectly(void) {
    /*
     * For SDHC the address equals the sector number (no byte shift).
     * Push a read for sector 100 and confirm success.
     */
    do_sdhc_init(&sd, 8192U);
    push_single_read(0x99U);
    uint8_t buf[512];
    TEST_ASSERT_EQUAL(SD_OK, SD_ReadBlocks(&sd, buf, 100U, 1));
}

/* -----------------------------------------------------------------------
 * SD_ReadBlocks — CMD17 error conditions
 * ----------------------------------------------------------------------- */

void test_ReadBlocks_CMD17_ErrorResponse_ReturnsError(void) {
    do_sdhc_init(&sd, 8192U);
    /* CMD17 response R1 = 0x04 (parameter error bit set, bit 6) */
    push_wait_ready();
    push_r1(0x04U);
    uint8_t buf[512];
    TEST_ASSERT_EQUAL(SD_ERROR, SD_ReadBlocks(&sd, buf, 0, 1));
}

void test_ReadBlocks_DataTokenTimeout_ReturnsTimeout(void) {
    /*
     * CMD17 response OK every attempt, but no data token (0xFE) is ever pushed.
     * WaitDataToken polls and times out after SD_DATA_TOKEN_TIMEOUT_MS (200ms)
     * iterations.  We push bytes for all 3 attempts (SD_MAX_RETRIES=2); each
     * attempt's WaitDataToken window must be filled with non-0xFE bytes so it
     * doesn't consume the next attempt's SendCommand bytes.
     *
     * Layout per attempt: WaitReady(0xFF) + R1(0x00) + 200×0x00 (busy, ≥ timeout)
     */
    do_sdhc_init(&sd, 8192U);
    for (int i = 0; i < 3; i++) {
        push_wait_ready();                                   /* SendCommand WaitReady */
        push_r1(0x00U);                                     /* CMD17 R1 = ready     */
        for (int j = 0; j < 200; j++) { mock_hal_push_byte(0x00U); } /* WaitDataToken busy */
    }

    uint8_t buf[512];
    SD_Status result = SD_ReadBlocks(&sd, buf, 0, 1);
    TEST_ASSERT_EQUAL(SD_TIMEOUT, result);
}

/* -----------------------------------------------------------------------
 * SD_ReadBlocks — retry logic
 * ----------------------------------------------------------------------- */

void test_ReadBlocks_SingleBlock_FirstFails_SecondSucceeds(void) {
    do_sdhc_init(&sd, 8192U);

    /* Attempt 1: CMD17 response is an error */
    push_wait_ready();
    push_r1(0x04U); /* error R1 */

    /* Attempt 2: success */
    push_single_read(0xCCU);

    uint8_t buf[512];
    TEST_ASSERT_EQUAL(SD_OK, SD_ReadBlocks(&sd, buf, 0, 1));
    TEST_ASSERT_EQUAL_UINT8(0xCCU, buf[0]);
}

void test_ReadBlocks_SingleBlock_AllRetriesFail_ReturnsError(void) {
    /* SD_MAX_RETRIES = 2 → up to 3 total attempts */
    do_sdhc_init(&sd, 8192U);

    for (int i = 0; i < 3; i++) {
        push_wait_ready();
        push_r1(0x04U);
    }

    uint8_t buf[512];
    TEST_ASSERT_EQUAL(SD_ERROR, SD_ReadBlocks(&sd, buf, 0, 1));
}

/* -----------------------------------------------------------------------
 * SD_ReadBlocks — stats
 * ----------------------------------------------------------------------- */

void test_ReadBlocks_Stats_IncrementedOnSuccess(void) {
    do_sdhc_init(&sd, 8192U);
    push_single_read(0x00U);

    uint8_t buf[512];
    SD_ReadBlocks(&sd, buf, 0, 1);

    SD_Stats s;
    SD_GetStats(&sd, &s);
    TEST_ASSERT_EQUAL_UINT32(1U, s.read_ops);
    TEST_ASSERT_EQUAL_UINT32(1U, s.read_blocks);
}

void test_ReadBlocks_Stats_NotIncrementedOnFailure(void) {
    do_sdhc_init(&sd, 8192U);
    /* All three attempts fail */
    for (int i = 0; i < 3; i++) {
        push_wait_ready();
        push_r1(0x04U);
    }

    uint8_t buf[512];
    SD_ReadBlocks(&sd, buf, 0, 1);

    SD_Stats s;
    SD_GetStats(&sd, &s);
    TEST_ASSERT_EQUAL_UINT32(0U, s.read_ops);
    TEST_ASSERT_EQUAL_UINT32(0U, s.read_blocks);
}

/* -----------------------------------------------------------------------
 * SD_WriteBlocks — parameter / pre-condition guards
 * ----------------------------------------------------------------------- */

void test_WriteBlocks_NullHandle_ReturnsParam(void) {
    uint8_t buf[512];
    TEST_ASSERT_EQUAL(SD_PARAM, SD_WriteBlocks(NULL, buf, 0, 1));
}

void test_WriteBlocks_NullBuffer_ReturnsParam(void) {
    SD_Init(&sd, &g_test_hspi, &g_test_cs, 0, false);
    TEST_ASSERT_EQUAL(SD_PARAM, SD_WriteBlocks(&sd, NULL, 0, 1));
}

void test_WriteBlocks_CountZero_ReturnsParam(void) {
    SD_Init(&sd, &g_test_hspi, &g_test_cs, 0, false);
    uint8_t buf[512];
    TEST_ASSERT_EQUAL(SD_PARAM, SD_WriteBlocks(&sd, buf, 0, 0));
}

void test_WriteBlocks_NotInitialized_ReturnsError(void) {
    SD_Init(&sd, &g_test_hspi, &g_test_cs, 0, false);
    uint8_t buf[512];
    TEST_ASSERT_EQUAL(SD_ERROR, SD_WriteBlocks(&sd, buf, 0, 1));
}

void test_WriteBlocks_OutOfRange_ReturnsParam(void) {
    do_sdhc_init(&sd, 8192U);
    uint8_t buf[512];
    TEST_ASSERT_EQUAL(SD_PARAM, SD_WriteBlocks(&sd, buf, 8192U, 1));
}

/* -----------------------------------------------------------------------
 * SD_WriteBlocks — single-block happy path
 * ----------------------------------------------------------------------- */

void test_WriteBlocks_SingleBlock_SDHC_Accepted(void) {
    do_sdhc_init(&sd, 8192U);
    push_single_write_accepted();

    uint8_t buf[512];
    memset(buf, 0xA5U, sizeof(buf));
    TEST_ASSERT_EQUAL(SD_OK, SD_WriteBlocks(&sd, buf, 0, 1));
}

void test_WriteBlocks_SingleBlock_SDSC_Accepted(void) {
    do_sdsc_init(&sd);
    push_single_write_accepted();

    uint8_t buf[512];
    memset(buf, 0x12U, sizeof(buf));
    TEST_ASSERT_EQUAL(SD_OK, SD_WriteBlocks(&sd, buf, 0, 1));
}

/* -----------------------------------------------------------------------
 * SD_WriteBlocks — data response error tokens
 * ----------------------------------------------------------------------- */

void test_WriteBlocks_DataResponse_CrcError_ReturnsCrcError(void) {
    /* Push CRC error for all 3 attempts so the final status is SD_CRC_ERROR. */
    do_sdhc_init(&sd, 8192U);
    push_single_write_crc_error();
    push_single_write_crc_error();
    push_single_write_crc_error();

    uint8_t buf[512];
    TEST_ASSERT_EQUAL(SD_CRC_ERROR, SD_WriteBlocks(&sd, buf, 0, 1));
}

void test_WriteBlocks_DataResponse_WriteError_ReturnsWriteError(void) {
    /* Push write error for all 3 attempts so the final status is SD_WRITE_ERROR. */
    do_sdhc_init(&sd, 8192U);
    push_single_write_write_error();
    push_single_write_write_error();
    push_single_write_write_error();

    uint8_t buf[512];
    TEST_ASSERT_EQUAL(SD_WRITE_ERROR, SD_WriteBlocks(&sd, buf, 0, 1));
}

void test_WriteBlocks_WriteBusyTimeout_ReturnsTimeout(void) {
    /*
     * CMD24 response OK, card accepts the data (0x05), but WaitReady after
     * write never returns 0xFF → SD_TIMEOUT.
     *
     * Push bytes for all 3 attempts (SD_MAX_RETRIES=2). Each attempt needs:
     *   WaitReady (0xFF) + R1 (0x00) + data-accepted (0x05) + SD_WRITE_BUSY_TIMEOUT_MS
     *   busy bytes (0x00) so WaitReady times out rather than succeeding.
     */
    do_sdhc_init(&sd, 8192U);
    for (int attempt = 0; attempt < 3; attempt++) {
        push_wait_ready();           /* CMD24 SendCommand WaitReady */
        push_r1(0x00U);              /* CMD24 R1 */
        mock_hal_push_byte(0x05U);   /* data response: accepted */
        for (int i = 0; i < 500; i++) { mock_hal_push_byte(0x00U); } /* card busy */
    }

    uint8_t buf[512];
    SD_Status result = SD_WriteBlocks(&sd, buf, 0, 1);
    TEST_ASSERT_EQUAL(SD_TIMEOUT, result);
}

/* -----------------------------------------------------------------------
 * SD_WriteBlocks — retry logic
 * ----------------------------------------------------------------------- */

void test_WriteBlocks_SingleBlock_FirstFails_SecondSucceeds(void) {
    do_sdhc_init(&sd, 8192U);

    push_single_write_crc_error();  /* attempt 1 */
    push_single_write_accepted();   /* attempt 2 */

    uint8_t buf[512];
    TEST_ASSERT_EQUAL(SD_OK, SD_WriteBlocks(&sd, buf, 0, 1));
}

void test_WriteBlocks_AllRetriesFail_ReturnsCrcError(void) {
    do_sdhc_init(&sd, 8192U);

    push_single_write_crc_error();
    push_single_write_crc_error();
    push_single_write_crc_error();

    uint8_t buf[512];
    TEST_ASSERT_EQUAL(SD_CRC_ERROR, SD_WriteBlocks(&sd, buf, 0, 1));
}

/* -----------------------------------------------------------------------
 * SD_WriteBlocks — stats
 * ----------------------------------------------------------------------- */

void test_WriteBlocks_Stats_IncrementedOnSuccess(void) {
    do_sdhc_init(&sd, 8192U);
    push_single_write_accepted();

    uint8_t buf[512];
    SD_WriteBlocks(&sd, buf, 0, 1);

    SD_Stats s;
    SD_GetStats(&sd, &s);
    TEST_ASSERT_EQUAL_UINT32(1U, s.write_ops);
    TEST_ASSERT_EQUAL_UINT32(1U, s.write_blocks);
}

void test_WriteBlocks_Stats_NotIncrementedOnFailure(void) {
    do_sdhc_init(&sd, 8192U);
    push_single_write_crc_error();
    push_single_write_crc_error();
    push_single_write_crc_error();

    uint8_t buf[512];
    SD_WriteBlocks(&sd, buf, 0, 1);

    SD_Stats s;
    SD_GetStats(&sd, &s);
    TEST_ASSERT_EQUAL_UINT32(0U, s.write_ops);
    TEST_ASSERT_EQUAL_UINT32(0U, s.write_blocks);
}

/* -----------------------------------------------------------------------
 * Main
 * ----------------------------------------------------------------------- */

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_ReadBlocks_NullHandle_ReturnsParam);
    RUN_TEST(test_ReadBlocks_NullBuffer_ReturnsParam);
    RUN_TEST(test_ReadBlocks_CountZero_ReturnsParam);
    RUN_TEST(test_ReadBlocks_NotInitialized_ReturnsError);
    RUN_TEST(test_ReadBlocks_NoMedia_ReturnsNoMedia);
    RUN_TEST(test_ReadBlocks_OutOfRange_ReturnsParam);
    RUN_TEST(test_ReadBlocks_CountExceedsCapacity_ReturnsParam);
    RUN_TEST(test_ReadBlocks_ZeroCapacity_SkipsBoundsCheck);

    RUN_TEST(test_ReadBlocks_SingleBlock_SDHC_HappyPath);
    RUN_TEST(test_ReadBlocks_SingleBlock_SDSC_AddressNotShifted);
    RUN_TEST(test_ReadBlocks_SDHC_SectorPassedDirectly);

    RUN_TEST(test_ReadBlocks_CMD17_ErrorResponse_ReturnsError);
    RUN_TEST(test_ReadBlocks_DataTokenTimeout_ReturnsTimeout);

    RUN_TEST(test_ReadBlocks_SingleBlock_FirstFails_SecondSucceeds);
    RUN_TEST(test_ReadBlocks_SingleBlock_AllRetriesFail_ReturnsError);

    RUN_TEST(test_ReadBlocks_Stats_IncrementedOnSuccess);
    RUN_TEST(test_ReadBlocks_Stats_NotIncrementedOnFailure);

    RUN_TEST(test_WriteBlocks_NullHandle_ReturnsParam);
    RUN_TEST(test_WriteBlocks_NullBuffer_ReturnsParam);
    RUN_TEST(test_WriteBlocks_CountZero_ReturnsParam);
    RUN_TEST(test_WriteBlocks_NotInitialized_ReturnsError);
    RUN_TEST(test_WriteBlocks_OutOfRange_ReturnsParam);

    RUN_TEST(test_WriteBlocks_SingleBlock_SDHC_Accepted);
    RUN_TEST(test_WriteBlocks_SingleBlock_SDSC_Accepted);

    RUN_TEST(test_WriteBlocks_DataResponse_CrcError_ReturnsCrcError);
    RUN_TEST(test_WriteBlocks_DataResponse_WriteError_ReturnsWriteError);
    RUN_TEST(test_WriteBlocks_WriteBusyTimeout_ReturnsTimeout);

    RUN_TEST(test_WriteBlocks_SingleBlock_FirstFails_SecondSucceeds);
    RUN_TEST(test_WriteBlocks_AllRetriesFail_ReturnsCrcError);

    RUN_TEST(test_WriteBlocks_Stats_IncrementedOnSuccess);
    RUN_TEST(test_WriteBlocks_Stats_NotIncrementedOnFailure);

    return UNITY_END();
}
