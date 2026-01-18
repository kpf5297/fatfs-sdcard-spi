# UART Serial Monitor Setup

## Quick Start: View SD Card Activity Over UART

The firmware now outputs all SD card operations to UART2 (115200 baud, 8N1). This allows you to see:
- Initialization progress
- File read/write operations with status indicators
- CSV data parsing
- Performance benchmark results
- Error messages with detailed troubleshooting info

## Hardware Setup

### UART2 Connections (STM32F446)
```
STM32F446         Serial Adapter (USB-UART)
----------        ---------
PA2 (UART2_TX) -> RX
PA3 (UART2_RX) -> TX
GND            -> GND
```

**Popular USB-UART Adapters:**
- CH340/CH341
- FT232RL
- CP2102
- PL2303

**Baud Rate:** 115200, 8 data bits, 1 stop bit, no parity (8N1)

## Software: Connect to Serial Monitor

### macOS
```bash
# List available ports
ls /dev/tty.usbserial* /dev/tty.usbmodem*

# Connect with screen (easiest)
screen /dev/tty.usbserial-xxxxx 115200

# Or with minicom
brew install minicom
minicom -D /dev/tty.usbserial-xxxxx -b 115200

# Exit screen: Ctrl+A then Ctrl+D
```

### Linux
```bash
# List available ports
ls /dev/ttyUSB* /dev/ttyACM*

# Connect with screen
screen /dev/ttyUSB0 115200

# Or with minicom
minicom -D /dev/ttyUSB0 -b 115200

# Or with picocom
sudo picocom /dev/ttyUSB0 -b 115200

# Exit: Ctrl+X
```

### Windows
```bash
# List available COM ports
wmic logicaldisk get name  # Identifies available ports

# Option 1: PuTTY (GUI)
# Download from putty.org, connect to COM port at 115200 baud

# Option 2: Tera Term (GUI)
# Download from teraterm.org

# Option 3: PowerShell
# Use Windows Terminal or PowerShell (more complex, use GUI recommended)
```

### VS Code Serial Monitor
```
1. Install "Serial Monitor" extension (Jan Lohrbauer)
2. Press Ctrl+Alt+D to open serial monitor
3. Select COM port and 115200 baud
4. View output in real-time
```

## Sample Output

When you program the device and connect UART, you'll see:

```
╔══════════════════════════════════════════════════════════╗
║       STM32F446 SD Card + FatFs + FreeRTOS v1.0          ║
║           Deterministic Thread-Safe Driver              ║
╚══════════════════════════════════════════════════════════╝

[SYSTEM] Initializing SD card system...
✅ SD system initialized successfully

========================================
SD CARD MOUNT PROCEDURE
========================================
[1/4] Linking SD driver...
  ✅ SD driver linked
[2/4] Initializing disk...
  ✅ Disk initialized
[3/4] Mounting filesystem at 0:/...
  ✅ Filesystem mounted successfully
[4/4] Checking SD card properties...
  Card Type: SDHC/SDXC
  💾 Total: 3843072 KB, Free: 3843040 KB
========================================

========================================
FILE OPERATIONS TEST
========================================

[TEST 1] Writing file (test.txt)...
  ✅ Wrote 50 bytes to test.txt
✅ Test 1 PASSED

[TEST 2] Reading file (test.txt)...
Content:
Hello World from SD Card!
This is a test file.
  ✅ Read 50 bytes from test.txt
✅ Test 2 PASSED

[TEST 3] Appending to file...
  ✅ Appended 31 bytes to test.txt
✅ Test 3 PASSED

[TEST 4] Creating directory (test_dir)...
✅ Test 4 PASSED

[TEST 5] Writing nested file (test_dir/nested.txt)...
  ✅ Wrote 38 bytes to test_dir/nested.txt
✅ Test 5 PASSED

[TEST 6] Listing all files...
📁 /
  └─ 📄 test.txt (81 bytes)
  └─ 📁 test_dir/
     └─ 📄 nested.txt (38 bytes)
  └─ 📄 data.csv (85 bytes)
✅ Test 6 PASSED

========================================
CSV OPERATIONS TEST
========================================

[TEST 7] Creating CSV file (data.csv)...
  ✅ Wrote 14 bytes to data.csv
  ✅ Appended 26 bytes to data.csv
  ✅ Appended 27 bytes to data.csv
  ✅ Appended 25 bytes to data.csv
  ✅ Appended 26 bytes to data.csv
✅ Test 7 PASSED - 4 sensor records written

[TEST 8] Reading CSV file...
CSV data parsed: 4 records
  [1] Sensor01 | Temperature | 25
  [2] Sensor02 | Humidity | 65
  [3] Sensor03 | Pressure | 1013
  [4] Sensor04 | Light | 450
✅ Test 8 PASSED

========================================
STORAGE INFORMATION
========================================

[TEST 9] Checking SD card capacity...
  💾 Total: 3843072 KB, Free: 3843040 KB
✅ Test 9 PASSED

========================================
PERFORMANCE BENCHMARK TEST
========================================

[TEST 10] Running read/write speed test...
📊 SD Card Benchmark Results
  Read Speed:  2048 KB/s
  Write Speed: 1024 KB/s
  Buffer Size: 4096 bytes
✅ Test 10 PASSED

========================================
CLEANUP
========================================

✅ SD card unmounted successfully

╔══════════════════════════════════════════════════════════╗
║              Demo Completed Successfully!                ║
║     All SD card functions tested and working properly    ║
╚══════════════════════════════════════════════════════════╝
```

## Adding Your Own Debug Output

You can add printf statements anywhere in your code - it will automatically output to UART2:

```c
// In any SD driver or application code:
printf("My debug message: value=%d\r\n", my_var);

// With formatting:
printf("  ✅ Operation successful\r\n");
printf("  ❌ Operation failed\r\n");
printf("  📊 Data: %d bytes read\r\n", bytes);
```

The printf output is thread-safe - multiple FreeRTOS tasks can call printf simultaneously.

## Troubleshooting Serial Connection

### No Output Appearing
1. **Wrong COM port?** Check device manager or `ls /dev/tty*`
2. **Wrong baud rate?** Must be exactly **115200**
3. **Cable issue?** Try different USB cable
4. **Driver missing?** Install CH340/FT232 drivers for your adapter
5. **Device not programmed?** Make sure firmware was flashed (LED should be on)

### Garbage Characters
- Baud rate mismatch - verify it's 115200
- Corrupted firmware - reprogram device

### Intermittent Loss of Connection
- USB cable too long (>3m) or poor quality
- USB port may be underpowered - try different port
- Add ferrite core to USB cable near adapter

## Logging to File (Optional Advanced)

To capture the full output session:

**On macOS/Linux:**
```bash
# With script
script -c "screen /dev/tty.usbserial-xxxxx 115200" output.log

# Or with tee
screen /dev/tty.usbserial-xxxxx 115200 | tee output.log
```

**Using minicom logging:**
```bash
minicom -D /dev/tty.usbserial-xxxxx -b 115200 -C capture.log
```

The log file will contain all UART output for later analysis.

## Multiple Tasks Logging

The firmware is configured with FreeRTOS. Multiple tasks can safely call printf() - the UART output is automatically synchronized:

```c
// Task 1
void Task1(void *arg) {
    for(;;) {
        printf("Task 1: Sensor reading\r\n");
        osDelay(1000);
    }
}

// Task 2
void Task2(void *arg) {
    for(;;) {
        printf("Task 2: Writing to SD card\r\n");
        osDelay(2000);
    }
}
```

Both tasks will output cleanly to the same UART without corruption - the mutex-protected drivers ensure synchronization.

---

**Need Help?**
- Check baud rate: should be 115200
- Verify connections: TX to RX, RX to TX, GND to GND
- Try different serial terminal program
- Ensure device is powered and reset after programming
