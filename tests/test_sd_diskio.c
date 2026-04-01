/*
 * tests/test_sd_diskio.c
 *
 * Tests for the FatFS diskio glue layer (sd_diskio_spi.c).
 * Exercises SD_disk_status, SD_disk_initialize, SD_disk_read,
 * SD_disk_write, and SD_disk_ioctl.
 */

#include "unity.h"
#include "mock_hal.h"
#include "test_helpers.h"
#include "sd_spi.h"
#include "sd_diskio_spi.h"
#include <string.h>

/*
 * g_sd_handle is the global handle defined in sd_diskio_spi.c.
 * We manipulate it directly for state-injection tests.
 */
extern SD_Handle_t g_sd_handle;

void setUp(void) {
    mock_hal_reset();
    memset(&g_sd_handle, 0, sizeof(g_sd_handle));
}

void tearDown(void) {}

/* -----------------------------------------------------------------------
 * Helpers
 * ----------------------------------------------------------------------- */

/* Bring the global handle to a fully initialized SDHC state. */
static void init_global_sdhc(uint32_t capacity_blocks) {
    SD_Init(&g_sd_handle, &g_test_hspi, &g_test_cs, 0, false);
    push_sdhc_init(capacity_blocks);
    SD_Status s = SD_SPI_Init(&g_sd_handle);
    TEST_ASSERT_EQUAL_MESSAGE(SD_OK, s, "Global handle init failed");
}

/* -----------------------------------------------------------------------
 * SD_disk_status
 * ----------------------------------------------------------------------- */

void test_disk_status_WrongDrive_ReturnsNoinit(void) {
    TEST_ASSERT_EQUAL(STA_NOINIT, SD_disk_status(1));
    TEST_ASSERT_EQUAL(STA_NOINIT, SD_disk_status(255));
}

void test_disk_status_HandleNotInit_ReturnsNoinit(void) {
    /* g_sd_handle.initialized = false (from setUp memset) */
    TEST_ASSERT_BITS(STA_NOINIT, STA_NOINIT, SD_disk_status(0));
}

void test_disk_status_NoCard_ReturnsNodisk(void) {
    SD_Init(&g_sd_handle, &g_test_hspi, &g_test_cs, 0, false);
    SD_SetCardDetect(&g_sd_handle, &g_test_cd, 0, true);
    mock_hal_set_gpio_read(GPIO_PIN_SET); /* absent */
    DSTATUS st = SD_disk_status(0);
    TEST_ASSERT_BITS(STA_NODISK, STA_NODISK, st);
}

void test_disk_status_Initialized_ReturnsZero(void) {
    init_global_sdhc(8192U);
    TEST_ASSERT_EQUAL(0, SD_disk_status(0));
}

/* -----------------------------------------------------------------------
 * SD_disk_initialize
 * ----------------------------------------------------------------------- */

void test_disk_initialize_WrongDrive_ReturnsNoinit(void) {
    TEST_ASSERT_EQUAL(STA_NOINIT, SD_disk_initialize(1));
}

void test_disk_initialize_NoCard_ReturnsNoinit(void) {
    SD_Init(&g_sd_handle, &g_test_hspi, &g_test_cs, 0, false);
    SD_SetCardDetect(&g_sd_handle, &g_test_cd, 0, true);
    mock_hal_set_gpio_read(GPIO_PIN_SET);
    TEST_ASSERT_BITS(STA_NODISK, STA_NODISK, SD_disk_initialize(0));
}

void test_disk_initialize_InitFails_ReturnsNoinit(void) {
    SD_Init(&g_sd_handle, &g_test_hspi, &g_test_cs, 0, false);
    /* Push nothing — CMD0 response polling never gets 0x01 → SD_ERROR */
    TEST_ASSERT_EQUAL(STA_NOINIT, SD_disk_initialize(0));
}

void test_disk_initialize_HappyPath_ReturnsZero(void) {
    SD_Init(&g_sd_handle, &g_test_hspi, &g_test_cs, 0, false);
    push_sdhc_init(8192U);
    TEST_ASSERT_EQUAL(0, SD_disk_initialize(0));
    TEST_ASSERT_TRUE(g_sd_handle.initialized);
}

/* -----------------------------------------------------------------------
 * SD_disk_read
 * ----------------------------------------------------------------------- */

void test_disk_read_WrongDrive_ReturnsParerr(void) {
    uint8_t buf[512];
    TEST_ASSERT_EQUAL(RES_PARERR, SD_disk_read(1, buf, 0, 1));
}

void test_disk_read_NullBuffer_ReturnsParerr(void) {
    TEST_ASSERT_EQUAL(RES_PARERR, SD_disk_read(0, NULL, 0, 1));
}

void test_disk_read_CountZero_ReturnsParerr(void) {
    uint8_t buf[512];
    TEST_ASSERT_EQUAL(RES_PARERR, SD_disk_read(0, buf, 0, 0));
}

void test_disk_read_NotInitialized_ReturnsNotrdy(void) {
    /* g_sd_handle not initialized — SD_ReadBlocks returns SD_ERROR */
    SD_Init(&g_sd_handle, &g_test_hspi, &g_test_cs, 0, false);
    uint8_t buf[512];
    TEST_ASSERT_EQUAL(RES_NOTRDY, SD_disk_read(0, buf, 0, 1));
}

void test_disk_read_HappyPath_ReturnsOk(void) {
    init_global_sdhc(8192U);
    push_single_read(0xBEU);
    uint8_t buf[512];
    TEST_ASSERT_EQUAL(RES_OK, SD_disk_read(0, buf, 0, 1));
    TEST_ASSERT_EQUAL_UINT8(0xBEU, buf[0]);
}

void test_disk_read_NoMedia_MapsToNotrdy(void) {
    init_global_sdhc(8192U);
    /* Remove card */
    SD_SetCardDetect(&g_sd_handle, &g_test_cd, 0, true);
    mock_hal_set_gpio_read(GPIO_PIN_SET);
    uint8_t buf[512];
    TEST_ASSERT_EQUAL(RES_NOTRDY, SD_disk_read(0, buf, 0, 1));
}

void test_disk_read_Error_MapsToError(void) {
    init_global_sdhc(8192U);
    /* CMD17 returns an error R1 (3 retries all fail) */
    for (int i = 0; i < 3; i++) {
        push_wait_ready();
        push_r1(0x04U);
    }
    uint8_t buf[512];
    TEST_ASSERT_EQUAL(RES_ERROR, SD_disk_read(0, buf, 0, 1));
}

/* -----------------------------------------------------------------------
 * SD_disk_write
 * ----------------------------------------------------------------------- */

void test_disk_write_WrongDrive_ReturnsParerr(void) {
    const uint8_t buf[512] = {0};
    TEST_ASSERT_EQUAL(RES_PARERR, SD_disk_write(1, buf, 0, 1));
}

void test_disk_write_NullBuffer_ReturnsParerr(void) {
    TEST_ASSERT_EQUAL(RES_PARERR, SD_disk_write(0, NULL, 0, 1));
}

void test_disk_write_CountZero_ReturnsParerr(void) {
    const uint8_t buf[512] = {0};
    TEST_ASSERT_EQUAL(RES_PARERR, SD_disk_write(0, buf, 0, 0));
}

void test_disk_write_NotInitialized_ReturnsNotrdy(void) {
    SD_Init(&g_sd_handle, &g_test_hspi, &g_test_cs, 0, false);
    const uint8_t buf[512] = {0};
    TEST_ASSERT_EQUAL(RES_NOTRDY, SD_disk_write(0, buf, 0, 1));
}

void test_disk_write_HappyPath_ReturnsOk(void) {
    init_global_sdhc(8192U);
    push_single_write_accepted();
    const uint8_t buf[512] = {0xA5U};
    TEST_ASSERT_EQUAL(RES_OK, SD_disk_write(0, buf, 0, 1));
}

void test_disk_write_CrcError_MapsToError(void) {
    init_global_sdhc(8192U);
    push_single_write_crc_error();
    push_single_write_crc_error();
    push_single_write_crc_error();
    const uint8_t buf[512] = {0};
    TEST_ASSERT_EQUAL(RES_ERROR, SD_disk_write(0, buf, 0, 1));
}

/* -----------------------------------------------------------------------
 * SD_disk_ioctl
 * ----------------------------------------------------------------------- */

void test_disk_ioctl_WrongDrive_ReturnsParerr(void) {
    DWORD val;
    TEST_ASSERT_EQUAL(RES_PARERR, SD_disk_ioctl(1, CTRL_SYNC, &val));
}

void test_disk_ioctl_CTRL_SYNC_CardReady_ReturnsOk(void) {
    init_global_sdhc(8192U);
    push_wait_ready(); /* SD_Sync internally calls WaitReady */
    TEST_ASSERT_EQUAL(RES_OK, SD_disk_ioctl(0, CTRL_SYNC, NULL));
}

void test_disk_ioctl_CTRL_SYNC_NullBuff_StillWorks(void) {
    /* CTRL_SYNC does not use the buffer argument */
    init_global_sdhc(8192U);
    push_wait_ready();
    TEST_ASSERT_EQUAL(RES_OK, SD_disk_ioctl(0, CTRL_SYNC, NULL));
}

void test_disk_ioctl_GET_SECTOR_SIZE_Returns512(void) {
    init_global_sdhc(8192U);
    WORD size = 0;
    TEST_ASSERT_EQUAL(RES_OK, SD_disk_ioctl(0, GET_SECTOR_SIZE, &size));
    TEST_ASSERT_EQUAL_UINT16(512U, size);
}

void test_disk_ioctl_GET_SECTOR_SIZE_NullBuff_ReturnsParerr(void) {
    init_global_sdhc(8192U);
    TEST_ASSERT_EQUAL(RES_PARERR, SD_disk_ioctl(0, GET_SECTOR_SIZE, NULL));
}

void test_disk_ioctl_GET_SECTOR_COUNT_Valid_ReturnsOk(void) {
    init_global_sdhc(8192U);
    DWORD count = 0;
    TEST_ASSERT_EQUAL(RES_OK, SD_disk_ioctl(0, GET_SECTOR_COUNT, &count));
    TEST_ASSERT_EQUAL_UINT32(8192U, count);
}

void test_disk_ioctl_GET_SECTOR_COUNT_ZeroCapacity_ReturnsError(void) {
    init_global_sdhc(0U);
    g_sd_handle.capacity_blocks = 0;
    DWORD count = 0;
    TEST_ASSERT_EQUAL(RES_ERROR, SD_disk_ioctl(0, GET_SECTOR_COUNT, &count));
}

void test_disk_ioctl_GET_SECTOR_COUNT_NullBuff_ReturnsParerr(void) {
    init_global_sdhc(8192U);
    TEST_ASSERT_EQUAL(RES_PARERR, SD_disk_ioctl(0, GET_SECTOR_COUNT, NULL));
}

void test_disk_ioctl_GET_BLOCK_SIZE_Returns1(void) {
    init_global_sdhc(8192U);
    DWORD bs = 0;
    TEST_ASSERT_EQUAL(RES_OK, SD_disk_ioctl(0, GET_BLOCK_SIZE, &bs));
    TEST_ASSERT_EQUAL_UINT32(1U, bs);
}

void test_disk_ioctl_UnknownCommand_ReturnsParerr(void) {
    init_global_sdhc(8192U);
    DWORD dummy;
    TEST_ASSERT_EQUAL(RES_PARERR, SD_disk_ioctl(0, 0xFFU, &dummy));
}

/* -----------------------------------------------------------------------
 * SD_DiskIoInit
 * ----------------------------------------------------------------------- */

void test_DiskIoInit_ValidArgs_ReturnsOk(void) {
    TEST_ASSERT_EQUAL(SD_OK,
        SD_DiskIoInit(&g_test_hspi, &g_test_cs, 3, false));
    TEST_ASSERT_EQUAL_PTR(&g_test_hspi, g_sd_handle.hspi);
    TEST_ASSERT_EQUAL_PTR(&g_test_cs,   g_sd_handle.cs_port);
    TEST_ASSERT_EQUAL_UINT16(3U,         g_sd_handle.cs_pin);
}

/* -----------------------------------------------------------------------
 * Main
 * ----------------------------------------------------------------------- */

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_disk_status_WrongDrive_ReturnsNoinit);
    RUN_TEST(test_disk_status_HandleNotInit_ReturnsNoinit);
    RUN_TEST(test_disk_status_NoCard_ReturnsNodisk);
    RUN_TEST(test_disk_status_Initialized_ReturnsZero);

    RUN_TEST(test_disk_initialize_WrongDrive_ReturnsNoinit);
    RUN_TEST(test_disk_initialize_NoCard_ReturnsNoinit);
    RUN_TEST(test_disk_initialize_InitFails_ReturnsNoinit);
    RUN_TEST(test_disk_initialize_HappyPath_ReturnsZero);

    RUN_TEST(test_disk_read_WrongDrive_ReturnsParerr);
    RUN_TEST(test_disk_read_NullBuffer_ReturnsParerr);
    RUN_TEST(test_disk_read_CountZero_ReturnsParerr);
    RUN_TEST(test_disk_read_NotInitialized_ReturnsNotrdy);
    RUN_TEST(test_disk_read_HappyPath_ReturnsOk);
    RUN_TEST(test_disk_read_NoMedia_MapsToNotrdy);
    RUN_TEST(test_disk_read_Error_MapsToError);

    RUN_TEST(test_disk_write_WrongDrive_ReturnsParerr);
    RUN_TEST(test_disk_write_NullBuffer_ReturnsParerr);
    RUN_TEST(test_disk_write_CountZero_ReturnsParerr);
    RUN_TEST(test_disk_write_NotInitialized_ReturnsNotrdy);
    RUN_TEST(test_disk_write_HappyPath_ReturnsOk);
    RUN_TEST(test_disk_write_CrcError_MapsToError);

    RUN_TEST(test_disk_ioctl_WrongDrive_ReturnsParerr);
    RUN_TEST(test_disk_ioctl_CTRL_SYNC_CardReady_ReturnsOk);
    RUN_TEST(test_disk_ioctl_CTRL_SYNC_NullBuff_StillWorks);
    RUN_TEST(test_disk_ioctl_GET_SECTOR_SIZE_Returns512);
    RUN_TEST(test_disk_ioctl_GET_SECTOR_SIZE_NullBuff_ReturnsParerr);
    RUN_TEST(test_disk_ioctl_GET_SECTOR_COUNT_Valid_ReturnsOk);
    RUN_TEST(test_disk_ioctl_GET_SECTOR_COUNT_ZeroCapacity_ReturnsError);
    RUN_TEST(test_disk_ioctl_GET_SECTOR_COUNT_NullBuff_ReturnsParerr);
    RUN_TEST(test_disk_ioctl_GET_BLOCK_SIZE_Returns1);
    RUN_TEST(test_disk_ioctl_UnknownCommand_ReturnsParerr);

    RUN_TEST(test_DiskIoInit_ValidArgs_ReturnsOk);

    return UNITY_END();
}
