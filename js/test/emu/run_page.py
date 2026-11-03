#!/usr/bin/env python3
"""A page's scripts in the emulated phone build (build.sh first):
python3 run_page.py page.html url [script-url=file ...] [--compare pc-output.html]
Prints the counts and notes, and the page after its scripts (or whether it is the PC's)."""
import sys, os, struct
sys.path.insert(0, os.path.expanduser("~/jseng/emu"))
from armemu import Emu
args = sys.argv[1:]
cmp = None
if '--compare' in args: k = args.index('--compare'); cmp = args[k + 1]; del args[k:k + 2]
page, url, scripts = args[0], args[1], [a.rsplit('=', 1) for a in args[2:]]
here = os.path.dirname(os.path.abspath(__file__))
src = open(page, "rb").read()
os.chdir(here)
e = Emu("o/mj.exe", "o/mj.sym", mem=0x10000000)
e.call("emu_init")
J, _ = e.call("mj_new")
A = 0x2000000
def put(b):
    global A
    a = A; e.write(a, b + b"\0"); A += (len(b) + 16) & ~7; return a
pa = put(src)
ents = [(put(u.encode()), put(open(f, 'rb').read()), os.path.getsize(f)) for u, f in scripts]
hrefa = put(url.encode())
tab = struct.pack("<I", len(ents)) + b"".join(struct.pack("<III", *t) for t in ents) + struct.pack("<I", hrefa)
ta = put(tab)
pg, n = e.call("mj_page", J, pa, len(src), ta)
# BwJsPage: html len href ua ref lang cookie w h expired ctx fetch fetchCtx out outLen say sayLen missing missingLen
#           navigate cookies cookiesLen run failed skipped external externalMissing written big timers nodes stopped
v = struct.unpack("<32I", e.read(pg, 4 * 32))
out, outLen, say, sayLen = v[13], v[14], v[15], v[16]
run, failed, skipped, external, extmiss, written, big, timers, nodes, stopped = v[22:32]
print("instructions %d: nodes %d run %d failed %d skipped %d external %d (missing %d) big %d written %d timers %d stopped %d changed %d" %
      (n, nodes, run, failed, skipped, external, extmiss, big, written, timers, stopped, out != 0))
print(e.read(say, sayLen).decode("utf-8", "replace") if say else "", end="")
res = e.read(out, outLen) if out else src
if cmp:
    want = open(cmp, "rb").read()
    print("SAME AS THE PC" if res == want else "DIFFERENT FROM THE PC")
    if res != want: open(page + ".emu", "wb").write(res)
else:
    sys.stdout.write(res.decode("utf-8", "replace"))
