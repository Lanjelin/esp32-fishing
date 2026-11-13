#!/usr/bin/env python3
"""Capture on-device UI over serial. Requires pyserial; use while not playing."""
import argparse
import base64
import struct
import sys
import time
import zlib
import serial


def chunk(kind, data):
    return struct.pack('!I', len(data)) + kind + data + struct.pack('!I', zlib.crc32(kind + data))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', default='ui.png', help='PNG file, or - for stdout')
    parser.add_argument('--view', choices=['current', 'desk-timer'], default='current')
    args = parser.parse_args()
    port = serial.Serial(port=None, baudrate=115200, timeout=1)
    port.dtr = False
    port.rts = False
    port.port = args.port
    with port:
        # Some USB bridges reset on open. Let firmware and BLE finish starting.
        time.sleep(5)
        port.reset_input_buffer()
        port.write(b't' if args.view == 'desk-timer' else b's')
        rows = {}
        end = time.monotonic() + 45
        while time.monotonic() < end:
            line = port.readline().strip()
            if line.startswith(b'SHOT_ERROR'):
                raise RuntimeError(line.decode())
            if line.startswith(b'SHOT_ROW '):
                _, number, encoded = line.split(b' ', 2)
                data = base64.b64decode(encoded, validate=True)
                if len(data) != 720:
                    raise RuntimeError('Invalid screenshot row length')
                rows[int(number)] = data
            if line == b'SHOT_END':
                break
    if set(rows) != set(range(135)):
        raise RuntimeError(f'Incomplete screenshot: {len(rows)}/135 rows')
    raw = b''.join(b'\0' + rows[y] for y in range(135))
    png = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('!2I5B', 240, 135, 8, 2, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))
    if args.output == '-':
        sys.stdout.buffer.write(png)
    else:
        with open(args.output, 'wb') as output:
            output.write(png)


if __name__ == '__main__':
    main()
