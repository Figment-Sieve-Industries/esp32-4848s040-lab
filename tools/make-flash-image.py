#!/usr/bin/env python3
"""Merge a PlatformIO build into one flashable image (written at offset 0) plus an ESP Web Tools manifest.

Usage (after `pio run -d projects/NN-name`):
    python tools/make-flash-image.py <project-dir> <out-dir> <version> [--env esp32-4848s040] [--title "..."]

Writes <out-dir>/<project>.bin and <out-dir>/manifest-<project>.json. The manifest refers to the image by the
relative path "firmware/<project>.bin", so the .bin files must be published next to the page in a firmware/ folder
(see .github/workflows/release.yml). Needs `pip install esptool==5.0.2` (the version PlatformIO bundles; PlatformIO's own copy cannot be run with the
system Python) and takes boot_app0.bin from the Arduino framework package PlatformIO installed.

Layout for the 16 MB default_16MB.csv partition table on ESP32-S3: bootloader 0x0, partition table 0x8000,
boot_app0 (otadata) 0xe000, app 0x10000. The offsets are checked against the partition table that was built.
"""
import argparse
import json
import pathlib
import subprocess
import sys

PIO = pathlib.Path.home() / ".platformio" / "packages"
BOOT_APP0 = PIO / "framework-arduinoespressif32" / "tools" / "partitions" / "boot_app0.bin"

BOOTLOADER_OFFSET = 0x0  # ESP32-S3
PARTITIONS_OFFSET = 0x8000
BOOT_APP0_OFFSET = 0xE000
APP_OFFSET = 0x10000


def first_app_offset(partitions_bin: pathlib.Path) -> int:
    """Offset of the first app partition in a compiled partition table (32-byte entries, magic 0xAA 0x50)."""
    data = partitions_bin.read_bytes()
    for i in range(0, len(data) - 32, 32):
        e = data[i : i + 32]
        if e[0:2] == b"\xaa\x50" and e[2] == 0x00:  # type 0 = app
            return int.from_bytes(e[4:8], "little")
    raise SystemExit("no app partition found in partitions.bin")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("project_dir", type=pathlib.Path)
    ap.add_argument("out_dir", type=pathlib.Path)
    ap.add_argument("version")
    ap.add_argument("--env", default="esp32-4848s040")
    ap.add_argument("--title", default=None)
    a = ap.parse_args()

    name = a.project_dir.resolve().name
    build = a.project_dir / ".pio" / "build" / a.env
    parts = {
        BOOTLOADER_OFFSET: build / "bootloader.bin",
        PARTITIONS_OFFSET: build / "partitions.bin",
        BOOT_APP0_OFFSET: BOOT_APP0,
        APP_OFFSET: build / "firmware.bin",
    }
    for p in parts.values():
        if not p.is_file():
            raise SystemExit(f"missing {p}")
    if first_app_offset(parts[PARTITIONS_OFFSET]) != APP_OFFSET:
        raise SystemExit("partition table puts the app somewhere other than 0x10000; update this script")

    a.out_dir.mkdir(parents=True, exist_ok=True)
    out = a.out_dir / f"{name}.bin"
    cmd = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "merge_bin", "-o", str(out),
           "--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "16MB"]
    for off in sorted(parts):
        cmd += [hex(off), str(parts[off])]
    subprocess.run(cmd, check=True)
    subprocess.run([sys.executable, "-m", "esptool", "--chip", "esp32s3", "image_info", str(out)], check=True)

    manifest = {
        "name": a.title or name,
        "version": a.version,
        "new_install_prompt_erase": True,
        "builds": [{"chipFamily": "ESP32-S3", "parts": [{"path": f"firmware/{name}.bin", "offset": 0}]}],
    }
    mpath = a.out_dir / f"manifest-{name}.json"
    mpath.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {out} ({out.stat().st_size} bytes) and {mpath}")


if __name__ == "__main__":
    main()
