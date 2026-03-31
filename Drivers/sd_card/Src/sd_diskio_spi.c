/*
 * sd_diskio_spi.c
 *
 * FatFs diskio implementation for the SPI SD driver.
 */
#include "diskio.h"
#include "sd_diskio_spi.h"
#include "sd_spi.h"
#include "ff_gen_drv.h"

#include <string.h>

/* Global SD handle */
SD_Handle_t g_sd_handle;

SD_Status SD_DiskIoInit(SPI_HandleTypeDef *hspi, GPIO_TypeDef *cs_port, uint16_t cs_pin, bool use_dma) {
    return SD_Init(&g_sd_handle, hspi, cs_port, cs_pin, use_dma);
}

DSTATUS SD_disk_status(BYTE drv) {
    if (drv != 0) {
        return STA_NOINIT;
    }

    if (!SD_IsCardPresent(&g_sd_handle)) {
        return STA_NODISK | STA_NOINIT;
    }
    return SD_IsInitialized(&g_sd_handle) ? 0 : STA_NOINIT;
}

DSTATUS SD_disk_initialize(BYTE drv) {
    if (drv != 0) {
        return STA_NOINIT;
    }

    if (!SD_IsCardPresent(&g_sd_handle)) {
        return STA_NODISK | STA_NOINIT;
    }

    if (SD_SPI_Init(&g_sd_handle) == SD_OK) {
        return 0;
    }
    return STA_NOINIT;
}

DRESULT SD_disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count) {
    if (pdrv != 0 || count == 0 || buff == NULL) {
        return RES_PARERR;
    }

    if (!SD_IsInitialized(&g_sd_handle) || !SD_IsCardPresent(&g_sd_handle)) {
        return RES_NOTRDY;
    }

    SD_Status status = SD_ReadBlocks(&g_sd_handle, buff, sector, count);
    if (status == SD_OK) {
        return RES_OK;
    }
    if (status == SD_NO_MEDIA || status == SD_BUSY) {
        return RES_NOTRDY;
    }
    return RES_ERROR;
}

DRESULT SD_disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count) {
    if (pdrv != 0 || count == 0 || buff == NULL) {
        return RES_PARERR;
    }

    if (!SD_IsInitialized(&g_sd_handle) || !SD_IsCardPresent(&g_sd_handle)) {
        return RES_NOTRDY;
    }

    SD_Status status = SD_WriteBlocks(&g_sd_handle, (const uint8_t *)buff, sector, count);
    if (status == SD_OK) {
        return RES_OK;
    }
    if (status == SD_NO_MEDIA || status == SD_BUSY) {
        return RES_NOTRDY;
    }
    return RES_ERROR;
}

DRESULT SD_disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    if (pdrv != 0) {
        return RES_PARERR;
    }

    switch (cmd) {
    case CTRL_SYNC:
        return (SD_Sync(&g_sd_handle) == SD_OK) ? RES_OK : RES_ERROR;
    case GET_SECTOR_SIZE:
        if (buff == NULL) return RES_PARERR;
        *(WORD *)buff = SD_BLOCK_SIZE;
        return RES_OK;
    case GET_SECTOR_COUNT:
        if (buff == NULL) return RES_PARERR;
        *(DWORD *)buff = SD_GetBlockCount(&g_sd_handle);
        return (*(DWORD *)buff > 0) ? RES_OK : RES_ERROR;
    case GET_BLOCK_SIZE:
        if (buff == NULL) return RES_PARERR;
        *(DWORD *)buff = 1;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}

const Diskio_drvTypeDef SD_Driver = {
    SD_disk_initialize,
    SD_disk_status,
    SD_disk_read,
#if _USE_WRITE
    SD_disk_write,
#endif
#if _USE_IOCTL
    SD_disk_ioctl,
#endif
};
