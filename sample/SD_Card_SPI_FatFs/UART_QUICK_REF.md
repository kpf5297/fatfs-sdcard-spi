# UART Serial Output - Quick Reference Card

## 🔌 Hardware Connections (5-minute setup)

```
STM32F446 Pin    USB-UART Adapter
─────────────────────────────────
PA2 (UART2_TX)   RX (input)
PA3 (UART2_RX)   TX (output)  
GND              GND (ground)
```

**Baud Rate:** 115200 (8N1)

---

## 🖥️ Connect to Serial Monitor (Pick One)

### **macOS/Linux - Fastest Way**
```bash
# Find your port
ls /dev/tty.usbserial*    # macOS
ls /dev/ttyUSB*           # Linux

# Connect (exit with Ctrl+A then D)
screen /dev/tty.usbserial-ABC123 115200
```

### **Windows - Use GUI**
- **PuTTY**: Download putty.org → Select COM port → 115200 baud → Open
- **Tera Term**: Download teraterm.org → Serial → 115200 → Connect

### **VS Code - Built-in**
1. Install "Serial Monitor" extension
2. Press `Ctrl+Alt+D`
3. Select COM port + 115200 → View output

---

## 📋 What You'll See

### At Startup
```
╔══════════════════════════════════════════════════════════╗
║       STM32F446 SD Card + FatFs + FreeRTOS v1.0          ║
║           Deterministic Thread-Safe Driver              ║
╚══════════════════════════════════════════════════════════╝

[SYSTEM] Initializing SD card system...
✅ SD system initialized successfully
```

### SD Card Mount
```
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
```

### File Operations
```
[TEST 1] Writing file (test.txt)...
  ✅ Wrote 50 bytes to test.txt
✅ Test 1 PASSED

[TEST 2] Reading file (test.txt)...
  ✅ Read 50 bytes from test.txt
```

---

## 🛠️ Build & Program

### Full Build
```bash
# In VS Code
Ctrl+Shift+B          # Build (default task)
Ctrl+Shift+P → "Build and Program"   # Program device
```

### From Terminal
```bash
cd ~/Code/SD_Card_SPI_FatFs
cube-cmake --build build/Debug        # Build
# Then connect serial monitor to see output
```

---

## 💡 Add Your Own Debug Output

```c
// Anywhere in your code - automatically goes to UART2:
printf("My debug message\r\n");

// With status indicators:
printf("✅ Success!\r\n");
printf("❌ Error code: %d\r\n", error);

// Formatted data:
printf("Read %d bytes at speed %lu KB/s\r\n", bytes, speed);
```

### Multi-Task Safe
```c
// Multiple tasks can safely call printf() simultaneously
// The mutex-based drivers ensure no corruption
void Task1(void *arg) { printf("Task 1 data\r\n"); }
void Task2(void *arg) { printf("Task 2 data\r\n"); }
```

---

## 🔧 Troubleshooting

| Problem | Solution |
|---------|----------|
| No output | Check USB cable, verify baud rate is **115200** |
| Garbage text | Wrong baud rate - must be 115200 |
| Connection drops | Try different USB port or cable |
| "Device not found" | Install USB driver (CH340/FT232/CP2102) |
| Permission denied (Linux) | `sudo usermod -a -G dialout $USER` then reboot |

---

## 📁 Files Modified

| File | Change |
|------|--------|
| [Core/Src/main.c](Core/Src/main.c) | Added UART printf redirection |
| [Drivers/sd_card/Src/sd_functions.c](Drivers/sd_card/Src/sd_functions.c) | Enhanced logging with status icons |
| [Core/Src/freertos.c](Core/Src/freertos.c) | Comprehensive 10-test demo |

---

## 📚 Documentation

| Document | Purpose |
|----------|---------|
| [UART_SETUP.md](UART_SETUP.md) | Detailed hardware & software setup |
| [UART_INTEGRATION.md](UART_INTEGRATION.md) | Technical integration details |
| [README.md](README.md) | Project overview |
| [DEVELOPMENT.md](DEVELOPMENT.md) | Developer guide |

---

## ✅ Status Indicators Used

| Symbol | Meaning |
|--------|---------|
| ✅ | Success / Passed / OK |
| ❌ | Failed / Error |
| 📁 | Directory |
| 📄 | File |
| 💾 | Storage capacity |
| 📊 | Performance / Benchmark |

---

## 🚀 Next Steps

1. **Build**: `Ctrl+Shift+B` in VS Code
2. **Connect serial**: Plug in USB-UART adapter
3. **Program**: `Ctrl+Shift+P` → Run Task → "Build and Program"
4. **Watch**: Monitor serial output - see all SD operations in real-time
5. **Extend**: Add your own `printf()` calls for custom debugging

---

## 💾 Memory Usage

```
Component        RAM          FLASH
─────────────────────────────────────
Core + HAL       ~8 KB        ~15 KB
FreeRTOS         ~4 KB        ~8 KB
SD Driver        ~1 KB        ~6 KB
FatFs            ~2 KB        ~15 KB
Application      ~4 KB        ~18 KB
─────────────────────────────────────
Total           ~20 KB (15%)  ~62 KB (12%)
Available      ~108 KB        ~450 KB
```

Plenty of room for your applications!

---

## 🎯 Key Features

- ✅ Real-time SD card activity monitoring
- ✅ Automatic error reporting with details
- ✅ Performance benchmarking with output
- ✅ Thread-safe multi-task operation logging
- ✅ No overhead - uses existing HAL functions
- ✅ Simple integration - just use `printf()`

---

**Everything is ready! Connect your serial adapter and watch the magic happen.** 🎉

Build succeeded ✅  
UART logging enabled ✅  
Comprehensive demo ready ✅  

Just connect a serial monitor at 115200 baud to PA2/PA3 and you'll see all the action!
