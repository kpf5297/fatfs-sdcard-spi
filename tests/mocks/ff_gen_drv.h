/*
 * tests/mocks/ff_gen_drv.h
 *
 * Minimal FatFS generic driver registration type needed by sd_diskio_spi.c.
 */

#ifndef __MOCK_FF_GEN_DRV_H__
#define __MOCK_FF_GEN_DRV_H__

#include "diskio.h"

typedef struct {
    DSTATUS (*disk_initialize)(BYTE pdrv);
    DSTATUS (*disk_status)(BYTE pdrv);
    DRESULT (*disk_read)(BYTE pdrv, BYTE *buff, DWORD sector, UINT count);
#if _USE_WRITE
    DRESULT (*disk_write)(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count);
#endif
#if _USE_IOCTL
    DRESULT (*disk_ioctl)(BYTE pdrv, BYTE cmd, void *buff);
#endif
} Diskio_drvTypeDef;

#endif /* __MOCK_FF_GEN_DRV_H__ */
