"""Locate __savegprlr/__restgprlr/fpr/vmx helpers in a flat XEX image dump.

Usage: python find_helpers.py <default.bin> [base]
"""
import struct
import sys

PATTERNS = {
    "restgprlr_14_address": bytes.fromhex("e9c1ff68"),
    "savegprlr_14_address": bytes.fromhex("f9c1ff68"),
    "restfpr_14_address": bytes.fromhex("c9ccff70"),
    "savefpr_14_address": bytes.fromhex("d9ccff70"),
    "restvmx_14_address": bytes.fromhex("3960fee07dcb60ce"),
    "savevmx_14_address": bytes.fromhex("3960fee07dcb61ce"),
    "restvmx_64_address": bytes.fromhex("3960fc00100b60cb"),
    "savevmx_64_address": bytes.fromhex("3960fc00100b61cb"),
}

data = open(sys.argv[1], "rb").read()
base = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x82000000


def find_all(pat):
    out, i = [], data.find(pat)
    while i != -1:
        if i % 4 == 0:
            out.append(i)
        i = data.find(pat, i + 1)
    return out


for name, pat in PATTERNS.items():
    hits = find_all(pat)
    # The real helper is a run of consecutive save/restore instructions, so
    # prefer hits followed by the r15 / f15 / v15 variant.
    good = []
    for h in hits:
        nxt = struct.unpack_from(">I", data, h + (8 if "vmx" in name else 4))[0]
        cur = struct.unpack_from(">I", data, h + (4 if "vmx" in name else 0))[0]
        if "vmx" in name:
            # next pair should be li r11, -0x110 / -0x3F0
            ok = (nxt >> 16) == 0x3960
        else:
            # same opcode, register number + 1
            ok = (nxt >> 26) == (cur >> 26) and ((nxt >> 21) & 31) == ((cur >> 21) & 31) + 1
        if ok:
            good.append(h)
    print(f"{name:22} " + (", ".join(f"0x{base + h:08X}" for h in good) or "NOT FOUND")
          + (f"   (raw hits: {len(hits)})" if len(hits) != len(good) else ""))
