# Writes pc-check.txt to the root of a FAT-formatted SD card (or anywhere) for test "m" in sd_tests.cpp:
# 20000 lines of 42 bytes, line n = "PC %05d " + 32 chars derived from n, then "\n".
# Usage: python make_pc_check.py I:/
import sys

ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZ012345"

out = []
for n in range(20000):
    x = (n * 2654435761) & 0xFFFFFFFF
    tail = "".join(ALPHABET[(x >> (i % 27)) & 31] for i in range(32))
    out.append(f"PC {n:05d} {tail}\n")

dest = (sys.argv[1] if len(sys.argv) > 1 else ".").rstrip("/\\") + "/pc-check.txt"
with open(dest, "wb") as f:
    f.write("".join(out).encode("ascii"))
print(f"wrote {dest}")
