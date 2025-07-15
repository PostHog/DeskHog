# Multi-Board Flash Utility for DeskHog

This utility automatically detects and flashes DeskHog firmware to multiple ESP32-S3 boards as they are connected.

## Features

- Automatically detects new ESP32-S3 Feather boards when plugged in
- Maintains an in-memory list of already-flashed boards to prevent re-flashing
- Supports parallel flashing of multiple boards
- Real-time monitoring of board connections/disconnections
- Live progress display with visual progress bars
- Status tracking for each board (building, flashing, verifying, etc.)
- Verbose mode for debugging

## Requirements

- Python 3.6+
- PlatformIO CLI (`pip install platformio`)
- pyserial (`pip install pyserial`)
- USB drivers for ESP32-S3

## Usage

### Basic Usage

From the DeskHog project root directory:

```bash
python multi_flash.py
```

Or if you made it executable:

```bash
./multi_flash.py
```

### Command Line Options

- `-v, --verbose`: Show detailed flashing output
- `-f, --firmware <path>`: Use a specific firmware file instead of building
- `-r, --reset`: Clear the list of flashed boards and exit

### Examples

1. **Basic monitoring and flashing:**
   ```bash
   python multi_flash.py
   ```

2. **Verbose mode (see detailed upload progress):**
   ```bash
   python multi_flash.py -v
   ```

3. **Flash a specific firmware file:**
   ```bash
   python multi_flash.py -f .pio/build/adafruit_feather_esp32s3_reversetft/firmware.bin
   ```

4. **Reset the flashed board memory:**
   ```bash
   python multi_flash.py -r
   ```

## How It Works

1. The script continuously scans for connected USB devices
2. When it detects an ESP32-S3 board (by VID/PID or description), it checks if it's already been flashed
3. New boards are automatically flashed using PlatformIO's upload command
4. Successfully flashed boards are remembered to prevent re-flashing
5. The script handles multiple boards simultaneously using threading
6. A real-time display shows:
   - Active flashing operations with progress bars
   - Current status (building, connecting, writing, verifying)
   - Completed boards with success/failure indicators
   - Total count of flashed boards

## Board Detection

The script identifies ESP32-S3 boards by:
- USB Vendor ID: 0x303A (Espressif) or 0x239A (Adafruit)
- USB Product ID: Various ESP32-S3 identifiers
- Device description containing "ESP32", "ESP32-S3", "Adafruit", or "Feather"

## Troubleshooting

### Board not detected
- Make sure the board is in normal mode (not bootloader mode)
- Check USB cable and connection
- Try verbose mode to see what devices are being detected

### Flashing fails
- The board might need to be put in bootloader mode manually (hold D0 while pressing Reset)
- Check that PlatformIO is properly installed
- Ensure you're running from the DeskHog project root directory

### Board keeps getting re-flashed
- This might happen if the board doesn't have a unique serial number
- Try disconnecting and reconnecting the board
- Use the reset option (`-r`) to clear the memory if needed

## Tips

- Keep the script running while connecting multiple boards
- Boards can be connected one at a time or multiple at once
- The display automatically refreshes to show real-time progress
- Progress bars show the current stage of flashing (0-100%)
- Status indicators:
  - 🔄 = Currently flashing
  - ✅ = Successfully flashed
  - ❌ = Failed to flash
  - ⚠️  = Disconnected during flash
- Press Ctrl+C to stop monitoring

## Display Example

```
================================================================================
DeskHog Multi-Board Flash Monitor
================================================================================
Time: 14:32:15 | Press Ctrl+C to stop
--------------------------------------------------------------------------------

📊 Board Status (2 flashed, 1 active):

  🔄 /dev/cu.usbmodem14201 [████████████████░░░░░░░░░░░░░] 54% - Writing firmware
  ✅ 303A:1001:ABCD1234                                     - ✓ Successfully flashed
  ✅ 239A:811C:XYZ789                                       - ✓ Successfully flashed

--------------------------------------------------------------------------------
💡 Tip: Boards are automatically detected and flashed when plugged in
```