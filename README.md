# FatFS SD Card SPI Driver

A production-ready SPI SD card driver for STM32 microcontrollers with FatFS integration and FreeRTOS support.

## Features

- Supports SDSC and SDHC cards
- Thread-safe with FreeRTOS semaphores
- Deterministic timeouts
- Explicit error reporting
- FatFS diskio layer integration
- High-level helper functions for file operations
- Benchmarking utilities

## Requirements

- STM32 microcontroller with SPI peripheral
- FreeRTOS (optional, but recommended for thread-safety)
- FatFS library (included in STM32CubeMX projects)
- CMSIS-RTOS or similar RTOS interface

## Installation

1. Copy the `Inc/` and `Src/` directories to your STM32 project.
2. Include the headers in your source files.
3. Configure SPI and GPIO pins for SD card communication.
4. Initialize the SD system before mounting FatFS.

## Usage

### Basic Initialization

```c
#include "sd_functions.h"

// Initialize SD card system
int result = sd_system_init(&hspi1, GPIOB, GPIO_PIN_12, false);
if (result != SD_OK) {
    // Handle error
}

// Mount FatFS
FATFS fs;
f_mount(&fs, sd_path, 1);
```

### File Operations

```c
#include "sd_functions.h"

// Write to file
sd_write_file("test.txt", "Hello, World!", 13);

// Read from file
char buffer[100];
sd_read_file("test.txt", buffer, sizeof(buffer));
```

## API Reference

### Core Functions

- `sd_system_init()`: Initialize the SD card system
- `sd_mount()`: Mount the SD card
- `sd_unmount()`: Unmount the SD card

### File Operations

- `sd_write_file()`: Write data to a file
- `sd_read_file()`: Read data from a file
- `sd_file_exists()`: Check if a file exists
- `sd_delete_file()`: Delete a file

### Benchmarking

- `sd_benchmark_run()`: Run performance benchmarks

## Configuration

The driver can be configured through defines in `sd_spi.h`:

- `SD_SPI_TIMEOUT`: SPI timeout in milliseconds
- `SD_INIT_TIMEOUT`: Initialization timeout
- `SD_BLOCK_SIZE`: SD card block size

## Thread Safety

When `USE_FREERTOS` is defined, the driver uses semaphores to ensure thread-safe access to the SD card.

## Error Handling

All functions return `SD_Status` enum values:
- `SD_OK`: Success
- `SD_ERROR`: General error
- `SD_TIMEOUT`: Operation timed out
- `SD_BUSY`: Card is busy
- `SD_NO_MEDIA`: No card detected
- `SD_CRC_ERROR`: CRC error
- `SD_WRITE_ERROR`: Write error
- `SD_UNSUPPORTED`: Unsupported card type

## Contributing

Contributions are welcome! Please follow these guidelines:

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests if applicable
5. Submit a pull request

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Versioning

This project uses [Semantic Versioning](https://semver.org/). For the versions available, see the [tags on this repository](https://github.com/kpf5297/fatfs-sdcard-spi/tags).