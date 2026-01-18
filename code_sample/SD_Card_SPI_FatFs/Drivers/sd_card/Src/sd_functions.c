/*
 * sd_functions.c
 *
 * Optional FatFs helper utilities (blocking, task context only).
 */


#include "fatfs.h"
#include "sd_diskio_spi.h"
#include "sd_spi.h"
#include "sd_functions.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ff.h"
#include "ffconf.h"

char sd_path[4] = "0:/";
FATFS fs;

#ifndef SD_FUNCTIONS_LOG_ENABLED
#define SD_FUNCTIONS_LOG_ENABLED 1
#endif

#if SD_FUNCTIONS_LOG_ENABLED
#define SD_APP_LOG(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#define SD_APP_LOG(...) do { } while (0)
#endif

int sd_system_init(SPI_HandleTypeDef *hspi, GPIO_TypeDef *cs_port, uint16_t cs_pin, bool use_dma) {
    return (SD_DiskIoInit(hspi, cs_port, cs_pin, use_dma) == SD_OK) ? 0 : -1;
}

//int sd_format(void) {
//	// Pre-mount required for legacy FatFS
//	f_mount(&fs, sd_path, 0);
//
//	FRESULT res;
//	res = f_mkfs(sd_path, 1, 0);
//	if (res != FR_OK) {
//		printf("Format failed: f_mkfs returned %d\r\n", res);
//	}
//		return res;
//}

int sd_get_space_kb(void) {
	FATFS *pfs;
	DWORD fre_clust, tot_sect, fre_sect, total_kb, free_kb;
	FRESULT res = f_getfree(sd_path, &fre_clust, &pfs);
	if (res != FR_OK) return res;

	tot_sect = (pfs->n_fatent - 2) * pfs->csize;
	fre_sect = fre_clust * pfs->csize;
	total_kb = tot_sect / 2;
	free_kb = fre_sect / 2;
	SD_APP_LOG("Total: %lu KB, Free: %lu KB\r\n", total_kb, free_kb);
	return FR_OK;
}

int sd_mount(void) {
	FRESULT res;

	printf("\r\n========================================\r\n");
	printf("SD card mount\r\n");
	printf("========================================\r\n");

	printf("Checking SD card presence...\r\n");
	if (!SD_IsCardPresent(&g_sd_handle)) {
		printf("ERROR: SD card not present!\r\n");
		return FR_NOT_READY;
	}
	printf("OK: SD card detected\r\n");

	printf("Initializing SD card disk interface...\r\n");
	DSTATUS stat = disk_initialize(0);
	printf("disk_initialize returned: 0x%02X\r\n", stat);
	if (stat != 0) {
		printf("ERROR: disk_initialize failed: 0x%02X\r\n", stat);
		printf("  STA_NOINIT=0x01, STA_NODISK=0x02, STA_PROTECT=0x04\r\n");
		return FR_NOT_READY;
	}
	printf("OK: Disk interface initialized\r\n");

	printf("Mounting filesystem at %s...\r\n", sd_path);
	res = f_mount(&fs, sd_path, 1);
	printf("f_mount returned: %d (0=OK, 1=DISK_ERR, 2=INT_ERR, 3=NOT_READY, 4=NO_FILE, 13=INVALID_NAME)\r\n", res);
	if (res == FR_OK)
	{
		printf("OK: Filesystem mounted successfully\r\n");
		printf("Card Type: %s\r\n", SD_IsSDHC(&g_sd_handle) ? "SDHC/SDXC" : "SDSC");

		// Capacity and free space reporting
		sd_get_space_kb();
		printf("========================================\r\n\r\n");
		return FR_OK;
	}

	/* Many users were having issues with f_mkfs, so I have disabled it
	 * You need to format SD card in FAT FileSysytem before inserting it
	 */
//	 Handle no filesystem by creating one
//	if (res == FR_NO_FILESYSTEM)
//	{
//		printf("No filesystem found on SD card. Attempting format...\r\nThis will create 32MB Partition (Most Probably)\r\n");
//		printf("If you need the full sized SD card, use the computer to format into FAT32\r\n");
//		sd_format();
//
//		printf("Retrying mount after format...\r\n");
//		res = f_mount(&fs, sd_path, 1);
//		if (res == FR_OK) {
//			printf("SD card formatted and mounted successfully.\r\n");
//			printf("Card Type: %s\r\n", sd_is_sdhc() ? "SDHC/SDXC" : "SDSC");
//
//			// Report capacity after format
//			sd_get_space_kb();
//		}
//		else {
//			printf("Mount failed even after format: %d\r\n", res);
//		}
//		return res;
//	}

	// Any other mount error
	printf("ERROR: Mount failed with code: %d\r\n", res);
	printf("========================================\r\n\r\n");
	return res;
}


int sd_unmount(void) {
	FRESULT res = f_mount(NULL, sd_path, 1);
	SD_APP_LOG("SD unmount: %s\r\n", (res == FR_OK) ? "OK" : "Failed");
	return res;
}

int sd_write_file(const char *filename, const char *text) {
	FIL file;
	UINT bw;
	FRESULT res = f_open(&file, filename, FA_CREATE_ALWAYS | FA_WRITE);
	if (res != FR_OK) {
		SD_APP_LOG("File open failed: %d\r\n", res);
		return res;
	}

	res = f_write(&file, text, strlen(text), &bw);
	f_close(&file);
	if (res == FR_OK && bw == strlen(text)) {
		SD_APP_LOG("Wrote %u bytes to %s\r\n", bw, filename);
	} else {
		SD_APP_LOG("Write failed: %d (expected %u bytes, wrote %u)\r\n", res, (unsigned int)strlen(text), bw);
	}
	return (res == FR_OK && bw == strlen(text)) ? FR_OK : FR_DISK_ERR;
}

int sd_append_file(const char *filename, const char *text) {
	FIL file;
	UINT bw;
	FRESULT res = f_open(&file, filename, FA_OPEN_ALWAYS | FA_WRITE);
	if (res != FR_OK) {
		SD_APP_LOG("File open failed: %d\r\n", res);
		return res;
	}

	res = f_lseek(&file, f_size(&file));
	if (res != FR_OK) {
		SD_APP_LOG("Seek failed: %d\r\n", res);
		f_close(&file);
		return res;
	}

	res = f_write(&file, text, strlen(text), &bw);
	f_close(&file);
	if (res == FR_OK && bw == strlen(text)) {
		SD_APP_LOG("Appended %u bytes to %s\r\n", bw, filename);
	} else {
		SD_APP_LOG("Append failed: %d\r\n", res);
	}
	return (res == FR_OK && bw == strlen(text)) ? FR_OK : FR_DISK_ERR;
}

int sd_read_file(const char *filename, char *buffer, UINT bufsize, UINT *bytes_read) {
	FIL file;
	if (buffer == NULL || bytes_read == NULL || bufsize == 0) {
		return FR_INVALID_PARAMETER;
	}
	*bytes_read = 0;

	FRESULT res = f_open(&file, filename, FA_READ);
	if (res != FR_OK) {
		SD_APP_LOG("File open failed: %d\r\n", res);
		return res;
	}

	res = f_read(&file, buffer, bufsize - 1, bytes_read);
	if (res != FR_OK) {
		SD_APP_LOG("Read failed: %d\r\n", res);
		f_close(&file);
		return res;
	}

	buffer[*bytes_read] = '\0';

	res = f_close(&file);
	if (res != FR_OK) {
		SD_APP_LOG("File close failed: %d\r\n", res);
		return res;
	}

	SD_APP_LOG("Read %u bytes from %s\r\n", *bytes_read, filename);
	return FR_OK;
}

int sd_read_csv(const char *filename, CsvRecord *records, int max_records, int *record_count) {
	FIL file;
	char line[128];
	if (records == NULL || record_count == NULL || max_records <= 0) {
		return FR_INVALID_PARAMETER;
	}
	*record_count = 0;

	FRESULT res = f_open(&file, filename, FA_READ);
	if (res != FR_OK) {
		SD_APP_LOG("Failed to open CSV: %s (%d)\r\n", filename, res);
		return res;
	}

	SD_APP_LOG("Reading CSV: %s\r\n", filename);
	while (f_gets(line, sizeof(line), &file) && *record_count < max_records) {
		char *token = strtok(line, ",");
		if (!token) continue;
		strncpy(records[*record_count].field1, token, sizeof(records[*record_count].field1) - 1);
		records[*record_count].field1[sizeof(records[*record_count].field1) - 1] = '\0';

		token = strtok(NULL, ",");
		if (!token) continue;
		strncpy(records[*record_count].field2, token, sizeof(records[*record_count].field2) - 1);
		records[*record_count].field2[sizeof(records[*record_count].field2) - 1] = '\0';

		token = strtok(NULL, ",");
		if (token)
			records[*record_count].value = atoi(token);
		else
			records[*record_count].value = 0;

		(*record_count)++;
	}

	f_close(&file);

	// Print parsed data
	for (int i = 0; i < *record_count; i++) {
		SD_APP_LOG("[%d] %s | %s | %d\r\n", i,
				records[i].field1,
				records[i].field2,
				records[i].value);
	}

	return FR_OK;
}

int sd_delete_file(const char *filename) {
	FRESULT res = f_unlink(filename);
	SD_APP_LOG("Delete %s: %s\r\n", filename, (res == FR_OK ? "OK" : "Failed"));
	return res;
}

int sd_rename_file(const char *oldname, const char *newname) {
	FRESULT res = f_rename(oldname, newname);
	SD_APP_LOG("Rename %s to %s: %s\r\n", oldname, newname, (res == FR_OK ? "OK" : "Failed"));
	return res;
}

FRESULT sd_create_directory(const char *path) {
	FRESULT res = f_mkdir(path);
	SD_APP_LOG("Create directory %s: %s\r\n", path, (res == FR_OK ? "OK" : "Failed"));
	return res;
}

void sd_list_directory_recursive(const char *path, int depth) {
	DIR dir;
	FILINFO fno;
	FRESULT res = f_opendir(&dir, path);
	if (res != FR_OK) {
		printf("%*s[ERR] Cannot open: %s\r\n", depth * 2, "", path);
		return;
	}

	while (1) {
		res = f_readdir(&dir, &fno);
		if (res != FR_OK || fno.fname[0] == 0) break;

		const char *name = (fno.fname[0] != 0) ? fno.fname : fno.altname;

		if (fno.fattrib & AM_DIR) {
			if (strcmp(name, ".") && strcmp(name, "..")) {
				SD_APP_LOG("%*s[D] %s\r\n", depth * 2, "", name);
				char newpath[128];
				snprintf(newpath, sizeof(newpath), "%s/%s", path, name);
				sd_list_directory_recursive(newpath, depth + 1);
			}
		} else {
			SD_APP_LOG("%*s[F] %s (%lu bytes)\r\n", depth * 2, "", name, (unsigned long)fno.fsize);
		}
	}
	f_closedir(&dir);
}

void sd_list_files(void) {
	SD_APP_LOG("Files on SD card:\r\n");
	sd_list_directory_recursive(sd_path, 0);
	SD_APP_LOG("\r\n\r\n");
}
