"""Find dcbz / dcbz128 uses and the stride of the loop around each one.

dcbz   = 31/1014 with RT=0; dcbz128 (dcbzl) = same opcode with RT=1.
For each hit, look a few instructions after for `addi rX,rX,imm` on the EA base
register, which reveals the step the loop expects one dcbz to clear.

Usage: python scan_dcbz.py <default.bin>
"""
import collections
import struct
import sys

data = open(sys.argv[1], "rb").read()
BASE = 0x82000000
TEXT = (0x82160000, 0x827B0854)


def word(a):
    return struct.unpack_from(">I", data, a - BASE)[0]


kinds = collections.Counter()
strides = collections.defaultdict(collections.Counter)
examples = collections.defaultdict(list)
for a in range(TEXT[0], TEXT[1], 4):
    w = word(a)
    if (w >> 26) != 31 or ((w >> 1) & 0x3FF) != 1014:
        continue
    rt = (w >> 21) & 31
    ra = (w >> 16) & 31
    rb = (w >> 11) & 31
    kind = "dcbz128" if rt == 1 else "dcbz"
    kinds[kind] += 1
    # Find the stride: addi on rA or rB within the next/previous 8 instructions.
    stride = None
    for k in list(range(1, 9)) + list(range(-1, -9, -1)):
        x = word(a + 4 * k)
        if (x >> 26) == 14:  # addi rD,rA,SIMM
            rd, rs, imm = (x >> 21) & 31, (x >> 16) & 31, x & 0xFFFF
            if imm & 0x8000:
                imm -= 0x10000
            if rd == rs and rd in (ra, rb) and rd != 0 and imm > 0:
                stride = imm
                break
    strides[kind][stride] += 1
    if len(examples[(kind, stride)]) < 4:
        examples[(kind, stride)].append(a)

for kind, n in kinds.items():
    print(f"{kind}: {n} uses")
    for stride, c in strides[kind].most_common():
        ex = ", ".join(f"0x{e:08X}" for e in examples[(kind, stride)])
        print(f"   loop stride {stride}: {c}   e.g. {ex}")
