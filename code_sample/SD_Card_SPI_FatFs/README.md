# SD Card SPI + FatFs + FreeRTOS v1 for STM32F446

A deterministic, thread-safe SD card driver refactored to use structs instead of global externs, with full FreeRTOS v1 support and DMA capabilities.

## Features

- ✅ **Struct-Based API** - Single SD handle with deterministic, bounded operations
- ✅ **FreeRTOS Thread Safety** - Built-in mutex protection for all SD operations
- ✅ **DMA Support** - High-performance data transfers
- ✅ **FatFs Integration** - Full file system support
- ✅ **CSV Operations** - Built-in CSV file reading/writing
- ✅ **Performance Benchmarking** - Speed testing utilities
- ✅ **Auto-Initialization** - Single initialization function handles all setup

## Build System

This project uses **STM32CubeCLT** for building and programming.

### Available VS Code Tasks

Press `Ctrl+Shift+P` and select "Run Task" to execute any of these:

#### Build Tasks
- **Build** (Default: `Ctrl+Shift+B`)
  - Incremental build of the project
  - Fastest for iterative development

- **Clean Build**
  - Removes build artifacts and rebuilds from scratch
  - Use when dependencies change or after git pull

- **Configure Project**
  - Runs CMake configuration
  - Use if CMakeLists.txt is modified

#### Programming Tasks
- **Program Device (STM32CubeProgrammer)**
  - Flashes firmware to STM32F446 via ST-LINK
  - Includes verification and device reset
  - Requirements:
    - ST-LINK debugger connected to SWD pins
    - Device powered and properly connected

- **Build and Program**
  - Builds project then immediately programs device
  - One-click deployment workflow
  - Combines build and programming into single task

- **Open Programmer GUI**
  - Launches STM32CubeProgrammer GUI
  - For manual programming or debugging

## Hardware Setup

### Required Connections (SWD Programming)
```
ST-LINK         STM32F446
--------        ---------
SWDIO    <-->   PA13
SWCLK    <-->   PA14
GND      <-->   GND
3V3      <-->   3V3 (Optional, auto-sensed)
```

### SD Card SPI Connections
```
SD Card          STM32F446 (SPI3)
-------          ----------------
CS       <-->    PD1   (SPI1_CS)
MOSI     <-->    PC12  (SPI3_MOSI)
MISO     <-->    PC11  (SPI3_MISO)
SCK      <-->    PC10  (SPI3_SCK)
GND      <-->    GND
VCC      <-->    3V3 (with decoupling cap)
```

## Project Structure

```
SD_Card_SPI_FatFs/
├── Core/
│   ├── Inc/          # STM32 HAL headers & main.h
│   └── Src/          # STM32 HAL implementations
├── Drivers/
│   ├── sd_card/      # SD Driver (REFACTORED)
│   │   ├── Inc/
│   │   │   ├── sd_spi.h          # Core SPI driver
│   │   │   ├── sd_functions.h    # High-level file ops
│   │   │   ├── sd_diskio_spi.h   # FatFs integration
│   │   │   └── sd_benchmark.h    # Performance tests
│   │   └── Src/
│   ├── STM32F4xx_HAL_Driver/     # ST Microelectronics HAL
│   └── CMSIS/                    # ARM CMSIS headers
├── FATFS/            # FatFs file system
├── Middlewares/
│   └── FreeRTOS/     # FreeRTOS kernel
├── CMakeLists.txt    # Main build configuration
├── cmake/            # CMake modules
└── .vscode/
    ├── tasks.json    # Build/program tasks
    ├── launch.json   # Debug configuration
    └── settings.json # Editor settings
```

## API Usage

### Initialization
```c
#include "sd_functions.h"
#include "spi.h"

// In main or FreeRTOS task:
if (sd_system_init(&hspi3, SPI1_CS_GPIO_Port, SPI1_CS_Pin, true) != 0) {
    printf("SD system initialization failed!\r\n");
    return;
}

if (sd_mount() == FR_OK) {
    printf("SD card mounted successfully\r\n");
} else {
    printf("SD mount failed\r\n");
}
```

### File Operations
```c
// Write file
sd_write_file("test.txt", "Hello World!\r\n");

// Read file
char buffer[256];
UINT bytes_read;
sd_read_file("test.txt", buffer, sizeof(buffer), &bytes_read);
printf("Read %u bytes: %s\r\n", bytes_read, buffer);

// Append to file
sd_append_file("test.txt", "Appended line\r\n");

// Delete file
sd_delete_file("test.txt");

// Create directory
sd_create_directory("data");

// List files
sd_list_files();
```

### CSV Operations
```c
// Write CSV
CsvRecord records[3] = {
    {"Name1", "Value1", 100},
    {"Name2", "Value2", 200},
    {"Name3", "Value3", 300}
};

for (int i = 0; i < 3; i++) {
    char line[64];
    snprintf(line, sizeof(line), "%s,%s,%d\r\n",
             records[i].field1, records[i].field2, records[i].value);
    if (i == 0) {
        sd_write_file("data.csv", line);
    } else {
        sd_append_file("data.csv", line);
    }
}

// Read CSV
CsvRecord read_records[5];
int record_count;
sd_read_csv("data.csv", read_records, 5, &record_count);
for (int i = 0; i < record_count; i++) {
    printf("[%d] %s | %s | %d\r\n", i,
           read_records[i].field1,
           read_records[i].field2,
           read_records[i].value);
}
```

### Performance Benchmarking
```c
// Run read/write speed tests
sd_benchmark();
// Output shows KB/s for read and write operations
```

### Thread-Safe Access (FreeRTOS)
All SD operations are automatically mutex-protected. Safe to call from multiple tasks:
```c
void Task1(void *arg) {
    sd_write_file("file1.txt", "Task 1 data\r\n");
}

void Task2(void *arg) {
    sd_append_file("file1.txt", "Task 2 data\r\n");
}
// No race conditions - mutex handles synchronization
```

## Cleanup and Shutdown
```c
// Unmount filesystem
sd_unmount();

// Optional: Deinitialize SD handle
SD_DeInit(&g_sd_handle);
```

## Troubleshooting

### Build Fails
```bash
# Clean and rebuild
rm -rf build/Debug
cube-cmake -B build/Debug -S .
cube-cmake --build build/Debug
```

### Programming Fails
- **No ST-LINK Found**: Check USB connection
- **No STM32 Target Found**: 
  - Verify SWD connections (PA13, PA14)
  - Check device power
  - Try power cycling the board

### SD Card Not Detected
- Verify CS pin connection (PD1)
- Check SPI3 connections (PC10, PC11, PC12)
- Ensure SD card is properly formatted (FAT32)
- Add decoupling capacitor near SD card VCC

### Performance Issues
- Enable DMA in SD driver (default: enabled)
- Increase SPI clock speed in `spi.c` (currently 256 prescaler)
- Use `sd_benchmark()` to measure read/write speeds

## Memory Usage
```
RAM:   ~20 KB / 128 KB (15%)
FLASH: ~59 KB / 512 KB (11%)
```

Plenty of room for additional features!

## Configuration

### Enable/Disable FreeRTOS Support
Edit `cmake/stm32cubemx/CMakeLists.txt`:
```cmake
set(MX_Defines_Syms 
    USE_HAL_DRIVER
    STM32F446xx
    USE_FREERTOS  # Comment out to disable FreeRTOS
)
```

### Adjust DMA Settings
Edit `Drivers/sd_card/Src/sd_spi.c`:
```c
// Disable DMA for basic operations
bool use_dma = false;  // vs true
```

### Change SPI Speed
Edit `Core/Src/spi.c`:
```c
hspi3.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;  // Adjust divisor
// Options: 2, 4, 8, 16, 32, 64, 128, 256
```

## Advanced Debugging

### Enable Debug Output
Connect UART1 for printf() output:
```
UART1_TX --> PA9
UART1_RX --> PA10
```
Baud rate: 115200, 8N1

### Debug with GDB
```bash
# In VS Code, use launch.json (pre-configured)
# Press F5 to start debugging
```

## References

- [STM32F446 Datasheet](https://www.st.com/en/microcontrollers/stm32f446.html)
- [FatFs Documentation](http://elm-chan.org/fsw/ff/)
- [FreeRTOS Documentation](https://www.freertos.org/documentation)
- [STM32CubeCLT User Guide](https://www.st.com/en/development-tools/stm32cubeclt.html)

## License

See `LICENSE` (if present) for project licensing. The SD driver in this repo is maintained independently of any external tutorial code.

---

**Last Updated**: January 17, 2026  
**Firmware Version**: 1.0.0 (Deterministic + FreeRTOS v1)
