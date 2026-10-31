#!/usr/bin/env python3
"""Runs the phone's ARM assembly helpers (core/igarm.c) in the Unicorn ARM emulator against Python
references, with random sizes and alignments and guard bytes around the buffers, and counts
instructions against GCC 2.9's build of the C versions (igutil.c).

  armtest.py igarm.dis igarm.sym cver.dis cver.sym     (objdump -d / nm of the two objects)
"""
import random, re, sys
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm_const import *

CODE, STACK, BUF = 0x10000, 0x80000, 0x100000
RET = 0xF000          # lr: a word we stop at


def load(dis, sym):
    words = {}
    for line in open(dis):
        m = re.match(r"\s*([0-9a-f]+):\s+([0-9a-f]{8})\s", line)
        if m: words[int(m.group(1), 16)] = int(m.group(2), 16)
    end = max(words) + 4
    code = bytearray(end)
    for a, w in words.items(): code[a:a + 4] = w.to_bytes(4, "little")
    syms = {}
    for line in open(sym):
        a, _, n = line.split()
        syms[n] = int(a, 16)
    return bytes(code), syms


class Cpu:
    def __init__(self, code, syms):
        self.uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        self.uc.mem_map(0, 0x200000)
        self.uc.mem_write(CODE, code)
        self.syms = syms
        self.count = 0
        def hook(uc, addr, size, ud): self.count += 1
        self.uc.hook_add(UC_HOOK_CODE, hook, begin=CODE, end=CODE + len(code))

    def call(self, name, *args):
        uc = self.uc
        regs = [UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2]
        for r, v in zip(regs, args): uc.reg_write(r, v)
        uc.reg_write(UC_ARM_REG_SP, STACK)
        uc.reg_write(UC_ARM_REG_LR, RET)
        # callee-saved registers must come back unchanged
        for r in range(4, 12): uc.reg_write(UC_ARM_REG_R0 + r, 0x1000 + r)
        before = self.count
        uc.emu_start(CODE + self.syms[name], RET)
        for r in range(4, 12):
            assert uc.reg_read(UC_ARM_REG_R0 + r) == 0x1000 + r, (name, "r%d clobbered" % r)
        assert uc.reg_read(UC_ARM_REG_SP) == STACK, (name, "sp")
        return uc.reg_read(UC_ARM_REG_R0), self.count - before


def check(asm, c, rnd):
    guard = 64
    for it in range(4000):
        n = rnd.choice([rnd.randrange(0, 20), rnd.randrange(0, 80), rnd.randrange(0, 600)])
        so, do = rnd.randrange(8), rnd.randrange(8)
        src = BUF + 0x1000 + so
        dst = BUF + 0x8000 + do
        data = bytes(rnd.randrange(256) for _ in range(n + 2 * guard))
        fill = bytes(rnd.randrange(256) for _ in range(n + 2 * guard))
        for cpu in (asm,):
            cpu.uc.mem_write(src - guard, data)
            cpu.uc.mem_write(dst - guard, fill)
            cpu.call("ig_memcpy", dst, src, n)
            got = bytes(cpu.uc.mem_read(dst - guard, n + 2 * guard))
            want = fill[:guard] + data[guard:guard + n] + fill[guard + n:]
            assert got == want, ("memcpy", n, so, do)
            assert bytes(cpu.uc.mem_read(src - guard, n + 2 * guard)) == data, "memcpy wrote the source"
            # memset
            v = rnd.randrange(-300, 300)
            cpu.uc.mem_write(dst - guard, fill)
            cpu.call("ig_memset", dst, v & 0xFFFFFFFF, n)
            got = bytes(cpu.uc.mem_read(dst - guard, n + 2 * guard))
            assert got == fill[:guard] + bytes([v & 255]) * n + fill[guard + n:], ("memset", n, do, v)
            # strlen
            s = bytes(rnd.randrange(1, 256) for _ in range(n)) + b"\0" + data[:8]
            cpu.uc.mem_write(src, s)
            r, _ = cpu.call("ig_strlen", src)
            assert r == n, ("strlen", n, so, r)
    r, _ = asm.call("ig_strlen", 0)
    assert r == 0, "strlen(NULL)"
    # a forward overlap (dst < src), as array removal does
    for n in range(0, 100):
        for shift in (1, 2, 3, 4, 5, 8, 33):
            base = bytes(rnd.randrange(256) for _ in range(200))
            asm.uc.mem_write(BUF, base)
            asm.call("ig_memcpy", BUF + 4, BUF + 4 + shift, n)
            ref = bytearray(base)
            for i in range(n): ref[4 + i] = ref[4 + shift + i]
            assert bytes(asm.uc.mem_read(BUF, 200)) == bytes(ref), ("overlap", n, shift)
    print("asm: 4000 random memcpy / memset / strlen runs and overlaps match")


def speed(asm, c):
    print("instructions (asm vs C):")
    for name, n, so, do in [("ig_memcpy", 16, 0, 0), ("ig_memcpy", 256, 0, 0), ("ig_memcpy", 4096, 0, 0),
                            ("ig_memcpy", 4096, 1, 0), ("ig_memset", 256, 0, 0), ("ig_memset", 4096, 0, 0),
                            ("ig_strlen", 10, 0, 0), ("ig_strlen", 200, 0, 0)]:
        res = []
        for cpu in (asm, c):
            if name == "ig_strlen":
                cpu.uc.mem_write(BUF + so, b"x" * n + b"\0")
                _, k = cpu.call(name, BUF + so)
            else:
                _, k = cpu.call(name, BUF + 0x8000 + do, BUF + so, n)
            res.append(k)
        print("  %-10s %5d bytes%s: %6d vs %6d  (%.1fx)" % (name, n, " (src+1)" if so else "", res[0], res[1], res[1] / float(res[0])))


if __name__ == "__main__":
    asm = Cpu(*load(sys.argv[1], sys.argv[2]))
    c = Cpu(*load(sys.argv[3], sys.argv[4]))
    check(asm, c, random.Random(9300))
    speed(asm, c)
