"""Screenshot of the 05-powerwall-display screen over serial (the firmware's "x" command).

    python screendump.py COM9 out.png [--screen N] [--settle SECONDS]

The device prints "#shot W H rgb565le native rot R baud B" at 115200, switches to baud B, sends one
"#px <row> <base64>" line per framebuffer row (native panel orientation), then "#shot end crc <hex>"
and switches back. Other log lines may interleave; only "#px" lines are used. The image is rotated
to what you see (rotation 3: logical (x, y) = framebuffer row W-1-x, column y) and saved as PNG
without third-party imaging libraries. Needs pyserial (PlatformIO's Python has it).
"""
import argparse
import base64
import re
import struct
import sys
import time
import zlib

import serial


def png_bytes(w, h, rgb):
    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + bytes(rgb[y * w * 3:(y + 1) * w * 3]) for y in range(h))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def read_line(ser, deadline):
    buf = b""
    while time.time() < deadline:
        c = ser.read(1)
        if not c:
            continue
        if c == b"\n":
            return buf.decode("ascii", "replace").rstrip("\r")
        buf += c
    raise TimeoutError("no line from device")


def command(ser, cmd, timeout=10):
    ser.reset_input_buffer()
    ser.write((cmd + "\n").encode())
    deadline = time.time() + timeout
    while True:
        line = read_line(ser, deadline)
        if "#ok" in line or "#err" in line:
            return line


def shot(ser):
    ser.reset_input_buffer()
    ser.write(b"x\n")
    deadline = time.time() + 10
    while True:
        line = read_line(ser, deadline)
        if line.startswith("#shot "):
            break
    parts = line.split()
    # "#shot W H rgb565le native rot R baud B"
    w, h, rot, baud = int(parts[1]), int(parts[2]), int(parts[parts.index("rot") + 1]), int(parts[parts.index("baud") + 1])
    time.sleep(0.1)
    ser.baudrate = baud
    rows = {}
    crc_dev = None
    deadline = time.time() + 60
    while True:
        line = read_line(ser, deadline)
        if line.startswith("#px "):
            _, r, b64 = line.split(" ", 2)
            try:
                data = base64.b64decode(b64, validate=True)
            except ValueError:
                continue
            if len(data) == w * 2:
                rows[int(r)] = data
        elif line.startswith("#shot end"):
            crc_dev = int(line.split()[-1], 16)
            break
    time.sleep(0.4)
    ser.baudrate = 115200
    ser.reset_input_buffer()
    missing = [r for r in range(h) if r not in rows]
    fb = b"".join(rows.get(r, b"\x00" * w * 2) for r in range(h))
    crc_ok = (zlib.crc32(fb) & 0xFFFFFFFF) == crc_dev
    return w, h, rot, fb, missing, crc_ok


def to_rgb(w, h, rot, fb):
    px = struct.unpack("<%dH" % (w * h), fb)
    rgb = bytearray(w * h * 3)
    for y in range(h):
        for x in range(w):
            if rot == 3:
                v = px[(w - 1 - x) * w + y]
            elif rot == 1:
                v = px[x * w + (h - 1 - y)]
            elif rot == 2:
                v = px[(h - 1 - y) * w + (w - 1 - x)]
            else:
                v = px[y * w + x]
            i = (y * w + x) * 3
            rgb[i] = ((v >> 11) & 0x1F) * 255 // 31
            rgb[i + 1] = ((v >> 5) & 0x3F) * 255 // 63
            rgb[i + 2] = (v & 0x1F) * 255 // 31
    return rgb


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port")
    ap.add_argument("out", help="PNG path; with --all, a prefix (out-<n>-<name>.png)")
    ap.add_argument("--screen", type=int, help="switch to this screen first (s <n>)")
    ap.add_argument("--all", action="store_true", help="capture every screen (uses l and s <n>)")
    ap.add_argument("--privacy", action="store_true", help="placeholder Wi-Fi name and town while capturing (v 1 / v 0)")
    ap.add_argument("--settle", type=float, default=2.0, help="seconds to wait after switching screens")
    a = ap.parse_args()

    # Set DTR/RTS low before opening: the CH340 auto-reset circuit would otherwise reset the board
    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = a.port, 115200, 0.2
    ser.dtr = False
    ser.rts = False
    ser.open()
    if a.privacy:
        print(command(ser, "v 1"))
    targets = []
    if a.all:
        listing = command(ser, "l")  # "screens: 0*house, 1 today, ... #ok" (* = current)
        targets = [(int(n), name) for n, name in re.findall(r"(\d+)[ *](\w+)", listing)]
    elif a.screen is not None:
        targets = [(a.screen, None)]
    else:
        targets = [(None, None)]

    for n, name in targets:
        if n is not None:
            print(command(ser, "s %d" % n))
            time.sleep(a.settle)
        w, h, rot, fb, missing, crc_ok = shot(ser)
        out = a.out if not a.all else "%s-%d-%s.png" % (a.out, n, name)
        with open(out, "wb") as f:
            f.write(png_bytes(w, h, to_rgb(w, h, rot, fb)))
        print("%s: %dx%d, %d missing rows, crc %s" % (out, w, h, len(missing), "ok" if crc_ok else "MISMATCH"))
    if a.privacy:
        print(command(ser, "v 0"))
    ser.close()


if __name__ == "__main__":
    sys.exit(main())
