# Mac CPU feed for the CPU Hog card

Streams your Mac's CPU load to a DeskHog running the **CPU Hog** card, so Max
runs on his wheel at the speed of *your computer's* CPU instead of the toy's own.

## Run it

1. Flash the firmware and add the **CPU Hog** card to your DeskHog.
2. Connect the DeskHog to your Mac over USB.
3. Run:

   ```sh
   python3 cpu_feed.py
   ```

   It auto-detects the DeskHog's serial port. If it can't, pass it explicitly:

   ```sh
   python3 cpu_feed.py /dev/tty.usbmodem1101
   ```

While it's running the card's label shows `MAC nn%`. Stop the script (Ctrl-C)
and within a few seconds the card falls back to the DeskHog's own CPU, and the
label flips back to `CPU nn%`.

## How it works

The script samples CPU usage once a second via `top` and writes lines like
`CPU:42\n` to the serial port. The firmware (`CpuHogCard`) parses those lines
and feeds the value to `CpuMonitor`, which prefers a fresh host reading over
its own idle-counter estimate.

- Pure Python standard library — no `pip install` needed.
- macOS only (uses `top`); the wire format is trivial, so porting the sender to
  Linux/Windows is just "print `CPU:<0-100>` once a second to the serial port."
- The DeskHog uses native USB CDC, so opening the port doesn't reset the board.
