#!/usr/bin/env python3
"""Runs every js/tests script in the emulated phone build (build.sh first) and compares the results
with js/tests/expected.txt (Node's, which the host build of this MuJS also gives). Prints the
instructions, the heap's peak and the C stack each script needed."""
import sys, os
sys.path.insert(0, os.path.expanduser("~/jseng/emu"))
from armemu import Emu, STACK
here = os.path.dirname(os.path.abspath(__file__))
tests = os.path.join(here, "..", "..", "tests")
os.chdir(here)
e = Emu("o/mj.exe", "o/mj.sym", mem=0x10000000)
e.call("emu_init")
exp = dict(l.rstrip("\n").split("\t", 1) for l in open(os.path.join(tests, "expected.txt"), encoding="utf-8"))
SRC, LOW = 0x2000000, 0x200000
def cstr(a):
    b = e.read(a, 4000); return b.split(b"\0")[0].decode("utf-8", "replace")
bad = 0
only = sys.argv[1:]
for name in sorted(f for f in os.listdir(tests) if f.endswith(".js") and (not only or any(o in f for o in only))):
    J, _ = e.call("mj_new")
    e.write(SRC, open(os.path.join(tests, name), "rb").read() + b"\0")
    e.write(LOW, b"\xa5" * (STACK - LOW))
    e.call("heap_reset_peak"); base = e.call("heap_live")[0]
    try:
        r, n = e.call("mj_run", J, SRC, 30 if "runaway" in name else 20000)
    except RuntimeError as x:
        print("%-16s %s" % (name, x)); bad += 1; continue
    st = e.read(LOW, STACK - LOW); used = STACK - LOW - next(i for i in range(len(st)) if st[i] != 0xa5)
    res = cstr(e.call("mj_result", J)[0])
    if r: res = ("compile: " if r == 1 else "error: ") + res
    peak = e.call("heap_peak")[0] - base
    e.call("mj_pop", J); e.call("mj_free", J)
    ok = exp.get(name) == res
    if not ok: bad += 1
    print("%-16s %4s %11d instr  heap %6d KB  stack %6d B  %s" % (name, "ok" if ok else "BAD", n, peak // 1024, used, res if ok else res + "\n    expected " + str(exp.get(name))))
print("all ok" if not bad else "%d BAD" % bad)
