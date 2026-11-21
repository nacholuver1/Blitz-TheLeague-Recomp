"""Find `bl`/`b` instructions targeting an address in a flat XEX image dump.

Usage: python find_calls.py <default.bin> <target> [text_start text_end]
"""
import struct
import sys

data = open(sys.argv[1], "rb").read()
base = 0x82000000
target = int(sys.argv[2], 0)
start = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x82160000
end = int(sys.argv[4], 0) if len(sys.argv) > 4 else 0x827B0854

for addr in range(start, end, 4):
    w = struct.unpack_from(">I", data, addr - base)[0]
    if (w >> 26) != 18 or (w & 2):  # I-form branch, relative only
        continue
    off = w & 0x03FFFFFC
    if off & 0x02000000:
        off -= 0x04000000
    if addr + off == target:
        print(f"0x{addr:08X} {'bl' if w & 1 else 'b '} 0x{target:08X}")
