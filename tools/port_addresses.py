"""Find functions from one Xbox 360 title in another by instruction pattern.

Titles built with the same SDK libraries (CRT, XAPI) or the same engine share
functions at different addresses. This compares whole functions, taken from
.pdata, with the parts that move between builds masked out: branch targets
and address immediates (lis/addi/loads/stores off non-stack registers).

Usage:
  python port_addresses.py <old.bin> <new.bin> name=0xADDR [name=0xADDR ...]
  python port_addresses.py <old.bin> <new.bin> --self-test name=0xADDR ...

<old.bin>/<new.bin> are flat image dumps (see tools/xexdump). Prints, per
function, exact matches, or else the closest functions by similarity.
"""
import struct
import sys

BASE = 0x82000000


def sections(d):
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    opt = struct.unpack_from("<H", d, pe + 20)[0]
    out = {}
    for i in range(nsec):
        o = pe + 24 + opt + i * 40
        name = d[o:o + 8].rstrip(b"\0").decode("ascii", "replace")
        vsize, va = struct.unpack_from("<II", d, o + 8)
        out[name] = (va, vsize)
    return out


def functions(d):
    """{start address: length in bytes} from .pdata."""
    va, size = sections(d)[".pdata"]
    funcs = {}
    for i in range(size // 8):
        begin, packed = struct.unpack_from(">II", d, va + i * 8)
        if begin == 0:
            continue
        length = ((packed >> 8) & 0x3FFFFF) * 4
        if length:
            funcs[begin] = length
    return funcs


def mask(w):
    op = w >> 26
    ra = (w >> 16) & 0x1F
    if op == 18:  # b / bl
        return w & 0xFC000003
    if op == 16:  # bc
        return w & 0xFFFF0003
    if op in (14, 15, 24, 25):  # addi, addis/lis, ori, oris: may build an address
        return w & 0xFFFF0000
    if 32 <= op <= 55 and ra not in (1,):  # loads/stores not off the stack pointer
        return w & 0xFFFF0000
    if op in (58, 62) and ra != 1:  # ld / std
        return w & 0xFFFF0000
    return w


def words(d, addr, length):
    off = addr - BASE
    n = length // 4
    return [mask(w) for w in struct.unpack_from(f">{n}I", d, off)]


def similarity(a, b):
    if len(a) != len(b):
        return 0.0
    return sum(x == y for x, y in zip(a, b)) / len(a)


def main():
    args = sys.argv[1:]
    if len(args) < 3:
        print(__doc__)
        return 1
    old = open(args[0], "rb").read()
    new = open(args[1], "rb").read()
    self_test = "--self-test" in args
    targets = [a for a in args[2:] if "=" in a]
    old_funcs = functions(old)
    new_funcs = functions(new)
    by_len = {}
    for start, length in new_funcs.items():
        by_len.setdefault(length, []).append(start)
    ok = True
    for t in targets:
        name, addr = t.split("=")
        addr = int(addr, 0)
        length = old_funcs.get(addr)
        if not length:
            # Hand-written assembly (setjmp, SwitchToFiber...) has no .pdata
            # entry: match a fixed window anywhere in the code sections.
            window = 64
            pat = words(old, addr, window)
            hits = []
            for sec, (va, size) in sections(new).items():
                if sec not in (".text",) and not sec.startswith("PSFD"):
                    continue
                for a in range(BASE + va, BASE + va + size - window, 4):
                    if mask(struct.unpack_from(">I", new, a - BASE)[0]) == pat[0] and \
                            words(new, a, window) == pat:
                        hits.append(a)
            found = ", ".join(f"0x{s:08X}" for s in hits) or "none"
            tag = "unique" if len(hits) == 1 else f"{len(hits)} matches"
            print(f"{name:24} 0x{addr:08X} (no .pdata, {window}-byte window) -> {found}  [{tag}]")
            if self_test and addr not in hits:
                ok = False
            continue
        pat = words(old, addr, length)
        same = by_len.get(length, [])
        exact = [s for s in same if words(new, s, length) == pat]
        if exact:
            found = ", ".join(f"0x{s:08X}" for s in sorted(exact))
            tag = "unique" if len(exact) == 1 else f"{len(exact)} matches"
            print(f"{name:24} 0x{addr:08X} ({length:4} bytes) -> {found}  [{tag}]")
            if self_test and addr not in exact:
                ok = False
            continue
        # No exact match: closest functions of a similar length.
        scored = []
        for s, ln in new_funcs.items():
            if abs(ln - length) <= max(8, length // 10):
                b = words(new, s, min(ln, length))
                scored.append((similarity(pat[:len(b)], b), s, ln))
        scored.sort(reverse=True)
        best = ", ".join(f"0x{s:08X} ({ln}b, {sc:.0%})" for sc, s, ln in scored[:3])
        print(f"{name:24} 0x{addr:08X} ({length:4} bytes) -> no exact match; closest: {best}")
        ok = ok and not self_test
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
