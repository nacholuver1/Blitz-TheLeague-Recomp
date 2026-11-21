"""Launch blitz.exe, wait, and run lldb probes against guest state.

Finds the host thread whose backtrace contains a given guest function, selects the
frame of that function, and prints guest registers from its PPC context.
Also dumps guest memory ranges (big-endian, guest address + 0x100000000).

Usage:
  python lldbprobe.py --thread-func sub_82637BA8 --regs r3,r30,r31 --mem 0x82D34F38:0x40
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
ap.add_argument("--thread-func", action="append", default=[])
ap.add_argument("--regs", default="r3,r4,r30,r31")
ap.add_argument("--mem", action="append", default=[])
ap.add_argument("--keep", action="store_true", help="leave the process running")
args = ap.parse_args()

exe_dir = str(pathlib.Path(__file__).resolve().parents[1] / "blitz" / "out" / "build" / "win-amd64" / args.config)
proc = subprocess.Popen([rf"{exe_dir}\blitz.exe"],
                        cwd=exe_dir, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(args.wait)


def lldb(*cmds):
    a = [LLDB, "-b", "-p", str(proc.pid)]
    for c in cmds:
        a += ["-o", c]
    a += ["-o", "detach", "-o", "quit"]
    return subprocess.run(a, capture_output=True, text=True, errors="replace").stdout


try:
    bt = lldb("thread backtrace all -c 60")
    for func in args.thread_func:
        hit = None
        for block in bt.split("  thread #")[1:]:
            tid = block.split()[0].rstrip(",:")
            for line in block.splitlines():
                m = re.search(r"frame #(\d+):.*`__imp__" + re.escape(func) + r"\b", line)
                if m:
                    hit = (tid, m.group(1))
                    break
            if hit:
                break
        if not hit:
            print(f"{func}: not on any stack")
            continue
        tid, frame = hit
        cmds = [f"thread select {tid}", f"frame select {frame}"]
        cmds += [f"expression -f x -- ctx.{r}.u32" for r in args.regs.split(",")]
        out = lldb(*cmds)
        vals = re.findall(r"\(rex::u32\) \$\d+ = (0x[0-9a-f]+)|\(unsigned int\) \$\d+ = (0x[0-9a-f]+)", out)
        vals = [a or b for a, b in vals]
        print(f"{func}: host thread #{tid} frame #{frame}")
        if not vals:
            print(out[-1200:])
        for r, v in zip(args.regs.split(","), vals):
            print(f"  {r} = {v}")
    for spec in args.mem:
        addr, size = (int(x, 0) for x in spec.split(":"))
        out = lldb(f"memory read -s1 -c {size} -f x 0x{GUEST_BASE + addr:X}")
        data = bytearray()
        for line in out.splitlines():
            if re.match(r"^0x[0-9a-f]+:", line):
                data += bytes(int(b, 16) for b in re.findall(r"0x([0-9a-f]{2})\b", line.split(":", 1)[1]))
        print(f"mem 0x{addr:08X}:")
        for i in range(0, len(data), 4):
            print(f"  +0x{i:02X} = 0x{int.from_bytes(data[i:i + 4], 'big'):08X}")
finally:
    if not args.keep:
        proc.kill()
