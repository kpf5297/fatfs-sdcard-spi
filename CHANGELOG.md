# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2024-01-18

### Added
- Initial release of FatFS SD Card SPI Driver
- Production-ready SPI SD card driver for STM32 microcontrollers
- Support for SDSC and SDHC cards
- FatFS diskio layer integration
- FreeRTOS thread-safety with semaphores
- Deterministic timeouts and explicit error reporting
- High-level helper functions for file operations
- Benchmarking utilities
- Comprehensive documentation (README.md)
- MIT License
- CI/CD pipeline with GitHub Actions
- Code formatting with clang-format

### Features
- Thread-safe access with FreeRTOS
- Blocking APIs requiring task context
- Explicit error status reporting
- DMA support option
- Performance benchmarking tools