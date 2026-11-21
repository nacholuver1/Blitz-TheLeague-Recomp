"""Find functions that touch the current-fiber slot (lwz rX,0x100(r13) then 0x164(rX)).

Usage: python find_fiber_funcs.py <default.bin>
"""
import struct
import sys

data = open(sys.argv[1], "rb").read()
BASE = 0x82000000

pdata = []
p = 0x82120E00 - BASE
for i in range(0x30A00 // 8):
    b, d = struct.unpack_from(">II", data, p + i * 8)
    if b == 0:
        break
    pdata.append((b, ((d >> 8) & 0x3FFFFF) * 4))


def word(a):
    return struct.unpack_from(">I", data, a - BASE)[0]


def func_of(a):
    for b, s in pdata:
        if b <= a < b + s:
            return b, s
    return None


hits = {}
for a in range(0x82160000, 0x827AA3AC, 4):
    w = word(a)
    # lwz rD, 0x100(r13)
    if (w >> 26) == 32 and ((w >> 16) & 31) == 13 and (w & 0xFFFF) == 0x100:
        rd = (w >> 21) & 31
        for k in range(1, 8):
            w2 = word(a + 4 * k)
            op = w2 >> 26
            if op in (32, 36) and ((w2 >> 16) & 31) == rd and (w2 & 0xFFFF) == 0x164:
                f = func_of(a)
                key = f[0] if f else a
                hits.setdefault(key, []).append((a + 4 * k, "lwz" if op == 32 else "stw"))
                break

for fn, uses in sorted(hits.items()):
    f = func_of(fn)
    size = f[1] if f else 0
    print(f"func 0x{fn:08X} size=0x{size:X}: " + ", ".join(f"{k}@0x{x:08X}" for x, k in uses))
