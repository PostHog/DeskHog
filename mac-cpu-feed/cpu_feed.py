#!/usr/bin/env python3
"""Stream this Mac's CPU load to a DeskHog running the CPU Hog card.

Samples CPU usage about once a second and writes "CPU:<0-100>\\n" to the
DeskHog's USB serial port. The firmware uses this to drive Max's running
speed; if the feed stops, the card falls back to the device's own CPU after
a few seconds and the label flips from "MAC" back to "CPU".

Usage:
    python3 cpu_feed.py                       # auto-detect the DeskHog port
    python3 cpu_feed.py /dev/tty.usbmodem1101 # or pass the port explicitly

Pure standard library - no pip installs required. macOS only (uses `top`).
"""

import glob
import re
import subprocess
import sys
import time

BAUD = 115200
IDLE_RE = re.compile(r"CPU usage:.*?([\d.]+)%\s+idle")


def find_port():
    """Return the first likely DeskHog serial device, or None."""
    for pattern in ("/dev/tty.usbmodem*", "/dev/cu.usbmodem*"):
        matches = sorted(glob.glob(pattern))
        if matches:
            return matches[0]
    return None


def read_cpu_percent():
    """Return macOS CPU busy percent (0-100) as 100 - idle.

    `top -l 2` prints two samples; the second reflects the last ~1s of
    activity, so this call also paces the loop to roughly one reading/sec.
    """
    out = subprocess.check_output(
        ["top", "-l", "2", "-n", "0", "-s", "1"],
        text=True,
    )
    idle = None
    for line in out.splitlines():
        m = IDLE_RE.search(line)
        if m:
            idle = float(m.group(1))  # keep the last (second) sample
    if idle is None:
        return 0
    return max(0, min(100, round(100 - idle)))


def configure(port):
    """Set 8N1 at BAUD and keep modem control lines from resetting the board."""
    subprocess.run(
        ["stty", "-f", port, str(BAUD),
         "clocal", "-hupcl", "cs8", "-cstopb", "-parenb"],
        check=True,
    )


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else find_port()
    if not port:
        sys.exit(
            "No DeskHog serial port found (looked for /dev/tty.usbmodem*). "
            "Plug it in, or pass the port explicitly."
        )

    print(f"Feeding Mac CPU to DeskHog on {port} (Ctrl-C to stop)")
    configure(port)

    with open(port, "wb", buffering=0) as ser:
        while True:
            try:
                load = read_cpu_percent()
            except subprocess.CalledProcessError:
                time.sleep(1)
                continue
            try:
                ser.write(f"CPU:{load}\n".encode())
            except OSError as exc:
                sys.exit(f"\nLost the serial port ({exc}). Is the DeskHog still connected?")
            print(f"  MAC CPU {load:3d}%   ", end="\r", flush=True)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nStopped.")
