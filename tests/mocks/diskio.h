/*
 * tests/mocks/diskio.h
 *
 * Minimal FatFS diskio interface types needed to compile sd_diskio_spi.c
 * on a host system without the full FatFS tree.
 */

#ifndef __MOCK_DISKIO_H__
#define __MOCK_DISKIO_H__

#include <stdint.h>

typedef uint8_t  BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef unsigned int UINT;

typedef BYTE DSTATUS;

typedef enum {
    RES_OK     = 0,
    RES_ERROR  = 1,
    RES_WRPRT  = 2,
    RES_NOTRDY = 3,
    RES_PARERR = 4
} DRESULT;

/* Status bits */
#define STA_NOINIT   0x01U
#define STA_NODISK   0x02U
#define STA_PROTECT  0x04U

/* ioctl commands */
#define CTRL_SYNC        0
#define GET_SECTOR_SIZE  2
#define GET_SECTOR_COUNT 1
#define GET_BLOCK_SIZE   3
#define CTRL_TRIM        4

/* Feature enable flags expected by sd_diskio_spi.c */
#define _USE_WRITE 1
#define _USE_IOCTL 1

#endif /* __MOCK_DISKIO_H__ */
