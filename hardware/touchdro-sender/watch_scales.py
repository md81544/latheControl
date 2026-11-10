#!/usr/bin/env python3
"""Watch the TouchDRO sender's serial output and show which scale moves.

The sender writes a continuous stream such as "X250383;Y-553984;Z131;" with no
line breaks, which is hard to read in a serial monitor. This prints a line
only when a reading changes, naming the axis that moved and by how much, so
you can wind one axis at a time and see which letter it is and which way it
counts.

Needs pyserial (pip install pyserial). Close any other serial monitor first,
as only one program can have the port open.

    python3 watch_scales.py /dev/ttyUSB0      (Linux; COM3 etc. on Windows,
                                               /dev/tty.usbserial-... on a Mac)
"""

import argparse
import re
import sys

import serial

TOKEN = re.compile(r"([XYZWTP])(-?\d+);")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("port", help="serial port, e.g. /dev/ttyUSB0 or COM3")
    parser.add_argument("--baud", type=int, default=9600)
    parser.add_argument(
        "--counts-per-mm",
        type=float,
        default=2560 / 25.4,
        help="scale counts per mm (default: 2560 per inch, the usual iGaging figure)",
    )
    args = parser.parse_args()

    latest = {}
    buffer = ""
    with serial.Serial(args.port, args.baud, timeout=1) as port:
        print(f"Listening on {args.port} at {args.baud} baud. Ctrl+C to stop.")
        while True:
            chunk = port.read(64).decode("ascii", errors="replace")
            if not chunk:
                print("(no data for a second)")
                continue
            buffer += chunk
            # Keep any incomplete token at the end for the next read.
            last = buffer.rfind(";")
            if last < 0:
                continue
            complete, buffer = buffer[: last + 1], buffer[last + 1 :]
            for axis, text in TOKEN.findall(complete):
                value = int(text)
                previous = latest.get(axis)
                latest[axis] = value
                if previous is None:
                    print(f"{axis} first reading {value}")
                elif value != previous:
                    change = value - previous
                    print(
                        f"{axis} moved {change:+d} counts "
                        f"({change / args.counts_per_mm:+.3f} mm), now {value}"
                    )


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
