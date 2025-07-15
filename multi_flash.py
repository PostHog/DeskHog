#!/usr/bin/env python3
"""
Multi-board flash utility for DeskHog
Automatically detects and flashes new ESP32-S3 boards when connected
"""

import serial.tools.list_ports
import subprocess
import time
import sys
import threading
import os
from datetime import datetime
from typing import Set, Dict, Optional
import argparse
import re
from collections import defaultdict

class BoardFlasher:
    def __init__(self, firmware_path: Optional[str] = None, verbose: bool = False):
        self.flashed_boards: Set[str] = set()
        self.running = True
        self.verbose = verbose
        self.firmware_path = firmware_path
        self.lock = threading.Lock()
        
        # Board status tracking
        self.board_status = {}  # board_id -> status string
        self.board_progress = {}  # board_id -> progress percentage
        self.active_flashing = {}  # board_id -> port device
        self.status_lock = threading.Lock()
        
        # ESP32-S3 vendor/product IDs
        self.target_vids = [0x303A]  # Espressif
        self.target_pids = [0x1001]  # ESP32-S3 USB JTAG/serial
        
        # Adafruit Feather ESP32-S3 can also appear as
        self.alt_vids = [0x239A]  # Adafruit
        self.alt_pids = [0x811C, 0x811D]  # Various Adafruit ESP32-S3 boards
        
    def log(self, message: str):
        timestamp = datetime.now().strftime("%H:%M:%S")
        print(f"[{timestamp}] {message}")
        
    def is_target_board(self, port) -> bool:
        """Check if the connected device is our target ESP32-S3 board"""
        if port.vid in self.target_vids and port.pid in self.target_pids:
            return True
        if port.vid in self.alt_vids and port.pid in self.alt_pids:
            return True
        
        # Also check description/manufacturer strings
        desc_lower = (port.description or "").lower()
        manuf_lower = (port.manufacturer or "").lower()
        
        esp32_keywords = ["esp32", "esp32-s3", "esp32s3"]
        adafruit_keywords = ["adafruit", "feather"]
        
        has_esp32 = any(kw in desc_lower or kw in manuf_lower for kw in esp32_keywords)
        has_adafruit = any(kw in desc_lower or kw in manuf_lower for kw in adafruit_keywords)
        
        return has_esp32 or has_adafruit
        
    def get_board_id(self, port) -> str:
        """Generate unique ID for a board based on serial number or port info"""
        if port.serial_number:
            return f"{port.vid:04X}:{port.pid:04X}:{port.serial_number}"
        else:
            # Fallback to location/hardware ID if no serial number
            return f"{port.vid:04X}:{port.pid:04X}:{port.device}"
            
    def scan_ports(self) -> Dict[str, any]:
        """Scan for connected ESP32-S3 boards"""
        boards = {}
        try:
            ports = serial.tools.list_ports.comports()
            for port in ports:
                if self.is_target_board(port):
                    board_id = self.get_board_id(port)
                    boards[board_id] = port
                    if self.verbose:
                        self.log(f"Found board: {port.device} - {port.description}")
        except Exception as e:
            self.log(f"Error scanning ports: {e}")
        return boards
        
    def update_status(self, board_id: str, status: str, progress: Optional[float] = None):
        """Update board status and progress"""
        with self.status_lock:
            self.board_status[board_id] = status
            if progress is not None:
                self.board_progress[board_id] = progress
                
    def parse_progress(self, line: str) -> Optional[float]:
        """Parse progress from esptool output"""
        # Look for percentage patterns like "Writing at 0x00010000... (6%)"
        match = re.search(r'\((\d+)%\)', line)
        if match:
            return float(match.group(1))
        return None
        
    def flash_board(self, port_device: str, board_id: str) -> bool:
        """Flash firmware to a specific board"""
        self.update_status(board_id, "Preparing to flash", 0)
        
        try:
            # Build the PlatformIO upload command
            cmd = ["pio", "run", "-t", "upload", "--upload-port", port_device]
            
            if self.firmware_path:
                # If specific firmware provided, use it
                cmd.extend(["--upload-command", f"esptool.py write_flash 0x0 {self.firmware_path}"])
            
            # Run the upload command
            process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                universal_newlines=True,
                bufsize=1
            )
            
            self.update_status(board_id, "Building firmware", 5)
            
            # Stream output in real-time
            for line in iter(process.stdout.readline, ''):
                if line:
                    line = line.rstrip()
                    if self.verbose:
                        print(f"  {line}")
                    
                    # Update status based on output
                    if "Building" in line:
                        self.update_status(board_id, "Building firmware", 10)
                    elif "Linking" in line:
                        self.update_status(board_id, "Linking firmware", 20)
                    elif "Retrieving maximum program size" in line:
                        self.update_status(board_id, "Checking firmware size", 25)
                    elif "Configuring upload protocol" in line:
                        self.update_status(board_id, "Configuring upload", 30)
                    elif "Connecting" in line:
                        self.update_status(board_id, "Connecting to board", 35)
                    elif "Chip is" in line:
                        self.update_status(board_id, "Connected to chip", 40)
                    elif "Erasing flash" in line:
                        self.update_status(board_id, "Erasing flash", 45)
                    elif "Writing at" in line:
                        progress = self.parse_progress(line)
                        if progress:
                            # Map 0-100% of writing to 50-95% of total progress
                            total_progress = 50 + (progress * 0.45)
                            self.update_status(board_id, f"Writing firmware", total_progress)
                    elif "Verifying" in line:
                        self.update_status(board_id, "Verifying firmware", 96)
                    elif "Leaving" in line or "Hard resetting" in line:
                        self.update_status(board_id, "Completing", 99)
                    
            process.wait()
            
            if process.returncode == 0:
                self.update_status(board_id, "✓ Successfully flashed", 100)
                return True
            else:
                self.update_status(board_id, f"✗ Failed (code: {process.returncode})", 0)
                return False
                
        except Exception as e:
            self.update_status(board_id, f"✗ Error: {str(e)[:30]}", 0)
            return False
            
    def display_status(self):
        """Display current status of all boards"""
        # Clear screen (works on Unix/Mac/Windows)
        os.system('cls' if os.name == 'nt' else 'clear')
        
        print("=" * 80)
        print("DeskHog multi-board flash monitor")
        print("=" * 80)
        print(f"Time: {datetime.now().strftime('%H:%M:%S')} | Press Ctrl+C to stop")
        print("-" * 80)
        
        # Display connected boards and their status
        with self.status_lock:
            if not self.board_status and not self.active_flashing:
                print("\n🔌 Waiting for boards... Connect ESP32-S3 boards to start flashing\n")
            else:
                print(f"\n📊 Board status ({len(self.flashed_boards)} flashed, {len(self.active_flashing)} active):\n")
                
                # Show active flashing boards first
                for board_id, port_device in self.active_flashing.items():
                    status = self.board_status.get(board_id, "Unknown")
                    progress = self.board_progress.get(board_id, 0)
                    
                    # Create progress bar
                    bar_width = 30
                    filled = int(bar_width * progress / 100)
                    bar = "█" * filled + "░" * (bar_width - filled)
                    
                    print(f"  🔄 {port_device[:20]:<20} [{bar}] {progress:3.0f}% - {status}")
                
                # Show completed boards
                for board_id in self.flashed_boards:
                    if board_id not in self.active_flashing and board_id in self.board_status:
                        status = self.board_status[board_id]
                        if "✓" in status:
                            print(f"  ✅ {board_id[:50]:<50} - {status}")
                        else:
                            print(f"  ❌ {board_id[:50]:<50} - {status}")
        
        print("\n" + "-" * 80)
        print("💡 Tip: Boards are automatically detected and flashed when plugged in")
        
    def monitor_loop(self):
        """Main monitoring loop"""
        if not os.path.exists("platformio.ini"):
            self.log("ERROR: platformio.ini not found. Run this script from the DeskHog project root.")
            return
            
        last_boards = {}
        last_display_time = 0
        display_interval = 0.1  # Update display every 100ms
        
        while self.running:
            try:
                current_boards = self.scan_ports()
                
                # Check for newly connected boards
                for board_id, port in current_boards.items():
                    if board_id not in last_boards and board_id not in self.flashed_boards:
                        with self.status_lock:
                            self.active_flashing[board_id] = port.device
                        
                        # Flash in a separate thread to not block monitoring
                        threading.Thread(
                            target=self._flash_worker,
                            args=(port.device, board_id),
                            daemon=True
                        ).start()
                        
                # Check for disconnected boards
                for board_id in last_boards:
                    if board_id not in current_boards:
                        with self.status_lock:
                            if board_id in self.active_flashing:
                                del self.active_flashing[board_id]
                                self.board_status[board_id] = "⚠️  Disconnected during flash"
                        
                last_boards = current_boards
                
                # Update display at regular intervals
                current_time = time.time()
                if current_time - last_display_time >= display_interval:
                    self.display_status()
                    last_display_time = current_time
                
                time.sleep(0.05)  # Poll more frequently for smoother updates
                
            except KeyboardInterrupt:
                break
            except Exception as e:
                self.log(f"Error in monitor loop: {e}")
                time.sleep(1)
                
    def _flash_worker(self, port_device: str, board_id: str):
        """Worker thread for flashing a board"""
        with self.lock:
            if board_id in self.flashed_boards:
                return
                
            self.flashed_boards.add(board_id)
            
        success = self.flash_board(port_device, board_id)
        
        # Clean up active flashing status
        with self.status_lock:
            if board_id in self.active_flashing:
                del self.active_flashing[board_id]
        
        if not success:
            # Remove from flashed set so it can be retried
            with self.lock:
                self.flashed_boards.discard(board_id)
                
    def run(self):
        """Start the monitor"""
        try:
            self.monitor_loop()
        except KeyboardInterrupt:
            pass
        finally:
            self.log("Shutting down...")
            self.running = False
            
    def reset_board_list(self):
        """Clear the list of flashed boards"""
        with self.lock:
            count = len(self.flashed_boards)
            self.flashed_boards.clear()
            self.log(f"Cleared {count} boards from memory")

def main():
    parser = argparse.ArgumentParser(description="Multi-board flash utility for DeskHog")
    parser.add_argument("-v", "--verbose", action="store_true", help="Show detailed output")
    parser.add_argument("-f", "--firmware", help="Path to specific firmware file (optional)")
    parser.add_argument("-r", "--reset", action="store_true", help="Reset flashed board list and exit")
    
    args = parser.parse_args()
    
    flasher = BoardFlasher(firmware_path=args.firmware, verbose=args.verbose)
    
    if args.reset:
        flasher.reset_board_list()
        return
        
    flasher.run()

if __name__ == "__main__":
    main()