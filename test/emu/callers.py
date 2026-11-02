#!/usr/bin/env python3
"""callers.py out page.html css... FUNC: who calls FUNC how often (by lr at its entry)"""
import sys, struct, collections, bisect
from unicorn import UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_LR
from armemu import Emu
out, page, func, css = sys.argv[1], sys.argv[2], sys.argv[-1], sys.argv[3:-1]
e = Emu(out + "/emu.exe", out + "/emu.sym")
e.call("emu_init")
A = 0x2000000; pos = [A + 0x1000]
def put(b):
    at = pos[0]; e.write(at, b); pos[0] += (len(b) + 16) & ~7; return at
html = open(page, "rb").read()
h = put(html + b"\0"); u = put(b"https://en.wikipedia.org/wiki/x\0"); c = put(b"text/html; charset=UTF-8\0")
cp = [put(open(f, "rb").read()) for f in css]; cl = [len(open(f, "rb").read()) for f in css]
ptrs = put(struct.pack("<%dI" % len(cp), *cp)); lens = put(struct.pack("<%dI" % len(cl), *cl))
e.write(A, struct.pack("<7I", h, len(html), u, c, len(cp), ptrs, lens))
at = e.syms[func]; cnt = collections.Counter()
e.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, d: cnt.__setitem__(uc.reg_read(UC_ARM_REG_LR), cnt[uc.reg_read(UC_ARM_REG_LR)] + 1), begin=at, end=at)
e.call("emu_parse", A)
for lr, n in cnt.most_common(8):
    f = e.addrs[bisect.bisect_right(e.addrs, lr) - 1]
    print("%8d  from %s+0x%x" % (n, e.fn[f], lr - f))
