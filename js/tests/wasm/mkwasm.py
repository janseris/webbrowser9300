#!/usr/bin/env python3
"""A small WebAssembly assembler for the tests: builds js/tests/d_wasm.js (a module made here,
its bytes in the script, run through the WebAssembly API). python3 mkwasm.py > ../d_wasm.js"""
import struct, sys

def u(n):
    out = bytearray()
    while True:
        b = n & 0x7f; n >>= 7
        if n: out.append(b | 0x80)
        else: out.append(b); return bytes(out)
def s(n):
    out = bytearray()
    while True:
        b = n & 0x7f; n >>= 7
        if (n == 0 and not b & 0x40) or (n == -1 and b & 0x40): out.append(b); return bytes(out)
        out.append(b | 0x80)
def vec(items): return u(len(items)) + b''.join(items)
def name(t): t = t.encode(); return u(len(t)) + t
def sec(i, body): return bytes([i]) + u(len(body)) + body
I32, I64, F32, F64 = 0x7f, 0x7e, 0x7d, 0x7c
def ft(p, r): return b'\x60' + vec([bytes([x]) for x in p]) + vec([bytes([x]) for x in r])

# instructions
def i32c(n): return b'\x41' + s(n)
def i64c(n): return b'\x42' + s(n)
def f64c(x): return b'\x44' + struct.pack('<d', x)
def f32c(x): return b'\x43' + struct.pack('<f', x)
def lget(i): return b'\x20' + u(i)
def lset(i): return b'\x21' + u(i)
def ltee(i): return b'\x22' + u(i)
def gget(i): return b'\x23' + u(i)
def gset(i): return b'\x24' + u(i)
def call(i): return b'\x10' + u(i)
def br(d): return b'\x0c' + u(d)
def brif(d): return b'\x0d' + u(d)
def mem(op, off=0, al=2): return bytes([op]) + u(al) + u(off)
BLOCK, LOOP, IF, ELSE, END = b'\x02\x40', b'\x03\x40', b'\x04\x40', b'\x05', b'\x0b'
def ifr(t): return b'\x04' + bytes([t])
def blockr(t): return b'\x02' + bytes([t])

types = [ft([I32, I32], [I32]), ft([I64], [I64]), ft([I32], [I32]), ft([], [I32]), ft([I32], []), ft([F64, F64], [F64]),
         ft([], []), ft([I32, I32], [I32, I32]), ft([F64], [I32]), ft([I32], [F64])]
# imports: env.log (i32) -> (), env.twice (i32) -> i32
imports = [name('env') + name('log') + b'\x00' + u(4), name('env') + name('twice') + b'\x00' + u(2)]
funcs = []   # (type, locals, body, export name)
def fn(t, locals_, body, ex=None): funcs.append((t, locals_, body, ex))
NIMP = 2
# 2 add
fn(0, [], lget(0) + lget(1) + b'\x6a', 'add')
# 3 fact (i64 recursive)
fn(1, [], lget(0) + i64c(2) + b'\x53' + ifr(I64) + i64c(1) + ELSE + lget(0) + lget(0) + i64c(1) + b'\x7d' + call(3) + b'\x7e' + END)
# 4 fib (loop)
fn(2, [I32, I32, I32], i32c(0) + lset(1) + i32c(1) + lset(2) + BLOCK + LOOP + lget(0) + b'\x45' + brif(1)
   + lget(1) + lget(2) + b'\x6a' + lset(3) + lget(2) + lset(1) + lget(3) + lset(2) + lget(0) + i32c(1) + b'\x6b' + lset(0) + br(0) + END + END + lget(1), 'fib')
# 5 store (addr, value) -> stores i32, returns load8_u of addr+1
fn(0, [], lget(0) + lget(1) + mem(0x36) + lget(0) + mem(0x2d, 1, 0), 'poke')
# 6 calls imports: log(x); return twice(x) + 1
fn(2, [], lget(0) + call(0) + lget(0) + call(1) + i32c(1) + b'\x6a', 'callout')
# 7 indirect(i, x): table[i](x)
fn(0, [], lget(1) + lget(0) + b'\x11' + u(2) + u(0), 'indirect')
# 8 sq (i32)->i32, 9 neg
fn(2, [], lget(0) + lget(0) + b'\x6c', 'sq')
fn(2, [], i32c(0) + lget(0) + b'\x6b')
# 10 counter: global0 += 1, returns it
fn(3, [], gget(0) + i32c(1) + b'\x6a' + gset(0) + gget(0), 'bump')
# 11 fmath(a, b): sqrt(a) + min(a, b) + nearest(2.5)
fn(5, [], lget(0) + b'\x9f' + lget(0) + lget(1) + b'\xa4' + b'\xa0' + f64c(2.5) + b'\x9e' + b'\xa0', 'fmath')
# 12 div(a, b) signed (traps on 0)
fn(0, [], lget(0) + lget(1) + b'\x6d', 'div')
# 13 grow(n) -> old pages
fn(2, [], lget(0) + b'\x40\x00', 'grow')
# 14 sw(i): br_table -> 10, 20, 30 (default)
fn(2, [], BLOCK + BLOCK + BLOCK + lget(0) + b'\x0e' + u(2) + u(0) + u(1) + u(2) + END + i32c(10) + b'\x0f' + END + i32c(20) + b'\x0f' + END + i32c(30), 'sw')
# 15 i64 bits: clz(1) + popcnt(-1) + (x << 40 >>> 40) + rotl
fn(9, [], i64c(1) + b'\x79' + i64c(-1) + b'\x7b' + b'\x7c' + lget(0) + b'\xac' + i64c(40) + b'\x86' + i64c(40) + b'\x88' + b'\x7c' + i64c(1) + i64c(63) + b'\x89' + b'\x7c' + b'\xb9', 'bits')
# 16 conv(f64) -> i32: trunc_sat_f64_s + extend8_s(200) + wrap(i64 0x1_0000_0005)
fn(8, [], lget(0) + b'\xfc' + u(2) + i32c(200) + b'\xc0' + b'\x6a' + i64c(0x100000005) + b'\xa7' + b'\x6a', 'conv')
# 20 fact(i32) -> f64 (i64 inside)
fn(9, [], lget(0) + b'\xac' + call(3) + b'\xb9', 'fact')
# 17 swap (multi-value): (a, b) -> (b, a)
fn(7, [], lget(1) + lget(0), 'swap')
# 18 fill(dst, val, n) memory.fill; returns load8_u(dst)
fn(0, [I32], lget(0) + lget(1) + i32c(3) + b'\xfc' + u(11) + b'\x00' + lget(0) + mem(0x2d, 0, 0), 'fill3')
# 19 sumto(n): sum 1..n via block/loop with if/else inside
fn(2, [I32], BLOCK + LOOP + lget(0) + b'\x45' + brif(1) + lget(0) + i32c(1) + b'\x71' + IF + lget(1) + lget(0) + b'\x6a' + lset(1) + ELSE + lget(1) + lget(0) + i32c(2) + b'\x6c' + b'\x6a' + lset(1) + END + lget(0) + i32c(1) + b'\x6b' + lset(0) + br(0) + END + END + lget(1), 'sumto')

typesec = sec(1, vec(types))
impsec = sec(2, vec(imports))
funcsec = sec(3, vec([u(f[0]) for f in funcs]))
tablesec = sec(4, vec([b'\x70\x00' + u(4)]))
memsec = sec(5, vec([b'\x01' + u(1) + u(4)]))
globsec = sec(6, vec([bytes([I32, 1]) + i32c(100) + END]))
exports = [name(f[3]) + b'\x00' + u(NIMP + i) for i, f in enumerate(funcs) if f[3]] + [name('memory') + b'\x02' + u(0), name('counter') + b'\x03' + u(0), name('table') + b'\x01' + u(0)]
expsec = sec(7, vec(exports))
elemsec = sec(9, vec([u(0) + i32c(1) + END + vec([u(8), u(9)])]))
def body(f):
    loc = [u(1) + bytes([t]) for t in f[1]]
    b = vec(loc) + f[2] + END
    return u(len(b)) + b
codesec = sec(10, vec([body(f) for f in funcs]))
datasec = sec(11, vec([u(0) + i32c(16) + END + vec([bytes([c]) for c in 'hello, wasm'.encode()])]))
mod = b'\0asm\1\0\0\0' + typesec + impsec + funcsec + tablesec + memsec + globsec + expsec + elemsec + codesec + datasec

js = '''(function () {
var o = [];
function p(x) { o.push(typeof x === 'object' && x !== null ? JSON.stringify(x) : String(x)); }
var bytes = new Uint8Array([%s]);
p([WebAssembly.validate(bytes), WebAssembly.validate(new Uint8Array([0, 97, 115, 109, 2, 0, 0, 0])), typeof WebAssembly.Module]);
var logged = [];
var m = new WebAssembly.Module(bytes);
p(WebAssembly.Module.imports(m).map(function (i) { return i.module + '.' + i.name + ':' + i.kind; }));
p(WebAssembly.Module.exports(m).length);
var inst = new WebAssembly.Instance(m, { env: { log: function (x) { logged.push(x); }, twice: function (x) { return x * 2; } } }), e = inst.exports;
p([e.add(2, 3), e.add(0x7fffffff, 1), e.fact(20), e.fib(30), e.sq(9)]);
var u8 = new Uint8Array(e.memory.buffer);
p(String.fromCharCode.apply(null, Array.from(u8.subarray(16, 27))));
p([e.poke(100, 0x01020304), u8[100], u8[103], new Int32Array(e.memory.buffer)[25]]);
p([e.callout(21), logged]);
p([e.indirect(1, 7), e.indirect(2, 7)]);
try { e.indirect(0, 1); } catch (x) { p(x instanceof WebAssembly.RuntimeError); }
p([e.bump(), e.bump(), e.counter.value]); e.counter.value = 7; p(e.bump());
p(e.fmath(16, 3));
try { e.div(1, 0); } catch (x) { p([x.name, x instanceof WebAssembly.RuntimeError, x.message]); }
p([e.div(-7, 2), e.memory.buffer.byteLength, e.grow(1), e.memory.buffer.byteLength, e.grow(10)]);
u8 = new Uint8Array(e.memory.buffer); p([u8.length, u8[16]]);
p([e.sw(0), e.sw(1), e.sw(2), e.sw(9)]);
p(e.bits(12345));
p([e.conv(1e20), e.conv(-3.9), e.conv(NaN)]);
p(e.swap(1, 2));
p([e.fill3(200, 0x1ab), u8[201], u8[203]]);
p([e.sumto(10), typeof e.table.get(1), e.table.get(1)(5), e.table.length]);
var done = [];
WebAssembly.instantiate(bytes, { env: { log: function () {}, twice: function (x) { return x; } } }).then(function (r) { done.push(r.instance.exports.add(40, 2), r.module instanceof WebAssembly.Module); });
var mm = new WebAssembly.Memory({ initial: 1, maximum: 2 }); p([mm.buffer.byteLength, mm.grow(1), mm.buffer.byteLength]);
try { mm.grow(1); } catch (x) { p(x.name); }
p(String(WebAssembly));
return o.join(';');
})()
'''
if len(sys.argv) > 1: open(sys.argv[1], 'wb').write(mod)   # (the module itself, for a page's fetch)
else: print(js % ', '.join(str(b) for b in mod))
