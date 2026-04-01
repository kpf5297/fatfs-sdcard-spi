/*
 * tests/test_sd_utils.c
 *
 * Tests for SD_Sync, SD_GetStats / SD_ResetStats, and accessor functions.
 * Also covers the error/timeout counter tracking added in the code review.
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
 * SD_Sync
 * ----------------------------------------------------------------------- */

void test_Sync_NullHandle_ReturnsParam(void) {
    TEST_ASSERT_EQUAL(SD_PARAM, SD_Sync(NULL));
}

void test_Sync_NotInitialized_ReturnsError(void) {
    SD_Init(&sd, &g_test_hspi, &g_test_cs, 0, false);
    TEST_ASSERT_EQUAL(SD_ERROR, SD_Sync(&sd));
}

void test_Sync_NoMedia_ReturnsNoMedia(void) {
    SD_Init(&sd, &g_test_hspi, &g_test_cs, 0, false);
    SD_SetCardDetect(&sd, &g_test_cd, 0, true);
    mock_hal_set_gpio_read(GPIO_PIN_SET); /* absent */
    sd.initialized = true;
    TEST_ASSERT_EQUAL(SD_NO_MEDIA, SD_Sync(&sd));
}

void test_Sync_CardReady_ReturnsOk(void) {
    do_sdhc_init(&sd, 8192U);
    /* SD_Sync calls SD_WaitReady with CS asserted: push 0xFF to return ready */
    push_wait_ready();
    TEST_ASSERT_EQUAL(SD_OK, SD_Sync(&sd));
}

void test_Sync_CardBusy_Timeout(void) {
    /*
     * Card never releases the bus (no 0xFF in queue — empty queue returns
     * 0xFF by default, so we need to actually push non-0xFF values to keep
     * WaitReady spinning). Push enough 0x00 bytes to fill the timeout window.
     */
    do_sdhc_init(&sd, 8192U);
    /* All non-0xFF → WaitReady loops; HAL_Delay advances tick until timeout */
    for (int i = 0; i < 1000; i++) {
        mock_hal_push_byte(0x00U);
    }
    TEST_ASSERT_EQUAL(SD_TIMEOUT, SD_Sync(&sd));
}

/* -----------------------------------------------------------------------
 * SD_GetStats / SD_ResetStats
 * ----------------------------------------------------------------------- */

void test_GetStats_NullHandle_NoCrash(void) {
    SD_Stats s;
    SD_GetStats(NULL, &s); /* must not fault */
}

void test_GetStats_NullStats_NoCrash(void) {
    SD_Init(&sd, &g_test_hspi, &g_test_cs, 0, false);
    SD_GetStats(&sd, NULL); /* must not fault */
}

void test_GetStats_ReturnsSnapshot(void) {
    do_sdhc_init(&sd, 8192U);

    /* Perform two successful reads */
    push_single_read(0x00U);
    push_single_read(0x00U);
    uint8_t buf[512];
    SD_ReadBlocks(&sd, buf, 0, 1);
    SD_ReadBlocks(&sd, buf, 1, 1);

    /* Perform one successful write */
    push_single_write_accepted();
    SD_WriteBlocks(&sd, buf, 0, 1);

    SD_Stats s;
    SD_GetStats(&sd, &s);
    TEST_ASSERT_EQUAL_UINT32(2U, s.read_ops);
    TEST_ASSERT_EQUAL_UINT32(2U, s.read_blocks);
    TEST_ASSERT_EQUAL_UINT32(1U, s.write_ops);
    TEST_ASSERT_EQUAL_UINT32(1U, s.write_blocks);
}

void test_ResetStats_ZerosAllCounters(void) {
    do_sdhc_init(&sd, 8192U);

    push_single_read(0x00U);
    uint8_t buf[512];
    SD_ReadBlocks(&sd, buf, 0, 1);

    SD_ResetStats(&sd);

    SD_Stats s;
    SD_GetStats(&sd, &s);
    TEST_ASSERT_EQUAL_UINT32(0U, s.read_ops);
    TEST_ASSERT_EQUAL_UINT32(0U, s.read_blocks);
    TEST_ASSERT_EQUAL_UINT32(0U, s.write_ops);
    TEST_ASSERT_EQUAL_UINT32(0U, s.write_blocks);
    TEST_ASSERT_EQUAL_UINT32(0U, s.error_count);
    TEST_ASSERT_EQUAL_UINT32(0U, s.timeout_count);
}

void test_ResetStats_NullHandle_NoCrash(void) {
    SD_ResetStats(NULL);
}

/* -----------------------------------------------------------------------
 * Error and timeout counters
 * ----------------------------------------------------------------------- */

void test_ErrorCount_IncrementedOnFailedRead(void) {
    do_sdhc_init(&sd, 8192U);

    /* Exhaust all retries with error responses */
    for (int i = 0; i < 3; i++) {
        push_wait_ready();
        push_r1(0x04U);
    }

    uint8_t buf[512];
    SD_ReadBlocks(&sd, buf, 0, 1);

    SD_Stats s;
    SD_GetStats(&sd, &s);
    TEST_ASSERT_GREATER_THAN(0U, s.error_count);
}

void test_TimeoutCount_IncrementedOnTimeout(void) {
    /*
     * Trigger SD_TIMEOUT by letting the data-token wait time out on every attempt.
     * SD_RecordStatus is called once with the final attempt's status; push bytes
     * for all 3 attempts (SD_MAX_RETRIES=2) so the final recorded status is
     * SD_TIMEOUT and timeout_count is incremented.
     */
    do_sdhc_init(&sd, 8192U);

    for (int i = 0; i < 3; i++) {
        push_wait_ready();                                        /* SendCommand WaitReady */
        push_r1(0x00U);                                          /* CMD17 R1 = ready      */
        for (int j = 0; j < 200; j++) { mock_hal_push_byte(0x00U); } /* WaitDataToken busy */
    }

    uint8_t buf[512];
    SD_ReadBlocks(&sd, buf, 0, 1);

    SD_Stats s;
    SD_GetStats(&sd, &s);
    TEST_ASSERT_GREATER_THAN(0U, s.timeout_count);
    TEST_ASSERT_GREATER_THAN(0U, s.error_count); /* timeout is also an error */
}

void test_ErrorCount_NotIncrementedOnSuccess(void) {
    do_sdhc_init(&sd, 8192U);
    push_single_read(0x00U);

    uint8_t buf[512];
    SD_ReadBlocks(&sd, buf, 0, 1);

    SD_Stats s;
    SD_GetStats(&sd, &s);
    TEST_ASSERT_EQUAL_UINT32(0U, s.error_count);
    TEST_ASSERT_EQUAL_UINT32(0U, s.timeout_count);
}

/* -----------------------------------------------------------------------
 * Init attempt counter
 * ----------------------------------------------------------------------- */

void test_InitAttempts_TrackedAcrossMultipleCalls(void) {
    SD_Init(&sd, &g_test_hspi, &g_test_cs, 0, false);

    push_sdhc_init(8192U);
    SD_SPI_Init(&sd);

    push_sdhc_init(8192U);
    SD_SPI_Init(&sd);

    SD_Stats s;
    SD_GetStats(&sd, &s);
    TEST_ASSERT_EQUAL_UINT32(2U, s.init_attempts);
}

/* -----------------------------------------------------------------------
 * Accessor functions
 * ----------------------------------------------------------------------- */

void test_IsSDHC_NullHandle_ReturnsFalse(void) {
    TEST_ASSERT_FALSE(SD_IsSDHC(NULL));
}

void test_IsSDHC_AfterSDHCInit_ReturnsTrue(void) {
    do_sdhc_init(&sd, 8192U);
    TEST_ASSERT_TRUE(SD_IsSDHC(&sd));
}

void test_IsSDHC_AfterSDSCInit_ReturnsFalse(void) {
    do_sdsc_init(&sd);
    TEST_ASSERT_FALSE(SD_IsSDHC(&sd));
}

void test_IsInitialized_NullHandle_ReturnsFalse(void) {
    TEST_ASSERT_FALSE(SD_IsInitialized(NULL));
}

void test_IsInitialized_AfterInit_ReturnsTrue(void) {
    do_sdhc_init(&sd, 8192U);
    TEST_ASSERT_TRUE(SD_IsInitialized(&sd));
}

void test_GetBlockCount_NullHandle_ReturnsZero(void) {
    TEST_ASSERT_EQUAL_UINT32(0U, SD_GetBlockCount(NULL));
}

void test_GetBlockCount_SDSCCard_ReturnsCorrectBlocks(void) {
    do_sdsc_init(&sd);
    TEST_ASSERT_EQUAL_UINT32(8192U, SD_GetBlockCount(&sd));
}

/* -----------------------------------------------------------------------
 * SD_DeInit
 * ----------------------------------------------------------------------- */

void test_DeInit_AfterInit_ClearsState(void) {
    do_sdhc_init(&sd, 8192U);
    TEST_ASSERT_TRUE(sd.initialized);

    SD_DeInit(&sd);
    TEST_ASSERT_FALSE(sd.initialized);
}

void test_DeInit_CanReinitAfterDeInit(void) {
    do_sdhc_init(&sd, 8192U);
    SD_DeInit(&sd);

    /* Re-initialize the same handle */
    mock_hal_reset();
    TEST_ASSERT_EQUAL(SD_OK, do_sdhc_init(&sd, 4096U));
    TEST_ASSERT_TRUE(sd.initialized);
    TEST_ASSERT_EQUAL_UINT32(4096U, sd.capacity_blocks);
}

/* -----------------------------------------------------------------------
 * Tick overflow — SD_Sync path
 * ----------------------------------------------------------------------- */

void test_Sync_TickOverflow_StillTimesOut(void) {
    /*
     * Set tick near UINT32_MAX. With the old overflow bug, the WaitReady
     * deadline wrapped to a small number and the loop would exit after a
     * single iteration. With the fix, unsigned subtraction handles the wrap
     * correctly and the loop runs for the full SD_WRITE_BUSY_TIMEOUT_MS.
     *
     * Push only non-0xFF bytes so WaitReady never finds the idle signal.
     * Verify the call count is > 20 to prove the loop ran past the wrap point.
     */
    do_sdhc_init(&sd, 8192U);
    mock_hal_set_tick(0xFFFFFF00U);

    /* Fill queue with 0x00 (busy signal) */
    for (int i = 0; i < 1000; i++) {
        mock_hal_push_byte(0x00U);
    }

    int calls_before = mock_hal_transmitrec_calls;
    SD_Sync(&sd);
    int calls_after = mock_hal_transmitrec_calls;

    TEST_ASSERT_GREATER_THAN(20, calls_after - calls_before);
}

/* -----------------------------------------------------------------------
 * Main
 * ----------------------------------------------------------------------- */

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_Sync_NullHandle_ReturnsParam);
    RUN_TEST(test_Sync_NotInitialized_ReturnsError);
    RUN_TEST(test_Sync_NoMedia_ReturnsNoMedia);
    RUN_TEST(test_Sync_CardReady_ReturnsOk);
    RUN_TEST(test_Sync_CardBusy_Timeout);

    RUN_TEST(test_GetStats_NullHandle_NoCrash);
    RUN_TEST(test_GetStats_NullStats_NoCrash);
    RUN_TEST(test_GetStats_ReturnsSnapshot);
    RUN_TEST(test_ResetStats_ZerosAllCounters);
    RUN_TEST(test_ResetStats_NullHandle_NoCrash);

    RUN_TEST(test_ErrorCount_IncrementedOnFailedRead);
    RUN_TEST(test_TimeoutCount_IncrementedOnTimeout);
    RUN_TEST(test_ErrorCount_NotIncrementedOnSuccess);

    RUN_TEST(test_InitAttempts_TrackedAcrossMultipleCalls);

    RUN_TEST(test_IsSDHC_NullHandle_ReturnsFalse);
    RUN_TEST(test_IsSDHC_AfterSDHCInit_ReturnsTrue);
    RUN_TEST(test_IsSDHC_AfterSDSCInit_ReturnsFalse);
    RUN_TEST(test_IsInitialized_NullHandle_ReturnsFalse);
    RUN_TEST(test_IsInitialized_AfterInit_ReturnsTrue);
    RUN_TEST(test_GetBlockCount_NullHandle_ReturnsZero);
    RUN_TEST(test_GetBlockCount_SDSCCard_ReturnsCorrectBlocks);

    RUN_TEST(test_DeInit_AfterInit_ClearsState);
    RUN_TEST(test_DeInit_CanReinitAfterDeInit);

    RUN_TEST(test_Sync_TickOverflow_StillTimesOut);

    return UNITY_END();
}
