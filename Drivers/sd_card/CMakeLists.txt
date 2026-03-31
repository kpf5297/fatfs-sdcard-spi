cmake_minimum_required(VERSION 3.22)

# ============================================================================
# SD Card SPI Driver - Modular CMake Configuration
#
# This driver is FreeRTOS-compliant and provides thread-safe, interrupt-safe
# access to SDSC/SDHC cards via SPI. It supports both DMA and polling modes
# with deterministic timeouts.
#
# Usage: add_subdirectory(path/to/sd_card)
#
# This creates the 'sd_card' OBJECT library which can be added to any target:
#   target_link_libraries(${MY_TARGET} sd_card)
#
# Requirements from parent project:
# ---- REQUIRED ----
# - main.h: SPI_HandleTypeDef, GPIO declarations
# - HAL headers: stm32f4xx_hal.h (or appropriate variant)
# - FatFS headers: diskio.h, ff.h, ff_gen_drv.h (if using with FatFS)
#
# ---- CONDITIONAL (if USE_FREERTOS=ON) ----
# - FreeRTOS.h, task.h, semphr.h
# - FreeRTOSConfig.h with:
#     configUSE_MUTEXES = 1 (REQUIRED)
#     configUSE_COUNTING_SEMAPHORES = 1 (REQUIRED)
#     configTICK_RATE_HZ >= 1000 (strongly recommended)
#     configSUPPORT_STATIC_ALLOCATION or configSUPPORT_DYNAMIC_ALLOCATION (one or both)
#
# See FREERTOS_COMPLIANCE.md for detailed FreeRTOS integration requirements.
# ============================================================================

# Define source files
set(SD_CARD_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/sd_spi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/sd_diskio_spi.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/sd_functions.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Src/sd_benchmark.c
)

# Define public include directory
set(SD_CARD_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/Inc
)

# Create OBJECT library for flexible integration
add_library(sd_card OBJECT)

# Add sources
target_sources(sd_card PRIVATE ${SD_CARD_SOURCES})

# Export public include directory (used by consuming target)
target_include_directories(sd_card PUBLIC ${SD_CARD_INCLUDES})

# Compile options for clean, standard C99 code
target_compile_options(sd_card PRIVATE
    -Wall
    -Wextra
    -pedantic
    -Wno-unused-parameter  # Some HAL parameters are unused in our context
)

# Optional: Enable logging for debugging (can be overridden by parent)
# Usage: cmake -DSD_LOG_ENABLED=1 ...
if(NOT DEFINED SD_LOG_ENABLED)
    set(SD_LOG_ENABLED 0)
endif()
target_compile_definitions(sd_card PRIVATE SD_LOG_ENABLED=${SD_LOG_ENABLED})

# FreeRTOS Integration
# To enable FreeRTOS-safe operation, the parent project should define:
#   add_compile_definitions(USE_FREERTOS)
# BEFORE calling add_subdirectory(sd_card)
#
# Example in parent CMakeLists.txt:
#   ----- option(USE_FREERTOS "Enable FreeRTOS integration" ON) -----
#   if(USE_FREERTOS)
#       add_compile_definitions(USE_FREERTOS)
#   endif()
#   add_subdirectory(Drivers/sd_card)
#
# The driver will validate FreeRTOS configuration at compile time.

# Configuration macros (can be overridden via -D flags)
# Examples:
#   cmake -DSD_BLOCK_SIZE=512 ...
#   cmake -DSD_INIT_TIMEOUT_MS=2000 ...
#   cmake -DSD_DMA_TIMEOUT_MS=500 ...
#
# See sd_spi.h for all available configuration options

# Notes:
# ----
# 1. This is an OBJECT library, not STATIC. It compiles to .o files that are
#    linked directly into the consuming executable. This is optimal for:
#    - Minimal binary size (no duplicate code across projects)
#    - Flexible integration (no separate .a file to manage)
#    - Easy distribution (copy folder, add one CMake line)
#
# 2. All dependencies (FreeRTOS, HAL, FatFS headers) must be resolvable
#    when the consuming project calls:
#       target_link_libraries(${MY_TARGET} sd_card)
#
# 3. Compile definitions like USE_FREERTOS must be set BEFORE linking:
#    ------ Correct order: ------
#    add_compile_definitions(USE_FREERTOS)  # First
#    add_subdirectory(Drivers/sd_card)      # Second
#    target_link_libraries(app sd_card)     # Third
#
# 4. To use in your top-level CMakeLists.txt:
#    ----
#    # Enable FreeRTOS integration
#    add_compile_definitions(USE_FREERTOS)
#
#    # Add the driver
#    add_subdirectory(Drivers/sd_card)
#
#    # Link to main executable
#    target_link_libraries(${MY_EXECUTABLE} sd_card)
#    ----
#
# 5. For debug logging, enable at project level:
#    ------ In top-level CMakeLists.txt: ------
#    if(CMAKE_BUILD_TYPE MATCHES Debug)
#        add_compile_definitions(SD_LOG_ENABLED=1)
#    endif()
#    ----
#
# 6. Multi-instance support:
#    - Primary SD: DMA enabled (recommended for speed)
#    - Secondary SD: polling only (falls back automatically)
#    - See FREERTOS_COMPLIANCE.md for details
#
# For comprehensive FreeRTOS compliance details, see:
# - FREERTOS_COMPLIANCE.md (in this directory)
# - INTEGRATION_GUIDE.md (for generic integration)
# ============================================================================

