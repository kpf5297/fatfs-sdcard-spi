cmake_minimum_required(VERSION 3.22)
project(sd_card_tests C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Unity test framework
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG        v2.6.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(unity)

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
set(DRIVER_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/../Drivers/sd_card)
set(MOCKS_DIR   ${CMAKE_CURRENT_SOURCE_DIR}/mocks)
set(TESTS_DIR   ${CMAKE_CURRENT_SOURCE_DIR})

# ---------------------------------------------------------------------------
# Common compile options and definitions
# ---------------------------------------------------------------------------
set(TEST_COMPILE_OPTIONS
    -Wall
    -Wextra
    -Wno-unused-parameter   # HAL stubs intentionally discard all params
    -Wno-unused-function    # Some test helpers unused in a given TU
)

set(TEST_COMPILE_DEFS
    SD_LOG_ENABLED=0        # Suppress all SD_LOG output
    # USE_FREERTOS is intentionally NOT defined: all FreeRTOS mutex/semaphore
    # code is compiled out, keeping tests host-native with no RTOS dependency.
)

# ---------------------------------------------------------------------------
# Source sets
# ---------------------------------------------------------------------------
set(MOCK_SOURCES
    ${TESTS_DIR}/mock_hal.c
)

set(DRIVER_CORE
    ${DRIVER_DIR}/Src/sd_spi.c
)

set(DRIVER_DISKIO
    ${DRIVER_DIR}/Src/sd_diskio_spi.c
)

# ---------------------------------------------------------------------------
# Include paths shared by all test targets.
# mocks/ comes first so that #include "main.h", "diskio.h", "ff_gen_drv.h"
# resolve to our stub headers rather than any embedded SDK headers.
# ---------------------------------------------------------------------------
set(TEST_INCLUDES
    ${MOCKS_DIR}            # stub headers (main.h, diskio.h, ff_gen_drv.h)
    ${TESTS_DIR}            # mock_hal.h, test_helpers.h
    ${DRIVER_DIR}/Inc       # sd_spi.h, sd_diskio_spi.h
)

# ---------------------------------------------------------------------------
# Helper macro: define one test executable
# ---------------------------------------------------------------------------
macro(add_sd_test target src)
    add_executable(${target}
        ${src}
        ${MOCK_SOURCES}
        ${DRIVER_CORE}
        ${ARGN}             # optional extra driver sources (e.g. sd_diskio_spi.c)
    )
    target_include_directories(${target} PRIVATE ${TEST_INCLUDES})
    target_compile_options(${target}    PRIVATE ${TEST_COMPILE_OPTIONS})
    target_compile_definitions(${target} PRIVATE ${TEST_COMPILE_DEFS})
    target_link_libraries(${target}     PRIVATE unity)
    add_test(NAME ${target} COMMAND ${target})
endmacro()

# ---------------------------------------------------------------------------
# Test executables
# ---------------------------------------------------------------------------

# Initialization, card detect, accessors
add_sd_test(test_sd_init       ${TESTS_DIR}/test_sd_init.c)

# Single-block read / write
add_sd_test(test_sd_readwrite  ${TESTS_DIR}/test_sd_readwrite.c)

# Multi-block read / write (CMD18 / CMD25)
add_sd_test(test_sd_multiblock ${TESTS_DIR}/test_sd_multiblock.c)

# Sync, stats, error counters, DeInit
add_sd_test(test_sd_utils      ${TESTS_DIR}/test_sd_utils.c)

# FatFS diskio glue layer (needs sd_diskio_spi.c compiled in as well)
add_sd_test(test_sd_diskio     ${TESTS_DIR}/test_sd_diskio.c
                                ${DRIVER_DISKIO})

# ---------------------------------------------------------------------------
# CTest
# ---------------------------------------------------------------------------
enable_testing()
