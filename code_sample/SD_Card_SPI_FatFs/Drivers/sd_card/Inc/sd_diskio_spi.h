/*
 * sd_diskio_spi.h
 *
 * FatFs diskio glue for the SPI SD card driver.
 */

#ifndef __SD_DISKIO_SPI_H__
#define __SD_DISKIO_SPI_H__

#include "diskio.h"
#include "ff_gen_drv.h"
#include "sd_spi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Global SD handle for FatFs interface (single-card configuration). */
extern SD_Handle_t g_sd_handle;

/* Initialize the global SD handle (call before mounting FatFs). */
SD_Status SD_DiskIoInit(SPI_HandleTypeDef *hspi, GPIO_TypeDef *cs_port, uint16_t cs_pin, bool use_dma);

extern const Diskio_drvTypeDef SD_Driver;
DSTATUS SD_disk_status(BYTE drv);
DSTATUS SD_disk_initialize(BYTE drv);
DRESULT SD_disk_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count);
DRESULT SD_disk_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count);
DRESULT SD_disk_ioctl(BYTE pdrv, BYTE cmd, void *buff);

#ifdef __cplusplus
}
#endif

#endif /* __SD_DISKIO_SPI_H__ */
