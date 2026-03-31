/*
 * tests/test_sd_init.c
 *
 * Tests for SD_Init, SD_SPI_Init, SD_DeInit, and SD_SetCardDetect.
 */

#include "unity.h"
#include "mock_hal.h"
#include "test_helpers.h"
#include "sd_spi.h"

static SD_Handle_t sd;

void setUp(void) {
    mock_hal_reset();
    memset(&sd, 0, sizeof(sd));
}

void tearDown(void) {}

/* -----------------------------------------------------------------------
 * SD_Init parameter validation
 * ----------------------------------------------------------------------- */

void test_SD_Init_NullHandle_ReturnsParam(void) {
    TEST_ASSERT_EQUAL(SD_PARAM, SD_Init(NULL, &g_test_hspi, &g_test_cs, 0, false));
}

void test_SD_Init_NullHspi_ReturnsParam(void) {
    TEST_ASSERT_EQUAL(SD_PARAM, SD_Init(&sd, NULL, &g_test_cs, 0, false));
}

void test_SD_Init_NullCsPort_ReturnsParam(void) {
    TEST_ASSERT_EQUAL(SD_PARAM, SD_Init(&sd, &g_test_hspi, NULL, 0, false));
}

void test_SD_Init_Success_SetsFields(void) {
    TEST_ASSERT_EQUAL(SD_OK, SD_Init(&sd, &g_test_hspi, &g_test_cs, 5, false));
    TEST_ASSERT_EQUAL_PTR(&g_test_hspi, sd.hspi);
    TEST_ASSERT_EQUAL_PTR(&g_test_cs,   sd.cs_port);
    TEST_ASSERT_EQUAL_UINT16(5,          sd.cs_pin);
    TEST_ASSERT_FALSE(sd.initialized);
    TEST_ASSERT_FALSE(sd.is_sdhc);
    TEST_ASSERT_FALSE(sd.use_dma);
    TEST_ASSERT_EQUAL_UINT32(512U, sd.block_size);
}

void test_SD_Init_DmaFlag_Stored(void) {
    TEST_ASSERT_EQUAL(SD_OK, SD_Init(&sd, &g_test_hspi, &g_test_cs, 0, true));
    TEST_ASSERT_TRUE(sd.use_dma);
}

/* -----------------------------------------------------------------------
 * SD_DeInit
 * ----------------------------------------------------------------------- */

void test_SD_DeInit_NullHandle_NoFault(void) {
    SD_DeInit(NULL); /* must not crash */
}

void test_SD_DeInit_ClearsInitialized(void) {
    SD_Init(&sd, &g_test_hspi, &g_test_cs, 0, false);
    sd.initialized = true;
    SD_DeInit(&sd);
    TEST_ASSERT_FALSE(sd.initialized);
}

/* -----------------------------------------------------------------------
 * SD_SetCardDetect
 * ----------------------------------------------------------------------- */

void test_SD_SetCardDetect_NullHandle_ReturnsParam(void) {
    TEST_ASSERT_EQUAL(SD_PARAM, SD_SetCardDetect(NULL, &g_test_cd, 0, true));
}

void test_SD_SetCardDetect_NullPort_ReturnsParam(void) {
    SD_Init(&sd, &g_test_hspi, &g_test_cs, 0, false);
    TEST_ASSERT_EQUAL(SD_PARAM, SD_SetCardDetect(&sd, NULL, 0, true));
}

void test_SD_SetCardDetect_Success(void) {
    SD_Init(&sd, &g_test_hspi, &g_test_cs, 0, false);
    TEST_ASSERT_EQUAL(SD_OK, SD_SetCardDetect(&sd, &g_test_cd, 7, true));
    TEST_ASSERT_TRUE(sd.has_cd);
    TEST_ASSERT_EQUAL_PTR(&g_test_cd, sd.cd_port);
    TEST_ASSERT_EQUAL_UINT16(7, sd.cd_pin);
    TEST_ASSERT_TRUE(sd.cd_active_low);
}

/* -----------------------------------------------------------------------
 * SD_IsCardPresent
 * ----------------------------------------------------------------------- */

void test_SD_IsCardPresent_NullHandle_ReturnsFalse(void) {
    TEST_ASSERT_FALSE(SD_IsCardPresent(NULL));
}

void test_SD_IsCardPresent_NoCd_AlwaysTrue(void) {
    SD_Init(&sd, &g_test_hspi, &g_test_cs, 0, false);
    /* has_cd = false → always present regardless of GPIO */
    mock_hal_set_gpio_read(GPIO_PIN_RESET);
    TEST_ASSERT_TRUE(SD_IsCardPresent(&sd));
    mock_hal_set_gpio_read(GPIO_PIN_SET);
    TEST_ASSERT_TRUE(SD_IsCardPresent(&sd));
}

void test_SD_IsCardPresent_ActiveLow_PinLow_Present(void) {
    SD_Init(&sd, &g_test_hspi, &g_test_cs, 0, false);
    SD_SetCardDetect(&sd, &g_test_cd, 0, true); /* active_low = true */
    mock_hal_set_gpio_read(GPIO_PIN_RESET);
    TEST_ASSERT_TRUE(SD_IsCardPresent(&sd));
}

void test_SD_IsCardPresent_ActiveLow_PinHigh_Absent(void) {
    SD_Init(&sd, &g_test_hspi, &g_test_cs, 0, false);
    SD_SetCardDetect(&sd, &g_test_cd, 0, true);
    mock_hal_set_gpio_read(GPIO_PIN_SET);
    TEST_ASSERT_FALSE(SD_IsCardPresent(&sd));
}

void test_SD_IsCardPresent_ActiveHigh_PinHigh_Present(void) {
    SD_Init(&sd, &g_test_hspi, &g_test_cs, 0, false);
    SD_SetCardDetect(&sd, &g_test_cd, 0, false); /* active_low = false */
    mock_hal_set_gpio_read(GPIO_PIN_SET);
    TEST_ASSERT_TRUE(SD_IsCardPresent(&sd));
}

/* -----------------------------------------------------------------------
 * SD_SPI_Init — parameter / pre-condition guards
 * ----------------------------------------------------------------------- */

void test_SD_SPI_Init_NullHandle_ReturnsParam(void) {
    TEST_ASSERT_EQUAL(SD_PARAM, SD_SPI_Init(NULL));
}

void test_SD_SPI_Init_NoMedia_ReturnsNoMedia(void) {
    SD_Init(&sd, &g_test_hspi, &g_test_cs, 0, false);
    SD_SetCardDetect(&sd, &g_test_cd, 0, true); /* active_low */
    mock_hal_set_gpio_read(GPIO_PIN_SET);        /* pin high → absent  */
    TEST_ASSERT_EQUAL(SD_NO_MEDIA, SD_SPI_Init(&sd));
    TEST_ASSERT_FALSE(sd.initialized);
}

void test_SD_SPI_Init_IncrementsInitAttempts(void) {
    SD_Init(&sd, &g_test_hspi, &g_test_cs, 0, false);
    /* Let CMD0 time out immediately by not pushing 0x01 */
    /* All ReceiveByte calls return 0xFF (bit 7=1) → response loop times out,
       then the outer do-while exits after SD_CMD_TIMEOUT_MS.
       We advance the tick inside HAL_Delay, but need to push enough non-idle
       bytes to keep WaitReady spinning. With default 0xFF from empty queue,
       WaitReady immediately succeeds, then SendCommand response loop gets
       0xFF (bit 7=1) for all 10 retries → SD_TIMEOUT.
       The test just checks the counter increments. */
    /* Push nothing — queue returns 0xFF for WaitReady, then 0xFF x10 for
       response polling → SendCommand returns SD_TIMEOUT → CMD0 fails.
       The outer init loop will spin until SD_INIT_TIMEOUT_MS elapses. */
    SD_SPI_Init(&sd); /* ignore return value */
    TEST_ASSERT_GREATER_OR_EQUAL(1U, sd.stats.init_attempts);
}

/* -----------------------------------------------------------------------
 * SD_SPI_Init — happy path: SDHC
 * ----------------------------------------------------------------------- */

void test_SD_SPI_Init_SDHC_HappyPath(void) {
    SD_Init(&sd, &g_test_hspi, &g_test_cs, 0, false);
    push_sdhc_init(8192U);
    TEST_ASSERT_EQUAL(SD_OK, SD_SPI_Init(&sd));
    TEST_ASSERT_TRUE(sd.initialized);
    TEST_ASSERT_TRUE(sd.is_sdhc);
    TEST_ASSERT_EQUAL_UINT32(8192U, sd.capacity_blocks);
}

void test_SD_SPI_Init_SDHC_CapacityParsed(void) {
    /* 32 GB = 61440 * 1024 blocks */
    SD_Init(&sd, &g_test_hspi, &g_test_cs, 0, false);
    push_sdhc_init(62914560U); /* 30720 * 1024 = 31457280 blocks ≈ 16 GB */
    TEST_ASSERT_EQUAL(SD_OK, SD_SPI_Init(&sd));
    TEST_ASSERT_EQUAL_UINT32(62914560U, sd.capacity_blocks);
}

/* -----------------------------------------------------------------------
 * SD_SPI_Init — happy path: SDSC
 * ----------------------------------------------------------------------- */

void test_SD_SPI_Init_SDSC_HappyPath(void) {
    SD_Init(&sd, &g_test_hspi, &g_test_cs, 0, false);
    push_sdsc_init();
    TEST_ASSERT_EQUAL(SD_OK, SD_SPI_Init(&sd));
    TEST_ASSERT_TRUE(sd.initialized);
    TEST_ASSERT_FALSE(sd.is_sdhc);
    TEST_ASSERT_EQUAL_UINT32(8192U, sd.capacity_blocks);
}

/* -----------------------------------------------------------------------
 * SD_SPI_Init — failure paths
 * ----------------------------------------------------------------------- */

void test_SD_SPI_Init_CMD0_NoValidResponse_ReturnsError(void) {
    /*
     * Empty queue → WaitReady gets 0xFF (passes), response loop gets 0xFF
     * for all 10 retries (bit 7 = 1 → invalid) → SendCommand returns SD_TIMEOUT.
     * The CMD0 do-while exits after SD_INIT_TIMEOUT_MS elapses via HAL_Delay.
     */
    SD_Init(&sd, &g_test_hspi, &g_test_cs, 0, false);
    TEST_ASSERT_EQUAL(SD_ERROR, SD_SPI_Init(&sd));
    TEST_ASSERT_FALSE(sd.initialized);
}

void test_SD_SPI_Init_ACMD41_Timeout_ReturnsTimeout(void) {
    /*
     * Push CMD0 and CMD8 success, then let ACMD41 loop never return 0x00.
     * Queue provides 0xFF (WaitReady) + 0x01 (CMD55) + 0xFF (WaitReady) + 0x01
     * (ACMD41 response, never 0x00) for each loop iteration. After
     * SD_INIT_TIMEOUT_MS the loop exits and the function returns SD_TIMEOUT.
     *
     * We push enough pairs for at least the timeout iterations. With
     * SD_INIT_TIMEOUT_MS default 1000 ms and 1 ms per BackoffDelay, push 1100
     * pairs to be safe (some iterations consume two WaitReady + response each).
     */
    SD_Init(&sd, &g_test_hspi, &g_test_cs, 0, false);
    push_cmd_exchange(0x01U);  /* CMD0 */
    push_cmd_exchange(0x01U);  /* CMD8 */
    push_r7_sdv2();

    /* ACMD41 loop: keep returning not-ready */
    for (int i = 0; i < 1100; i++) {
        push_wait_ready(); push_r1(0x01U); /* CMD55 */
        push_wait_ready(); push_r1(0x01U); /* ACMD41: 0x01 = still in idle */
    }

    SD_Status result = SD_SPI_Init(&sd);
    TEST_ASSERT_EQUAL(SD_TIMEOUT, result);
    TEST_ASSERT_FALSE(sd.initialized);
}

void test_SD_SPI_Init_CSD_ReadFail_ZeroCapacity_StillOk(void) {
    /*
     * Make CMD9 return a non-zero R1 (error) → ReadCSD fails → capacity = 0.
     * The driver still returns SD_OK and sets initialized = true.
     */
    SD_Init(&sd, &g_test_hspi, &g_test_cs, 0, false);
    push_cmd_exchange(0x01U);   /* CMD0   */
    push_cmd_exchange(0x01U);   /* CMD8   */
    push_r7_sdv2();
    push_cmd_exchange(0x01U);   /* CMD55  */
    push_cmd_exchange(0x00U);   /* ACMD41 */
    push_cmd_exchange(0x00U);   /* CMD58  */
    push_ocr_sdhc();
    push_wait_ready();
    push_r1(0x01U);             /* CMD9 R1 = 0x01 (error bit set) → ReadCSD fails */

    TEST_ASSERT_EQUAL(SD_OK, SD_SPI_Init(&sd));
    TEST_ASSERT_TRUE(sd.initialized);
    TEST_ASSERT_EQUAL_UINT32(0U, sd.capacity_blocks);
}

/* -----------------------------------------------------------------------
 * Tick overflow resilience
 * ----------------------------------------------------------------------- */

void test_SD_SPI_Init_TickOverflow_CMD0_TimesOutCorrectly(void) {
    /*
     * Place the tick near UINT32_MAX so that any deadline computed as
     * "start + timeout" would immediately wrap to a small value (old bug).
     * Verify that the init loop still waits for the full timeout period
     * rather than exiting on the first iteration.
     *
     * Strategy: set tick = UINT32_MAX - 5. Push nothing (queue returns 0xFF
     * for WaitReady → passes, then 0xFF x10 for response → SD_TIMEOUT per
     * iteration). Each BackoffDelay adds 1 to the tick. After INIT_TIMEOUT_MS
     * ticks total the loop should exit.
     *
     * We measure that SD_SPI_Init consumes multiple TransmitReceive calls
     * rather than zero or one, proving the loop ran for more than one iteration.
     */
    SD_Init(&sd, &g_test_hspi, &g_test_cs, 0, false);
    mock_hal_set_tick(0xFFFFFF00U); /* UINT32_MAX - 255 */

    SD_SPI_Init(&sd); /* result may be SD_ERROR or SD_TIMEOUT, not checked */

    /*
     * With old overflow code: deadline = 0xFFFFFF00 + 1000 = 0x2E7 (wraps).
     * First check: tick = 0xFFFFFF01 > 0x2E7 → exits after 1 iteration → ~12 calls.
     * With fixed code: loop runs for ~1000 iterations → many hundreds of calls.
     * Assert > 50 to distinguish the two cases unambiguously.
     */
    TEST_ASSERT_GREATER_THAN(50, mock_hal_transmitrec_calls);
}

/* -----------------------------------------------------------------------
 * Accessor functions before initialization
 * ----------------------------------------------------------------------- */

void test_SD_IsSDHC_BeforeInit_ReturnsFalse(void) {
    SD_Init(&sd, &g_test_hspi, &g_test_cs, 0, false);
    TEST_ASSERT_FALSE(SD_IsSDHC(&sd));
}

void test_SD_IsInitialized_BeforeInit_ReturnsFalse(void) {
    SD_Init(&sd, &g_test_hspi, &g_test_cs, 0, false);
    TEST_ASSERT_FALSE(SD_IsInitialized(&sd));
}

void test_SD_GetBlockCount_BeforeInit_ReturnsZero(void) {
    SD_Init(&sd, &g_test_hspi, &g_test_cs, 0, false);
    TEST_ASSERT_EQUAL_UINT32(0U, SD_GetBlockCount(&sd));
}

void test_SD_GetBlockCount_AfterInit_ReturnsCapacity(void) {
    do_sdhc_init(&sd, 8192U);
    TEST_ASSERT_EQUAL_UINT32(8192U, SD_GetBlockCount(&sd));
}

/* -----------------------------------------------------------------------
 * Main
 * ----------------------------------------------------------------------- */

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_SD_Init_NullHandle_ReturnsParam);
    RUN_TEST(test_SD_Init_NullHspi_ReturnsParam);
    RUN_TEST(test_SD_Init_NullCsPort_ReturnsParam);
    RUN_TEST(test_SD_Init_Success_SetsFields);
    RUN_TEST(test_SD_Init_DmaFlag_Stored);

    RUN_TEST(test_SD_DeInit_NullHandle_NoFault);
    RUN_TEST(test_SD_DeInit_ClearsInitialized);

    RUN_TEST(test_SD_SetCardDetect_NullHandle_ReturnsParam);
    RUN_TEST(test_SD_SetCardDetect_NullPort_ReturnsParam);
    RUN_TEST(test_SD_SetCardDetect_Success);

    RUN_TEST(test_SD_IsCardPresent_NullHandle_ReturnsFalse);
    RUN_TEST(test_SD_IsCardPresent_NoCd_AlwaysTrue);
    RUN_TEST(test_SD_IsCardPresent_ActiveLow_PinLow_Present);
    RUN_TEST(test_SD_IsCardPresent_ActiveLow_PinHigh_Absent);
    RUN_TEST(test_SD_IsCardPresent_ActiveHigh_PinHigh_Present);

    RUN_TEST(test_SD_SPI_Init_NullHandle_ReturnsParam);
    RUN_TEST(test_SD_SPI_Init_NoMedia_ReturnsNoMedia);
    RUN_TEST(test_SD_SPI_Init_IncrementsInitAttempts);
    RUN_TEST(test_SD_SPI_Init_SDHC_HappyPath);
    RUN_TEST(test_SD_SPI_Init_SDHC_CapacityParsed);
    RUN_TEST(test_SD_SPI_Init_SDSC_HappyPath);
    RUN_TEST(test_SD_SPI_Init_CMD0_NoValidResponse_ReturnsError);
    RUN_TEST(test_SD_SPI_Init_ACMD41_Timeout_ReturnsTimeout);
    RUN_TEST(test_SD_SPI_Init_CSD_ReadFail_ZeroCapacity_StillOk);

    RUN_TEST(test_SD_SPI_Init_TickOverflow_CMD0_TimesOutCorrectly);

    RUN_TEST(test_SD_IsSDHC_BeforeInit_ReturnsFalse);
    RUN_TEST(test_SD_IsInitialized_BeforeInit_ReturnsFalse);
    RUN_TEST(test_SD_GetBlockCount_BeforeInit_ReturnsZero);
    RUN_TEST(test_SD_GetBlockCount_AfterInit_ReturnsCapacity);

    return UNITY_END();
}
