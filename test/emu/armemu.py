#!/usr/bin/env python3
"""Run code built for the Nokia 9300 (GCC 2.9 armi -O2, as the phone's) in the Unicorn ARM emulator:
instruction counts per call and per function, the hottest basic blocks, an 8 KB data-cache model,
and memory faults with the function they happened in. From Maps 9300 (test/arm/*.py there use the
same pattern). Build the image with build_emu.sh; then:

    from armemu import Emu
    e = Emu("emu.exe", "emu.sym", cache=False)
    e.call("emu_init")
    e.write(0x2000000, data)
    r, n = e.call("my_func", 0x2000000, len(data))     # r0 back, instructions run
    e.profile(top=10)                                   # per function, of the last call
    e.hot(top=10)                                       # basic blocks
    print(e.misses())                                   # with cache=True: read misses by function

Memory: 64 MB from 0x1000; the image at its base (0x10000 by build_emu.sh); the stack at 0x300000
down; the driver's heap (drv_template.cpp) at 0x400000 up; put your data from 0x2000000.
pip install unicorn"""
import struct, bisect, collections, hashlib
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_BLOCK, UC_HOOK_MEM_READ, UcError
from unicorn.arm_const import UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC

RET, STACK = 0x2000, 0x300000

class Emu:
    def __init__(self, exe, symf, cache=False, mem=0x8000000):
        data = open(exe, "rb").read()
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        nsec = struct.unpack_from("<H", data, pe + 6)[0]
        optsz = struct.unpack_from("<H", data, pe + 20)[0]
        base = struct.unpack_from("<I", data, pe + 24 + 28)[0]
        sec = pe + 24 + optsz
        self.uc = uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        uc.mem_map(0x1000, mem - 0x1000)
        for i in range(nsec):
            name, vsize, va, rawsz, rawptr = struct.unpack_from("<8sIIII", data, sec + i * 40)
            if rawsz and not name.startswith(b".stab"): uc.mem_write(base + va, data[rawptr:rawptr + rawsz])
        self.syms, fn = {}, {}
        for line in open(symf):
            p = line.split()
            if len(p) == 3:
                self.syms[p[2]] = int(p[0], 16)
                if p[1] in "Tt" and not p[2].startswith(".L"): fn[int(p[0], 16)] = p[2]
        self.fn, self.addrs = fn, sorted(fn)
        self.blocks = collections.Counter()
        uc.hook_add(UC_HOOK_BLOCK, lambda u, a, s, d: self.blocks.__setitem__((a, s), self.blocks[(a, s)] + 1))
        self.cache = cache
        if cache:
            # the ARM925T's: 8 KB, here 2-way, 16-byte lines, reads allocate (the misses: memory stalls
            # the instruction counts don't show)
            self.LINE, self.SETS = 16, 8192 // 16 // 2
            self.tags = [[-1, -1] for _ in range(self.SETS)]
            self.missby = collections.Counter()
            self.reads = 0
            uc.hook_add(UC_HOOK_MEM_READ, self._mem)

    def func_at(self, a):
        return self.fn[self.addrs[bisect.bisect_right(self.addrs, a) - 1]]

    def _mem(self, uc, access, addr, size, value, data):
        ln = addr // self.LINE
        st = self.tags[ln % self.SETS]
        self.reads += 1
        if st[0] == ln: return
        if st[1] == ln: st[0], st[1] = st[1], st[0]; return
        st[1] = st[0]; st[0] = ln
        self.missby[self.func_at(uc.reg_read(UC_ARM_REG_PC))] += 1

    def write(self, addr, b): self.uc.mem_write(addr, bytes(b))
    def read(self, addr, n): return bytes(self.uc.mem_read(addr, n))
    def md5(self, addr, n): return hashlib.md5(self.read(addr, n)).hexdigest()[:8]

    def call(self, name, *args):
        """Calls a function (up to 4 int arguments); returns (r0, instructions)."""
        uc = self.uc
        for r, v in zip([UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3], args): uc.reg_write(r, v & 0xffffffff)
        uc.reg_write(UC_ARM_REG_SP, STACK); uc.reg_write(UC_ARM_REG_LR, RET)
        self.blocks.clear()
        if self.cache: self.missby.clear(); self.reads = 0
        try: uc.emu_start(self.syms[name], RET)
        except UcError as e:
            pc = uc.reg_read(UC_ARM_REG_PC)
            a = self.addrs[bisect.bisect_right(self.addrs, pc) - 1]
            raise RuntimeError("FAULT in %s: %s at 0x%x (%s+0x%x)" % (name, e, pc, self.fn[a], pc - a))
        return uc.reg_read(UC_ARM_REG_R0), sum(s // 4 * c for (a, s), c in self.blocks.items())

    def profile(self, top=10, show=True):
        per = collections.Counter()
        for (a, s), c in self.blocks.items(): per[self.func_at(a)] += s // 4 * c
        tot = sum(per.values()) or 1
        if show:
            for f, n in per.most_common(top): print("%10d %5.1f%%  %s" % (n, 100.0 * n / tot, f))
        return per

    def hot(self, top=10, func=None):
        rows = sorted(((s // 4 * c, a, s, c) for (a, s), c in self.blocks.items() if func is None or self.func_at(a) == func), reverse=True)[:top]
        for n, a, s, c in rows:
            f = self.addrs[bisect.bisect_right(self.addrs, a) - 1]
            print("  0x%x %s+0x%x: %d instr x %d = %d" % (a, self.fn[f], a - f, s // 4, c, n))

    def misses(self):
        return (self.reads, sum(self.missby.values()), self.missby.most_common(8)) if self.cache else None
