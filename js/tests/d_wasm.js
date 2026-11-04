(function () {
var o = [];
function p(x) { o.push(typeof x === 'object' && x !== null ? JSON.stringify(x) : String(x)); }
var bytes = new Uint8Array([0, 97, 115, 109, 1, 0, 0, 0, 1, 51, 10, 96, 2, 127, 127, 1, 127, 96, 1, 126, 1, 126, 96, 1, 127, 1, 127, 96, 0, 1, 127, 96, 1, 127, 0, 96, 2, 124, 124, 1, 124, 96, 0, 0, 96, 2, 127, 127, 2, 127, 127, 96, 1, 124, 1, 127, 96, 1, 127, 1, 124, 2, 23, 2, 3, 101, 110, 118, 3, 108, 111, 103, 0, 4, 3, 101, 110, 118, 5, 116, 119, 105, 99, 101, 0, 2, 3, 20, 19, 0, 1, 2, 0, 2, 0, 2, 2, 3, 5, 0, 2, 2, 9, 8, 9, 7, 0, 2, 4, 4, 1, 112, 0, 4, 5, 4, 1, 1, 1, 4, 6, 7, 1, 127, 1, 65, 228, 0, 11, 7, 150, 1, 20, 3, 97, 100, 100, 0, 2, 3, 102, 105, 98, 0, 4, 4, 112, 111, 107, 101, 0, 5, 7, 99, 97, 108, 108, 111, 117, 116, 0, 6, 8, 105, 110, 100, 105, 114, 101, 99, 116, 0, 7, 2, 115, 113, 0, 8, 4, 98, 117, 109, 112, 0, 10, 5, 102, 109, 97, 116, 104, 0, 11, 3, 100, 105, 118, 0, 12, 4, 103, 114, 111, 119, 0, 13, 2, 115, 119, 0, 14, 4, 98, 105, 116, 115, 0, 15, 4, 99, 111, 110, 118, 0, 16, 4, 102, 97, 99, 116, 0, 17, 4, 115, 119, 97, 112, 0, 18, 5, 102, 105, 108, 108, 51, 0, 19, 5, 115, 117, 109, 116, 111, 0, 20, 6, 109, 101, 109, 111, 114, 121, 2, 0, 7, 99, 111, 117, 110, 116, 101, 114, 3, 0, 5, 116, 97, 98, 108, 101, 1, 0, 9, 8, 1, 0, 65, 1, 11, 2, 8, 9, 10, 226, 2, 19, 7, 0, 32, 0, 32, 1, 106, 11, 23, 0, 32, 0, 66, 2, 83, 4, 126, 66, 1, 5, 32, 0, 32, 0, 66, 1, 125, 16, 3, 126, 11, 11, 53, 3, 1, 127, 1, 127, 1, 127, 65, 0, 33, 1, 65, 1, 33, 2, 2, 64, 3, 64, 32, 0, 69, 13, 1, 32, 1, 32, 2, 106, 33, 3, 32, 2, 33, 1, 32, 3, 33, 2, 32, 0, 65, 1, 107, 33, 0, 12, 0, 11, 11, 32, 1, 11, 14, 0, 32, 0, 32, 1, 54, 2, 0, 32, 0, 45, 0, 1, 11, 13, 0, 32, 0, 16, 0, 32, 0, 16, 1, 65, 1, 106, 11, 9, 0, 32, 1, 32, 0, 17, 2, 0, 11, 7, 0, 32, 0, 32, 0, 108, 11, 7, 0, 65, 0, 32, 0, 107, 11, 11, 0, 35, 0, 65, 1, 106, 36, 0, 35, 0, 11, 22, 0, 32, 0, 159, 32, 0, 32, 1, 164, 160, 68, 0, 0, 0, 0, 0, 0, 4, 64, 158, 160, 11, 7, 0, 32, 0, 32, 1, 109, 11, 6, 0, 32, 0, 64, 0, 11, 26, 0, 2, 64, 2, 64, 2, 64, 32, 0, 14, 2, 0, 1, 2, 11, 65, 10, 15, 11, 65, 20, 15, 11, 65, 30, 11, 26, 0, 66, 1, 121, 66, 127, 123, 124, 32, 0, 172, 66, 40, 134, 66, 40, 136, 124, 66, 1, 66, 63, 137, 124, 185, 11, 19, 0, 32, 0, 252, 2, 65, 200, 1, 192, 106, 66, 133, 128, 128, 128, 16, 167, 106, 11, 8, 0, 32, 0, 172, 16, 3, 185, 11, 6, 0, 32, 1, 32, 0, 11, 18, 1, 1, 127, 32, 0, 32, 1, 65, 3, 252, 11, 0, 32, 0, 45, 0, 0, 11, 52, 1, 1, 127, 2, 64, 3, 64, 32, 0, 69, 13, 1, 32, 0, 65, 1, 113, 4, 64, 32, 1, 32, 0, 106, 33, 1, 5, 32, 1, 32, 0, 65, 2, 108, 106, 33, 1, 11, 32, 0, 65, 1, 107, 33, 0, 12, 0, 11, 11, 32, 1, 11, 11, 17, 1, 0, 65, 16, 11, 11, 104, 101, 108, 108, 111, 44, 32, 119, 97, 115, 109]);
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

