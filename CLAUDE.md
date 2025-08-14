# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

DeskHog is an embedded IoT device project built on the Adafruit ESP32-S3 Reverse TFT Feather. It's a palm-sized developer toy with a 240x135 color display that visualizes PostHog analytics data through a card-based UI system. The project uses PlatformIO with the Arduino framework for ESP32.

## Core Architecture

### Multi-Core Task Isolation
**Critical**: DeskHog uses FreeRTOS dual-core architecture with strict task isolation:

- **Core 0 (Protocol CPU)**: WiFi, web server, PostHog API, NeoPixel control
- **Core 1 (Application CPU)**: LVGL graphics, UI rendering, input handling

**NEVER update UI elements from Core 0 tasks** - this will crash the device. Use `EventQueue` for all cross-core communication.

### Event-Driven Communication
Use `EventQueue` class for safe inter-task messaging. All UI updates must happen on the UI task (Core 1).

### Card-Based UI System
The UI consists of a stack of cards managed by:
- `CardController`: Manages card lifecycle and configuration
- `CardNavigationStack`: Handles UI transitions and animations
- Individual card classes: `ProvisioningCard`, `InsightCard`, `FriendCard`

## Development Commands

### Build and Flash
```bash
# Standard build and upload
pio run --target upload

# Build only
pio run

# Clean build
pio run --target clean

# Erase flash completely (use when OTA partitions cause boot issues)
pio run --target erase --target upload

# Monitor serial output with exception decoder
pio device monitor --filter esp32_exception_decoder
```

### Multi-Device Development
```bash
# Flash multiple boards simultaneously (requires multi_flash.py)
python multi_flash.py
```

### Asset Pipeline
Pre-build scripts automatically convert assets:
- `htmlconvert.py`: Inlines HTML/CSS/JS from `html/` into C++ headers
- `ttf2c.py`: Converts TTF fonts to LVGL-compatible C arrays
- `png2c.py`: Converts PNG sprites to C arrays

### Web UI Development
```bash
# Preview web portal locally (open in browser)
open html/portal.html
```

## Key Components

### Hardware Interface
- **Buttons**: 3-button navigation (Up/D6, Down/D0, Center/D1)
- **Reset Sequence**: Hold Down + Reset to enter bootloader mode
- **Power Management**: Hold Center + Down to enter deep sleep

### Configuration Management
- `ConfigManager`: Persistent storage using ESP32 preferences
- `CaptivePortal`: Web-based device configuration
- WiFi provisioning via QR code on first boot

### PostHog Integration
- `PostHogClient`: API client for fetching analytics data  
- `InsightParser`: Converts API responses to displayable format
- Supports multiple insight types (numeric, funnel, trends)

## Adding New Card Types

1. Add enum to `CardType` in `src/ui/CardController.h`
2. Create card class implementing LVGL UI
3. Register with `CardController::initializeCardTypes()`
4. Define factory function for dynamic instantiation

Web UI automatically adapts to new card types through registration system.

## Memory Management

- **PSRAM Enabled**: Use for graphics buffers and SSL operations
- **Partition Scheme**: Dual-app OTA partitions for firmware updates
- **Web Portal Budget**: Maximum 100KB for inlined HTML/CSS/JS assets

## Development Guidelines

### AI-Assisted Development
The project is optimized for Cursor IDE and AI coding assistants:
- Follow existing patterns strictly
- Use `EventQueue` for all cross-core communication
- Never update UI from wrong core
- Review AI suggestions for "LLM slop" (unused variables, unnecessary delays)

### Code Patterns
- Use RAII and smart pointers where possible
- Follow existing naming conventions
- Implement factory pattern for dynamic card creation
- Use event-driven architecture to prevent tight coupling

### Testing
```bash
# Unit tests (currently limited/commented out)
pio test

# Hardware-in-the-loop testing is primary verification method
```

## Troubleshooting

### Common Issues
- **Flash doesn't update**: Use "Erase Flash and Upload" from PlatformIO menu
- **Device won't boot**: Check OTA partition table, may need flash erase
- **UI crashes**: Verify UI updates only happen on Core 1/UI task
- **Memory issues**: Check PSRAM usage and buffer allocations

### Debug Output
Monitor with exception decoder enabled:
```bash
pio device monitor --filter esp32_exception_decoder
```

## Important Files

- `platformio.ini`: Build configuration and dependencies
- `tech-details.md`: Comprehensive technical documentation
- `src/main.cpp`: Entry point and task initialization
- `src/ui/CardController.*`: Core UI management system
- `src/EventQueue.*`: Inter-task communication system
- `html/portal.*`: Web configuration interface (gets inlined)

## Hardware Requirements

- Adafruit ESP32-S3 Reverse TFT Feather (Product #5691)
- Optional: 350mAh LiPoly battery
- 3D printed enclosure (files in `3d-printing/`)