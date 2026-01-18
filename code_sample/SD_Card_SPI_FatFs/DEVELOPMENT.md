# Development Guide - SD Card SPI + FatFs + FreeRTOS

## Quick Start

### 1. Build the Project
```bash
# Option A: VS Code task (Recommended)
# Press Ctrl+Shift+B or Ctrl+Shift+P → Run Task → "Build"

# Option B: Manual build
cd ~/Code/SD_Card_SPI_FatFs
cube-cmake --build build/Debug
```

### 2. Program the Device
```bash
# Option A: VS Code task (Recommended)
# Ctrl+Shift+P → Run Task → "Build and Program"

# Option B: Program only
# Ctrl+Shift+P → Run Task → "Program Device (STM32CubeProgrammer)"

# Option C: Manual programming
STM32_Programmer_CLI -c port=SWD -d build/Debug/SD_Card_SPI_FatFs.elf -v -rst
```

### 3. Monitor Serial Output
If UART1 is connected:
```bash
# macOS
screen /dev/tty.usbserial-* 115200

# Linux
screen /dev/ttyUSB0 115200

# To exit: Ctrl+A then Ctrl+D
```

## FreeRTOS Task Structure

The main SD card demo runs in `StartDefaultTask()` in [Core/Src/freertos.c](Core/Src/freertos.c):

```c
void StartDefaultTask(void const * argument) {
    printf("Starting SD Card Demo...\r\n");
    
    // 1. Initialize SD system
    if (sd_system_init(&hspi3, SPI1_CS_GPIO_Port, SPI1_CS_Pin, true) != 0) {
        printf("SD system initialization failed!\r\n");
        while(1) osDelay(1000);
    }
    
    // 2. Mount filesystem
    if (sd_mount() == FR_OK) {
        // 3. File operations
        sd_write_file("test.txt", "Hello World!");
        
        // 4. CSV operations
        // (Creates records, writes to CSV, reads back)
        
        // 5. Benchmarking
        sd_benchmark();
        
        // 6. Cleanup
        sd_unmount();
    }
    
    // 7. Keep task alive
    for(;;) osDelay(1000);
}
```

## Code Architecture

### Handle-Based Driver (Deterministic)

```
Application Code
    ↓
sd_functions.c (High-level API)
    ↓
sd_diskio_spi.c (FatFs bridge)
    ↓
sd_spi.c (Struct-based, mutex-protected)
    ↓
STM32 HAL (SPI3, GPIO)
    ↓
Hardware (SD Card)
```

**Key Principle**: All operations go through `SD_Handle_t` struct. No global state beyond the single global handle in diskio layer.

### Thread Safety with Mutex

Every SD operation acquires a mutex:
```c
static StaticSemaphore_t sd_mutex_buffer;

void SD_Init(SD_Handle_t *handle) {
    handle->mutex = xSemaphoreCreateMutexStatic(&sd_mutex_buffer);
}

uint8_t SD_ReadBlocks(SD_Handle_t *handle, uint8_t *data, ...) {
    xSemaphoreTake(handle->mutex, portMAX_DELAY);  // ← Acquire
    // ... SPI operations ...
    xSemaphoreGive(handle->mutex);  // ← Release
}
```

This ensures only one task accesses SPI3 at a time, preventing data corruption.

## Adding New Features

### Example: Create a Sensor Data Logging Task

1. Create the task:
```c
// In freertos.c, add:
void SensorLogTask(void const * argument) {
    float temperature = 0.0f;
    
    for(;;) {
        // Read sensor
        temperature = get_temperature();
        
        // Log to SD card (thread-safe via mutex)
        char line[64];
        snprintf(line, sizeof(line), "%.2f\r\n", temperature);
        sd_append_file("sensor_log.csv", line);
        
        osDelay(5000);  // Every 5 seconds
    }
}
```

2. Create the task in `osThreadCreate()`:
```c
osThreadDef(SensorLogTask, SensorLogTask, osPriorityNormal, 0, 128);
osThreadCreate(osThread(SensorLogTask), NULL);
```

The mutex in `sd_spi.c` automatically handles synchronization!

## Debugging

### Enable Verbose Logging

Edit [Drivers/sd_card/Src/sd_spi.c](Drivers/sd_card/Src/sd_spi.c):
```c
#define SD_DEBUG 1  // Add at top of file

#ifdef SD_DEBUG
#define SD_LOG printf
#else
#define SD_LOG(...) do {} while(0)
#endif
```

Then add `SD_LOG()` calls throughout:
```c
uint8_t SD_SPI_Init(SD_Handle_t *sd_handle) {
    SD_LOG("Initializing SD card...\r\n");
    // ... init code ...
    SD_LOG("SD card initialized, SDHC=%d\r\n", sd_handle->is_sdhc);
}
```

### Inspect SD Card State

Create a debug command:
```c
void sd_debug_info(void) {
    extern SD_Handle_t g_sd_handle;
    
    printf("SD Handle Debug Info:\r\n");
    printf("  Initialized: %d\r\n", g_sd_handle.initialized);
    printf("  Is SDHC: %d\r\n", g_sd_handle.is_sdhc);
    printf("  Use DMA: %d\r\n", g_sd_handle.use_dma);
    printf("  Mutex: %p\r\n", (void*)g_sd_handle.mutex);
    
    // Test read
    uint8_t test_block[512];
    int result = SD_ReadBlocks(&g_sd_handle, test_block, 0, 1);
    printf("  Read Block 0: %s\r\n", result ? "FAILED" : "OK");
}
```

### Hardware Debugging

**Check SPI Communication**:
```c
// Verify SPI is working
HAL_SPI_Transmit(&hspi3, test_data, 1, 100);
HAL_SPI_Receive(&hspi3, recv_data, 1, 100);
printf("SPI Test: Sent=0x%02X, Received=0x%02X\r\n", test_data[0], recv_data[0]);
```

**Verify GPIO Connections**:
```c
// Toggle CS pin manually
HAL_GPIO_WritePin(SPI1_CS_GPIO_Port, SPI1_CS_Pin, GPIO_PIN_RESET);
HAL_Delay(100);
HAL_GPIO_WritePin(SPI1_CS_GPIO_Port, SPI1_CS_Pin, GPIO_PIN_SET);
printf("CS pin toggled\r\n");
```

## Memory Analysis

### Current Usage
```
Component          RAM        FLASH
─────────────────────────────────────
FreeRTOS kernel    ~4 KB      ~8 KB
SD driver          ~1 KB      ~6 KB
FatFs              ~2 KB      ~15 KB
Application        ~12 KB     ~30 KB
─────────────────────────────────────
Total              ~20 KB     ~59 KB
Available          108 KB     453 KB
```

### Stack Requirements
```c
// Adjust task stack size if needed
osThreadDef(StartDefaultTask, StartDefaultTask, osPriorityNormal, 0, 512);
                                                                        ↑
                                                    Stack size in words (512 = 2KB)
```

If you get stack overflow, increase this value.

## Performance Optimization

### Increase SPI Speed
Edit [Core/Src/spi.c](Core/Src/spi.c):
```c
hspi3.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_128;  // ← From 256
// 8 MHz / 128 = 62.5 kHz (vs 31.25 kHz)
// Trade-off: Higher speed = potential for signal integrity issues
```

### Enable DMA Transfers
Default is already enabled. Verify in [Drivers/sd_card/Src/sd_spi.c](Drivers/sd_card/Src/sd_spi.c):
```c
SD_Handle_t sd_handle = {
    .use_dma = true,  // ← Enable for performance
    // ...
};
```

### Reduce Card Detection Retries
Edit [Drivers/sd_card/Src/sd_spi.c](Drivers/sd_card/Src/sd_spi.c):
```c
for (int i = 0; i < 10; i++) {  // ← Reduce from 100 if card responds quickly
    // ...
}
```

## Common Issues and Solutions

### Issue: "No STM32 target found"
```
Error: Unable to get core ID
Error: No STM32 target found!
```

**Solutions**:
1. Check SWD connections (PA13, PA14, GND)
2. Power cycle the board
3. Try manual reset: Hold reset button for 2 seconds
4. Update ST-LINK firmware: `STM32CubeProgrammer` GUI → ST-LINK → Update

### Issue: SD Card Not Detected
```
SD system initialization failed!
```

**Checklist**:
1. ✅ Verify SPI3 connections: PC10 (CLK), PC11 (MISO), PC12 (MOSI)
2. ✅ Check CS pin: PD1
3. ✅ SD card is FAT32 formatted
4. ✅ Run `sd_benchmark()` to verify SPI communication

### Issue: File Operations Fail
```
Failed to write file!
```

**Debug**:
```c
if (sd_mount() != FR_OK) {
    printf("Mount failed. Checking sd_spi status...\r\n");
    FRESULT res = f_stat("", NULL);  // Try to stat root
    printf("f_stat result: %d\r\n", res);
}
```

### Issue: FreeRTOS Task Not Running
```
// Nothing prints
```

**Check**:
1. Is `StartDefaultTask` being called? (Should start after RTOS kernel begins)
2. Check task creation in `osThreadCreate()` - verify stack size is sufficient
3. Add debug output before any SD operations

## Testing Checklist

Before deploying to production:

- [ ] **Build**: `cube-cmake --build build/Debug` completes with no errors
- [ ] **Program**: Device flashes and resets
- [ ] **Serial Output**: UART1 shows initialization messages
- [ ] **SD Mount**: "SD card mounted" message appears
- [ ] **File Write**: Can create and write files
- [ ] **File Read**: Can read files back correctly
- [ ] **CSV Operations**: CSV data written and parsed correctly
- [ ] **Benchmark**: Performance tests show reasonable speeds (>1 MB/s read)
- [ ] **Multi-Task**: Multiple tasks can safely access SD card
- [ ] **Power Cycle**: All operations survive device reset

## Repository Structure Reference

```
Key Files for Development:
├── Core/Src/freertos.c              ← Main application task
├── Drivers/sd_card/Inc/sd_spi.h     ← Driver API (struct-based)
├── Drivers/sd_card/Src/sd_spi.c     ← Driver implementation (mutex-protected)
├── Drivers/sd_card/Inc/sd_functions.h    ← High-level file ops
├── Drivers/sd_card/Src/sd_functions.c    ← Implementation
├── CMakeLists.txt                   ← Build configuration
├── .vscode/tasks.json               ← VS Code automation
└── README.md                         ← Quick reference

Configuration Files:
├── Core/Inc/FreeRTOSConfig.h        ← FreeRTOS settings
├── FATFS/Target/ffconf.h            ← FatFs configuration
├── Core/Inc/stm32f4xx_hal_conf.h    ← HAL configuration
└── STM32F446XX_FLASH.ld             ← Linker script
```

## Next Steps

1. **Build**: Run the build task and verify no errors
2. **Connect Hardware**: Attach ST-LINK and SD card
3. **Program**: Use "Build and Program" task
4. **Test**: Monitor serial output and run file operations
5. **Extend**: Add your own SD-based features!

---

**Questions?** Check the implementation files - they're heavily documented!
