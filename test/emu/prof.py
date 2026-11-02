#!/usr/bin/env python3
"""Parse + layout of a page with its stylesheets, as the phone's code, in the emulator:
   prof.py out page.html [a.css b.css ...]  -> instructions per call, per function, hot blocks"""
import sys, struct
from armemu import Emu
out, page, css = sys.argv[1], sys.argv[2], sys.argv[3:]
e = Emu(out + "/emu.exe", out + "/emu.sym", cache="-c" in sys.argv[0:1])
e.call("emu_init")
A = 0x2000000
pos = A + 0x1000
def put(b):
    global pos
    at = pos; e.write(at, b); pos += (len(b) + 16) & ~7; return at
html = open(page, "rb").read()
h = put(html + b"\0"); u = put(b"https://en.wikipedia.org/wiki/Nokia_9300\0"); c = put(b"text/html; charset=UTF-8\0")
cp, cl = [], []
for f in css:
    d = open(f, "rb").read(); cp.append(put(d)); cl.append(len(d))
ptrs = put(struct.pack("<%dI" % max(1, len(cp)), *(cp or [0]))); lens = put(struct.pack("<%dI" % max(1, len(cl)), *(cl or [0])))
e.write(A, struct.pack("<7I", h, len(html), u, c, len(cp), ptrs if cp else 0, lens if cp else 0))
doc, n = e.call("emu_parse", A)
print("parse: %d instructions (~%d ms at 17 ns), ops %d, rules %d" % (n, n * 17 // 1000000, e.call("emu_ops", doc)[0], e.call("emu_rules", doc)[0]))
e.call("emu_parse", A)
e.profile(top=18)
print("hot blocks:"); e.hot(top=12)
boxes, n = e.call("emu_layout", doc, 466)
print("layout: %d instructions (~%d ms), %d boxes" % (n, n * 17 // 1000000, boxes))
e.profile(top=10)
