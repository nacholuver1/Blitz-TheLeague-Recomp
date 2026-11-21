"""Launch blitz.exe, wait, then read big-endian guest memory through lldb.

Guest address A lives at host 0x100000000 + A.
Usage: python guestpeek.py [--wait 12] [--config RelWithDebInfo]
"""
import argparse
import pathlib
import re
import subprocess
import time

LLDB = r"C:\Program Files\LLVM\bin\lldb.exe"
GUEST_BASE = 0x100000000

ap = argparse.ArgumentParser()
ap.add_argument("--wait", type=int, default=12)
ap.add_argument("--config", default="RelWithDebInfo")
args = ap.parse_args()

exe_dir = str(pathlib.Path(__file__).resolve().parents[1] / "blitz" / "out" / "build" / "win-amd64" / args.config)
proc = subprocess.Popen([rf"{exe_dir}\blitz.exe"],
                        cwd=exe_dir, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(args.wait)


def read_guest(addr, count):
    out = subprocess.run(
        [LLDB, "-b", "-p", str(proc.pid),
         "-o", f"memory read -s1 -c {count} -f x 0x{GUEST_BASE + addr:X}",
         "-o", "detach", "-o", "quit"],
        capture_output=True, text=True, errors="replace").stdout
    data = bytearray()
    for line in out.splitlines():
        if re.match(r"^0x[0-9a-f]+:", line):
            data += bytes(int(b, 16) for b in re.findall(r"0x([0-9a-f]{2})\b", line.split(":", 1)[1]))
    return bytes(data[:count])


def be32(b, o):
    return int.from_bytes(b[o:o + 4], "big")


try:
    g = read_guest(0x82D34FE0, 16)
    obj = be32(g, 4)
    print(f"[0x82D34FE4] object      = 0x{obj:08X}")
    print(f"[0x82D34FE8] callback    = 0x{be32(g, 8):08X}")
    special = be32(read_guest(0x828525C8, 4), 0)
    print(f"[0x828525C8] special tid = 0x{special:08X}")
    if obj:
        o = read_guest(obj, 0x48)
        for i in range(0, len(o), 4):
            print(f"  obj+0x{i:02X} = 0x{be32(o, i):08X}")
finally:
    proc.kill()
