(function () { var o = [];
function p(x) { o.push(typeof x === 'object' && x !== null ? JSON.stringify(x) : String(x)); }
var b = new ArrayBuffer(16), u8 = new Uint8Array(b), i32 = new Int32Array(b), f64 = new Float64Array(b, 8, 1);
i32[0] = -2; f64[0] = 1.5; p(Array.from(u8)); p([u8.length, i32.length, f64.length, f64.byteOffset, b.byteLength, u8.BYTES_PER_ELEMENT, Float64Array.BYTES_PER_ELEMENT]);
var c = new Uint8ClampedArray([300, -5, 1.5, 2.5, 254.6]); p(Array.from(c));
var i8 = new Int8Array([127, 128, 255, -129]); p(Array.from(i8));
var u16 = new Uint16Array(3); u16.set([1, 2], 1); p(Array.from(u16));
var s = u8.subarray(4, 8); s[0] = 9; p([u8[4], s.length, s.byteOffset, s.buffer === b]);
var sl = u8.slice(0, 2); sl[0] = 77; p([u8[0], sl[0]]);
p(Array.from(new Float32Array([1.1, 2])).map(function (x) { return x.toFixed(4); }));
var dv = new DataView(b); dv.setUint16(0, 0x1234); p([dv.getUint16(0), dv.getUint16(0, true), dv.getInt8(1), u8[0], u8[1]]);
dv.setFloat64(8, -0.25, true); p([f64[0], dv.getFloat64(8, true)]); dv.setFloat32(0, 3.5); p(dv.getFloat32(0));
dv.setInt32(4, -7, true); p([dv.getInt32(4, true), dv.getUint32(4, true)]);
p([Object.prototype.toString.call(u8), ArrayBuffer.isView(u8), ArrayBuffer.isView(dv), ArrayBuffer.isView(b), u8 instanceof Uint8Array, typeof Uint8Array]);
var m = new Int16Array([3, 1, 2]).map(function (x) { return x * 10; }); p([m.constructor === Int16Array, Array.from(m)]);
p(Array.from(new Uint8Array([5, 3, 10, 1]).sort())); p(new Uint8Array([1, 2, 3]).join('-')); p([...new Uint8Array([7, 8])]);
p(Array.from(Uint8Array.from(new Set([4, 5])))); p(Array.from(Int32Array.of(1, 2, 3).filter(function (x) { return x > 1; })));
p(new Uint8Array(4).fill(6, 1, 3).join()); p(Array.from(new Uint8Array([1, 2, 3, 4, 5]).copyWithin(0, 3)));
var big = new Float64Array(1000); for (var i = 0; i < 1000; i++) big[i] = i / 2; var t = 0; for (i = 0; i < 1000; i++) t += big[i]; p(t);
p(new Uint8Array(b.slice(4, 6)).length); p([u8[99], u8.length]); u8[99] = 1; p(u8[99]);
try { new Int32Array(b, 3); } catch (e) { p(e.name); }
p(Array.from(new Uint32Array(new Uint8Array([1, 2, 3]))));
p(new Uint8Array([1, 2, 3]).indexOf(2) + ',' + new Uint8Array([1, 2, 3]).includes(3) + ',' + new Uint8Array([1, 2]).reduce(function (a, b) { return a + b; }));
return o.join(';'); })()
