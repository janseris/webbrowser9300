#!/usr/bin/env python3
"""A page's scripts in the emulated phone build: python3 run_page.py page.html -> the page after them,
with the counts and notes; compared with the PC's (test/pagetest.c) output if given as the second argument."""
import sys, os, struct
sys.path.insert(0, os.path.expanduser("~/jseng/emu"))
from armemu import Emu
here = os.path.dirname(os.path.abspath(__file__)); os.chdir(here)
e = Emu("o/mj.exe", "o/mj.sym", mem=0x10000000)
e.call("emu_init")
J, _ = e.call("mj_new")
src = open(sys.argv[1], "rb").read()
e.write(0x2000000, src + b"\0")
pg, n = e.call("mj_page", J, 0x2000000, len(src))
raw = e.read(pg, 4 * 21)
v = struct.unpack("<21I", raw)
# html len href ua ref lang w h expired ctx out outLen say sayLen run failed skipped external written big stopped
out, outLen, say, sayLen = v[10], v[11], v[12], v[13]
run, failed, skipped, external, written = v[14:19]
stopped = v[20]
print("instructions %d: run %d failed %d skipped %d external %d written %d stopped %d" % (n, run, failed, skipped, external, written, stopped))
print(e.read(say, sayLen).decode("utf-8", "replace") if say else "", end="")
page = e.read(out, outLen) if out else src
if len(sys.argv) > 2:
    want = open(sys.argv[2], "rb").read()
    print("SAME AS THE PC" if page == want else "DIFFERENT FROM THE PC")
else:
    sys.stdout.write(page.decode("utf-8", "replace"))
