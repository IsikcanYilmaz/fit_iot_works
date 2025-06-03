#!/usr/bin/env python3

import sys
import threading
import serial
import argparse
import itertools
import re
import time
from datetime import datetime
from collections import defaultdict

# ANSI color codes for prefixes
COLOR_CODES = [
    '\033[91m',  # Red
    '\033[92m',  # Green
    '\033[93m',  # Yellow
    '\033[94m',  # Blue
    '\033[95m',  # Magenta
    '\033[96m',  # Cyan
    '\033[97m',  # White
]
RESET_COLOR = '\033[0m'

# a reply is considered "complete" when this timeout is reached.
INACTIVITY_TIMEOUT = 0.1  # 100ms

def timestamp():
    return datetime.now().strftime("[%H:%M:%S]")


def expand_devices(device_patterns):
    """Expands patterns like /dev/ttyUSB[0-2] or /dev/ttyUSB[1,3]"""
    result = []
    pattern = re.compile(r"(.*)\[(.*?)\](.*)")

    for item in device_patterns:
        match = pattern.match(item)
        if match:
            prefix, body, suffix = match.groups()
            parts = body.split(',')

            for part in parts:
                if '-' in part:
                    start, end = map(int, part.split('-'))
                    result.extend([f"{prefix}{i}{suffix}" for i in range(start, end + 1)])
                else:
                    result.append(f"{prefix}{part}{suffix}")
        else:
            result.append(item)
    return result


def device_reader(dev, ser, output_queue):
    """Buffer serial input, flush after input is "complete" (reached set timeout)"""
    buffer = []
    last_received = time.time()

    while True:
        try:
            line = ser.readline()
            if line:
                buffer.append(line.decode(errors='replace').rstrip('\r\n'))
                last_received = time.time()
            elif buffer and (time.time() - last_received) >= INACTIVITY_TIMEOUT:
                # Flush buffered lines together
                output_queue[dev].append('\n'.join(buffer))
                buffer.clear()
        except Exception as e:
            output_queue[dev].append(f"ERROR: {e}")
            break


def output_flusher(output_queue, colors):
    """Flush output_queue continuously in round-robin order."""
    while True:
        any_output = False
        for dev in list(output_queue.keys()):
            while output_queue[dev]:
                block = output_queue[dev].pop(0)
                for line in block.splitlines():
                    print(f"{timestamp()} {colors[dev]}[{dev}]{RESET_COLOR} {line}")
                any_output = True
        if not any_output:
            time.sleep(0.01)

def main(devices, baudrate):
    serial_ports = {}
    color_cycle = itertools.cycle(COLOR_CODES)
    output_queue = defaultdict(list)
    colors = {}

    # get the device names from the "compact" input, e.g. /dev/ttyUSB[0-2] will be expanded to /dev/ttyUSB0, /dev/ttyUSB1, /dev/ttyUSB2
    expanded_devices = expand_devices(devices)

    for dev in expanded_devices:
        try:
            ser = serial.Serial(dev, baudrate=baudrate, timeout=0.05)
            serial_ports[dev] = ser
            colors[dev] = next(color_cycle)
        except Exception as e:
            print(f"Failed to open {dev}: {e}", file=sys.stderr)

    if not serial_ports:
        print("No valid serial ports opened.")
        sys.exit(1)

    # Start reader threads
    for dev, ser in serial_ports.items():
        t = threading.Thread(target=device_reader, args=(dev, ser, output_queue), daemon=True)
        t.start()

    # Start output flusher
    flusher_thread = threading.Thread(target=output_flusher, args=(output_queue, colors), daemon=True)
    flusher_thread.start()

    # Main loop: user input
    try:
        while True:
            try:
                line = input()
            except EOFError:
                break
            if not line.strip():
                continue
            for dev, ser in serial_ports.items():
                ser.write((line + '\n').encode())
            
            print(f"{timestamp()} {RESET_COLOR} |-> {line}")
    except KeyboardInterrupt:
        print("\nExiting...")
    finally:
        for ser in serial_ports.values():
            ser.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Serial Aggregator - Transmit to & Receive from multiple serial devices at the same time!")
    parser.add_argument('devices', nargs='+', help='Serial device paths (e.g., /dev/ttyUSB[0-2], /dev/ttyUSB[1,3])')
    parser.add_argument('-b', '--baud', type=int, default=115200, help='Baud rate (default: 115200)')

    args = parser.parse_args()

    main(args.devices, args.baud)
