"""Drive 06-sd-card-test over the CH340 serial port and save a log.

Run with PlatformIO's Python (it has pyserial), from PowerShell:
  & $env:USERPROFILE\.platformio\penv\Scripts\python.exe sd_serial.py COM9 --reset i t s soak:120 i

Steps run in order:
  t / s / r / i / h  send that command and wait for its "#done <c>" line
  soak:N             start the soak, watch it for N seconds, stop it
  wait:N             just listen for N seconds (e.g. while you pull and re-insert the card)
--reset pulses EN first and captures the boot log. Without it the port opens without resetting the board.
Everything is echoed and written to sd_serial_<timestamp>.log next to this script.
"""
import argparse
import datetime
import os
import sys
import time

import serial

TIMEOUTS = {"t": 180, "s": 240, "r": 30, "i": 10, "h": 5, "k": 10}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("steps", nargs="*", default=["i"])
    ap.add_argument("--reset", action="store_true")
    a = ap.parse_intermixed_args()

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    log_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), f"sd_serial_{stamp}.log")
    log = open(log_path, "w", encoding="utf-8")

    ser = serial.Serial()
    ser.port = a.port
    ser.baudrate = 115200
    ser.timeout = 0.2
    ser.dtr = False  # IO0 high: never enter the bootloader
    ser.rts = False  # EN high: don't reset on open
    ser.open()

    buf = b""

    def lines(until=None, seconds=10.0):
        """Echo lines for up to `seconds`; return True as soon as a line starts with `until`."""
        nonlocal buf
        end = time.time() + seconds
        while time.time() < end:
            buf += ser.read(512)
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                text = raw.decode("utf-8", "replace").rstrip("\r")
                print(text, flush=True)
                log.write(text + "\n")
                log.flush()
                if until and text.startswith(until):
                    return True
        return False

    if a.reset:
        ser.rts = True
        time.sleep(0.1)
        ser.rts = False
        lines(seconds=12)  # boot + mount attempts
    else:
        lines(seconds=1)

    failed = False
    for step in a.steps:
        if step.startswith("soak:"):
            secs = int(step.split(":")[1])
            ser.write(b"k")
            ok = lines("#done k", TIMEOUTS["k"])
            lines(seconds=secs)
            ser.write(b"k")
            ok = lines("#done k", TIMEOUTS["k"]) and ok
        elif step.startswith("wait:"):
            lines(seconds=int(step.split(":")[1]))
            continue
        else:
            ser.write(step.encode())
            ok = lines(f"#done {step}", TIMEOUTS.get(step, 30))
        if not ok:
            print(f"!! step {step}: no #done marker before the timeout", flush=True)
            log.write(f"!! step {step}: timeout\n")
            failed = True

    ser.close()
    log.close()
    print(f"log: {log_path}")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
