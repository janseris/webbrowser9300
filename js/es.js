// Browser 9300: the engine's ES2015+ runtime (js/genes.py makes js/mujs/jses.h from it; every
// js_State runs it). The hidden helpers (names starting with '%') are what the parser's
// lowerings call (js/mujs/jsparse.c); then Symbol, Map, Set, WeakMap, WeakSet, Promise,
// iterators and the newer methods. ES5 only. Symbols are strings ("@@iterator").
(function (G) {
var OP = Object.prototype, AP = Array.prototype, SP = String.prototype, FP = Function.prototype;
var hasOwn = OP.hasOwnProperty, slice = AP.slice, defp = Object.defineProperty;
function hid(o, n, v) { defp(o, n, { value: v, writable: true, configurable: true, enumerable: false }); }
function def(o, n, v) { if (!hasOwn.call(o, n)) hid(o, n, v); }
function isObj(x) { return x !== null && (typeof x === 'object' || typeof x === 'function'); }

// ---- Symbol (strings)
var symN = 0;
function Symbol() {   // (length 0, as polyfills test)
    var d = arguments[0];
    if (this instanceof Symbol) throw new TypeError('Symbol is not a constructor');
    return '@@' + (d === undefined ? '' : String(d)) + '@' + (++symN);
}
var registry = {};
hid(Symbol, 'for', function (k) { k = String(k); return hasOwn.call(registry, k) ? registry[k] : (registry[k] = '@@' + k + '@for'); });
hid(Symbol, 'keyFor', function (s) { for (var k in registry) if (registry[k] === s) return k; });
var wk = ['iterator', 'asyncIterator', 'hasInstance', 'toPrimitive', 'toStringTag', 'species', 'isConcatSpreadable', 'match', 'replace', 'search', 'split', 'unscopables', 'matchAll'];
for (var i = 0; i < wk.length; i++) hid(Symbol, wk[i], '@@' + wk[i]);
if (typeof G.Symbol !== 'function') hid(G, 'Symbol', Symbol);
var ITER = '@@iterator', AITER = '@@asyncIterator';

// ---- iterators
function iterResult(v, d) { return { value: v, done: d }; }
function ArrayIterator(a, kind) { this.a = a; this.i = 0; this.k = kind; }
hid(ArrayIterator.prototype, 'next', function () {
    var a = this.a;
    if (!a || this.i >= a.length) { this.a = null; return iterResult(undefined, true); }
    var i = this.i++;
    return iterResult(this.k === 0 ? a[i] : this.k === 1 ? i : [i, a[i]], false);
});
hid(ArrayIterator.prototype, ITER, function () { return this; });
var IterProto = Object.getPrototypeOf(new ArrayIterator([], 0));
hid(AP, 'values', function () { return new ArrayIterator(this, 0); });
hid(AP, 'keys', function () { return new ArrayIterator(this, 1); });
hid(AP, 'entries', function () { return new ArrayIterator(this, 2); });
hid(AP, ITER, AP.values);
function strChars(s) {
    var r = [], i, c;
    for (i = 0; i < s.length; i++) {
        c = s.charCodeAt(i);
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < s.length) { r.push(s.charAt(i) + s.charAt(i + 1)); i++; }
        else r.push(s.charAt(i));
    }
    return r;
}
hid(SP, ITER, function () { return new ArrayIterator(strChars(String(this)), 0); });
hid(G, '%iterstep', function (it) { var r = it.next(); if (!isObj(r)) throw new TypeError('iterator result is not an object'); return r; });

// an iterable as an array (for ..., for-of and destructuring)
function toArr(x) {
    if (x instanceof Array && x[ITER] === AP.values) return x;
    if (typeof x === 'string') return strChars(x);
    if (x == null) throw new TypeError(x + ' is not iterable');
    var f = x[ITER];
    if (typeof f === 'function') {
        var it = f.call(x), r = [], s;
        if (it instanceof ArrayIterator && it.k === 0 && it.a) return slice.call(it.a, it.i);
        while (!(s = it.next()).done) r.push(s.value);
        return r;
    }
    if (typeof x.length === 'number') return slice.call(x);
    throw new TypeError((typeof x) + ' is not iterable');
}
hid(G, '%arr', toArr);
hid(G, '%slice', function (a, i) { return slice.call(a, i); });
hid(G, '%pushall', function (a, x) { x = toArr(x); for (var i = 0, n = a.length; i < x.length; i++) a[n + i] = x[i]; a.length = n + x.length; });
hid(G, '%apply', function (f, t, args) {
    if (typeof f !== 'function') throw new TypeError(typeof f + ' is not a function');
    return f.apply(t, args);
});
hid(G, '%pow', Math.pow);
function assign(t, s) {
    if (s != null) {
        if (typeof s === 'string') { for (var j = 0; j < s.length; j++) t[j] = s.charAt(j); return t; }
        for (var k in s) if (hasOwn.call(s, k)) t[k] = s[k];
    }
    return t;
}
hid(G, '%assign', assign);
hid(G, '%objrest', function (o, keys) {
    var r = {}, k, i, skip = {};
    for (i = 0; i < keys.length; i++) skip[keys[i]] = 1;
    if (o != null) for (k in o) if (hasOwn.call(o, k) && !hasOwn.call(skip, k)) r[k] = o[k];
    return r;
});
var tplCache = {};
hid(G, '%tpl', function (id, cooked, raw) {
    var t = tplCache[id];
    if (t) return t;
    hid(cooked, 'raw', raw);
    if (Object.freeze) { Object.freeze(raw); Object.freeze(cooked); }
    return (tplCache[id] = cooked);
});
// classes
hid(G, '%defm', function (o, k, f) { hid(o, k, f); });
hid(G, '%defa', function (o, k, g, s) {
    var d = Object.getOwnPropertyDescriptor(o, k) || {};
    defp(o, k, { get: g || d.get, set: s || d.set, configurable: true, enumerable: false });
});
hid(G, '%defp', function (o, k, v) {
    k = String(k);
    defp(o, k, { value: v, writable: true, configurable: true, enumerable: k.charAt(0) !== '#' });
});
hid(G, '%inherit', function (C, S) {
    if (S === null) { C.prototype = Object.create(null); return; }
    if (typeof S !== 'function') throw new TypeError('Class extends value ' + S + ' is not a constructor or null');
    C.prototype = Object.create(S.prototype);
    hid(C.prototype, 'constructor', C);
    Object.setPrototypeOf(C, S);
});
var natives = [Error, TypeError, RangeError, SyntaxError, ReferenceError, EvalError, URIError];
hid(G, '%superCall', function (self, S, args, F) {
    var r, i, k;
    for (i = 0; i < natives.length; i++) if (S === natives[i]) break;
    if (i < natives.length) { // an Error: its message and stack onto this
        r = S.apply(null, args);
        if (r.message !== undefined && r.message !== '') hid(self, 'message', r.message);
        if (r.stack !== undefined) hid(self, 'stack', r.stack);
    } else if (S === Object) {
    } else if (S === Array) {
        for (i = 0; i < args.length; i++) self[i] = args[i];
        self.length = args.length;
    } else {
        try { r = S.apply(self, args); }
        catch (e) { if (S['%noctor'] || /\[native code\]/.test(FP.toString.call(S))) r = undefined; else throw e; }
        if (isObj(r) && r !== self) for (k in r) if (hasOwn.call(r, k)) self[k] = r[k];
    }
    if (F) F.call(self);
});

// ---- Object, Array, String, Number, Math: the newer methods
def(Object, 'assign', function (t) {
    if (t == null) throw new TypeError('Cannot convert undefined or null to object');
    t = Object(t);
    for (var i = 1; i < arguments.length; i++) assign(t, arguments[i]);
    return t;
});
def(Object, 'is', function (a, b) { return a === b ? (a !== 0 || 1 / a === 1 / b) : (a !== a && b !== b); });
def(Object, 'values', function (o) { var r = [], k; for (k in o) if (hasOwn.call(o, k)) r.push(o[k]); return r; });
def(Object, 'entries', function (o) { var r = [], k; for (k in o) if (hasOwn.call(o, k)) r.push([k, o[k]]); return r; });
def(Object, 'fromEntries', function (it) { var o = {}, a = toArr(it); for (var i = 0; i < a.length; i++) o[a[i][0]] = a[i][1]; return o; });
def(Object, 'getOwnPropertySymbols', function () { return []; });
def(Object, 'getOwnPropertyDescriptors', function (o) {
    var r = {}, n = Object.getOwnPropertyNames(o);
    for (var i = 0; i < n.length; i++) r[n[i]] = Object.getOwnPropertyDescriptor(o, n[i]);
    return r;
});
def(Array, 'from', function (a, f, t) {
    if (a == null) throw new TypeError('Array.from: ' + a + ' is not iterable');
    var src = (typeof a === 'string' || typeof a[ITER] === 'function') ? toArr(a) : a, n = src.length >>> 0, r = new Array(n);
    for (var i = 0; i < n; i++) r[i] = f ? f.call(t, src[i], i) : src[i];
    return r;
});
def(Array, 'of', function () { return slice.call(arguments); });
def(AP, 'find', function (f, t) { for (var i = 0; i < this.length; i++) if (f.call(t, this[i], i, this)) return this[i]; });
def(AP, 'findIndex', function (f, t) { for (var i = 0; i < this.length; i++) if (f.call(t, this[i], i, this)) return i; return -1; });
def(AP, 'findLast', function (f, t) { for (var i = this.length - 1; i >= 0; i--) if (f.call(t, this[i], i, this)) return this[i]; });
def(AP, 'findLastIndex', function (f, t) { for (var i = this.length - 1; i >= 0; i--) if (f.call(t, this[i], i, this)) return i; return -1; });
def(AP, 'includes', function (v, from) {
    var n = this.length, i = from | 0;
    if (i < 0) i = Math.max(n + i, 0);
    for (; i < n; i++) if (this[i] === v || (v !== v && this[i] !== this[i])) return true;
    return false;
});
def(AP, 'fill', function (v, s, e) {
    var n = this.length; s = s | 0; e = e === undefined ? n : e | 0;
    if (s < 0) s = Math.max(s + n, 0); if (e < 0) e += n;
    for (var i = s; i < e && i < n; i++) this[i] = v;
    return this;
});
def(AP, 'copyWithin', function (t, s, e) {
    var n = this.length, a = slice.call(this, s | 0, e === undefined ? n : e), i;
    t = t | 0; if (t < 0) t += n;
    for (i = 0; i < a.length && t + i < n; i++) this[t + i] = a[i];
    return this;
});
function flat(a, d, r) {
    for (var i = 0; i < a.length; i++) {
        if (!(i in a)) continue;
        if (d > 0 && a[i] instanceof Array) flat(a[i], d - 1, r); else r.push(a[i]);
    }
    return r;
}
def(AP, 'flat', function (d) { return flat(this, d === undefined ? 1 : +d, []); });
def(AP, 'flatMap', function (f, t) { return flat(AP.map.call(this, f, t), 1, []); });
def(AP, 'at', function (i) { i = Math.trunc ? Math.trunc(+i) || 0 : i | 0; if (i < 0) i += this.length; return this[i]; });
def(AP, 'toReversed', function () { return slice.call(this).reverse(); });
def(AP, 'toSorted', function (f) { return slice.call(this).sort(f); });
def(AP, 'toSpliced', function () { var a = slice.call(this); AP.splice.apply(a, arguments); return a; });
def(AP, 'with', function (i, v) { var a = slice.call(this); if (i < 0) i += a.length; a[i] = v; return a; });
def(SP, 'substr', function (start, len) {
    var s = String(this), n = s.length;
    start = start | 0; if (start < 0) start = Math.max(n + start, 0);
    len = len === undefined ? n - start : len | 0;
    return len <= 0 ? '' : s.substring(start, start + len);
});
def(SP, 'trimStart', function () { var s = String(this), t = s.trim(); return t ? s.slice(s.indexOf(t)) : ''; });
def(SP, 'trimEnd', function () { var s = String(this), t = s.trim(); return t ? s.slice(0, s.indexOf(t) + t.length) : ''; });
def(SP, 'trimLeft', SP.trimStart);
def(SP, 'trimRight', SP.trimEnd);
def(SP, 'startsWith', function (t, p) { p = p | 0; t = String(t); return String(this).substring(p, p + t.length) === t; });
def(SP, 'endsWith', function (t, e) { var s = String(this); t = String(t); e = e === undefined ? s.length : e | 0; return e - t.length >= 0 && s.substring(e - t.length, e) === t; });
def(SP, 'includes', function (t, p) { if (t instanceof RegExp) throw new TypeError('First argument must not be a regular expression'); return String(this).indexOf(t, p) >= 0; });
def(SP, 'repeat', function (n) { var s = String(this), r = ''; n = n | 0; if (n < 0) throw new RangeError('Invalid count value'); while (n > 0) { if (n & 1) r += s; n >>= 1; s += s; } return r; });
function pad(s, n, f, atStart) {
    s = String(s); n = n | 0; f = f === undefined ? ' ' : String(f);
    if (n <= s.length || f === '') return s;
    var fill = ''; while (fill.length < n - s.length) fill += f;
    fill = fill.substring(0, n - s.length);
    return atStart ? fill + s : s + fill;
}
def(SP, 'padStart', function (n, f) { return pad(this, n, f, true); });
def(SP, 'padEnd', function (n, f) { return pad(this, n, f, false); });
def(SP, 'at', function (i) { var s = String(this); i = i | 0; if (i < 0) i += s.length; return s.charAt(i) || undefined; });
def(SP, 'codePointAt', function (i) {
    var s = String(this), c = s.charCodeAt(i | 0), d;
    if (c !== c) return undefined;
    if (c >= 0xD800 && c < 0xDC00) { d = s.charCodeAt((i | 0) + 1); if (d >= 0xDC00 && d < 0xE000) return (c - 0xD800) * 0x400 + d - 0xDC00 + 0x10000; }
    return c;
});
def(String, 'fromCodePoint', function (cp) {
    var r = '', c;
    for (var i = 0; i < arguments.length; i++) {
        c = +arguments[i];
        if (c > 0xFFFF) { c -= 0x10000; r += String.fromCharCode(0xD800 + (c >> 10), 0xDC00 + (c & 1023)); }
        else r += String.fromCharCode(c);
    }
    return r;
});
def(String, 'raw', function (t) {
    var r = '', raw = t.raw;
    for (var i = 0; i < raw.length; i++) { r += raw[i]; if (i + 1 < raw.length && i + 1 < arguments.length) r += arguments[i + 1]; }
    return r;
});
function reEscape(s) { return s.replace(/[\\^$.*+?()[\]{}|\/]/g, '\\$&'); }
def(SP, 'replaceAll', function (p, r) {
    if (p instanceof RegExp) {
        if (!p.global) throw new TypeError('replaceAll must be called with a global RegExp');
        return String(this).replace(p, r);
    }
    return String(this).replace(new RegExp(reEscape(String(p)), 'g'), typeof r === 'function' ? r : String(r));
});
def(SP, 'matchAll', function (re) {
    var s = String(this), r = [], m;
    re = re instanceof RegExp ? new RegExp(re.source, (re.global ? 'g' : 'g') + (re.ignoreCase ? 'i' : '') + (re.multiline ? 'm' : '')) : new RegExp(re, 'g');
    while ((m = re.exec(s)) !== null) { r.push(m); if (m[0] === '') re.lastIndex++; }
    return new ArrayIterator(r, 0);
});
def(SP, 'normalize', function () { return String(this); });
def(SP, 'isWellFormed', function () { return true; });
def(Number, 'isNaN', function (v) { return typeof v === 'number' && v !== v; });
def(Number, 'isFinite', function (v) { return typeof v === 'number' && isFinite(v); });
def(Number, 'isInteger', function (v) { return typeof v === 'number' && isFinite(v) && Math.floor(v) === v; });
def(Number, 'isSafeInteger', function (v) { return Number.isInteger(v) && Math.abs(v) <= 9007199254740991; });
def(Number, 'parseFloat', parseFloat);
def(Number, 'parseInt', parseInt);
def(Number, 'EPSILON', Math.pow(2, -52));
def(Number, 'MAX_SAFE_INTEGER', 9007199254740991);
def(Number, 'MIN_SAFE_INTEGER', -9007199254740991);
def(Math, 'sign', function (x) { x = +x; return x > 0 ? 1 : x < 0 ? -1 : x; });
def(Math, 'trunc', function (x) { x = +x; return x < 0 ? Math.ceil(x) : Math.floor(x); });
def(Math, 'log10', function (x) { return Math.log(x) / Math.LN10; });
def(Math, 'log2', function (x) { return Math.log(x) / Math.LN2; });
def(Math, 'log1p', function (x) { return Math.log(1 + x); });
def(Math, 'expm1', function (x) { return Math.exp(x) - 1; });
def(Math, 'cbrt', function (x) { x = +x; return x < 0 ? -Math.pow(-x, 1 / 3) : Math.pow(x, 1 / 3); });
def(Math, 'hypot', function () { var s = 0; for (var i = 0; i < arguments.length; i++) s += arguments[i] * arguments[i]; return Math.sqrt(s); });
def(Math, 'sinh', function (x) { return (Math.exp(x) - Math.exp(-x)) / 2; });
def(Math, 'cosh', function (x) { return (Math.exp(x) + Math.exp(-x)) / 2; });
def(Math, 'tanh', function (x) { var a = Math.exp(x), b = Math.exp(-x); return a === Infinity ? 1 : b === Infinity ? -1 : (a - b) / (a + b); });
def(Math, 'fround', function (x) { return +x; });
def(Math, 'imul', function (a, b) { var ah = (a >>> 16) & 0xffff, al = a & 0xffff, bh = (b >>> 16) & 0xffff, bl = b & 0xffff; return ((al * bl) + (((ah * bl + al * bh) << 16) >>> 0) | 0); });
def(Math, 'clz32', function (x) { x = x >>> 0; if (!x) return 32; var n = 0; while (!(x & 0x80000000)) { x <<= 1; n++; } return n; });
def(Math, 'acosh', function (x) { return Math.log(x + Math.sqrt(x * x - 1)); });
def(Math, 'asinh', function (x) { x = +x; return x === -Infinity || x === 0 ? x : Math.log(x + Math.sqrt(x * x + 1)); });
def(Math, 'atanh', function (x) { return Math.log((1 + x) / (1 - x)) / 2; });
// ES2015: the Object functions take primitives (as their wrapper objects)
(function () {
    function obj(o) { if (o == null) throw new TypeError('Cannot convert undefined or null to object'); return Object(o); }
    function prim(o) { return o === null || (typeof o !== 'object' && typeof o !== 'function'); }
    var keys = Object.keys, gopn = Object.getOwnPropertyNames, gopd = Object.getOwnPropertyDescriptor, gpo = Object.getPrototypeOf;
    hid(Object, 'keys', function keys_(o) { return keys(prim(o) ? obj(o) : o); });
    hid(Object, 'getOwnPropertyNames', function getOwnPropertyNames(o) { return gopn(prim(o) ? obj(o) : o); });
    hid(Object, 'getOwnPropertyDescriptor', function getOwnPropertyDescriptor(o, k) {
        if (typeof o === 'string' && (k = String(k)) === String(k >>> 0) && +k < o.length) return { value: o.charAt(+k), writable: false, enumerable: true, configurable: false };
        return gopd(prim(o) ? obj(o) : o, k);
    });
    hid(Object, 'getPrototypeOf', function getPrototypeOf(o) { return gpo(prim(o) ? obj(o) : o); });
    ['seal', 'freeze', 'preventExtensions'].forEach(function (n) { var f = Object[n]; if (f) hid(Object, n, function (o) { return prim(o) ? o : f(o); }); });
    ['isSealed', 'isFrozen'].forEach(function (n) { var f = Object[n]; if (f) hid(Object, n, function (o) { return prim(o) ? true : f(o); }); });
    var ie = Object.isExtensible; if (ie) hid(Object, 'isExtensible', function (o) { return prim(o) ? false : ie(o); });
})();
def(G, 'globalThis', G);

// Annex B leftovers old scripts call
var DP = Date.prototype;
def(DP, 'toGMTString', DP.toUTCString);
def(DP, 'getYear', function () { return this.getFullYear() - 1900; });
def(DP, 'setYear', function (y) { y = +y; return this.setFullYear(y >= 0 && y <= 99 ? 1900 + y : y); });
def(OP, '__defineGetter__', function (k, f) { defp(this, k, { get: f, enumerable: true, configurable: true }); });
def(OP, '__defineSetter__', function (k, f) { defp(this, k, { set: f, enumerable: true, configurable: true }); });
def(OP, '__lookupGetter__', function (k) { for (var o = this; o; o = Object.getPrototypeOf(o)) { var d = Object.getOwnPropertyDescriptor(o, k); if (d) return d.get; } });
def(OP, '__lookupSetter__', function (k) { for (var o = this; o; o = Object.getPrototypeOf(o)) { var d = Object.getOwnPropertyDescriptor(o, k); if (d) return d.set; } });
def(RegExp.prototype, 'compile', function (p, f) { return new RegExp(p, f); });
def(FP, 'toSource', function () { return FP.toString.call(this); });

def(Object, 'groupBy', function (it, f) { var o = {}, a = toArr(it); for (var i = 0; i < a.length; i++) { var k = f(a[i], i); (o[k] || (o[k] = [])).push(a[i]); } return o; });

// ---- generators and async functions (the engine's coroutines: %gen makes the object)
function Generator() {}
var GP = Generator.prototype;
function gstep(g, mode, v) {
    var r = g['%co'](mode, v); // [value, done]
    return iterResult(r[0], r[1]);
}
hid(GP, 'next', function (v) { return gstep(this, 0, v); });
hid(GP, 'throw', function (e) { return gstep(this, 1, e); });
hid(GP, 'return', function (v) { return gstep(this, 2, v); });
hid(GP, ITER, function () { return this; });
hid(GP, '@@toStringTag', 'Generator');
hid(G, '%genproto', GP);

// yield* inside a generator: the inner iterator's values one by one (the engine calls this)
hid(G, '%iter', function (x) {
    if (x == null) throw new TypeError(x + ' is not iterable');
    var f = x[ITER];
    if (typeof f !== 'function') { if (typeof x === 'string' || typeof x.length === 'number') return new ArrayIterator(toArr(x), 0); throw new TypeError('not iterable'); }
    return f.call(x);
});
hid(G, '%ystep', function (it, v) { var r = it.next(v); if (!isObj(r)) throw new TypeError('Iterator result ' + r + ' is not an object'); return r; });
hid(G, '%aiter', function (x) {
    var f = x != null && x[AITER];
    return typeof f === 'function' ? f.call(x) : G['%iter'](x);
});
hid(G, '%es', { OP: OP, AP: AP, SP: SP, FP: FP, hasOwn: hasOwn, slice: slice, defp: defp, hid: hid, def: def, isObj: isObj, ITER: ITER, AITER: AITER, iterResult: iterResult, ArrayIterator: ArrayIterator, IterProto: IterProto, toArr: toArr, assign: assign });
})(this);

// ---- the rest, compiled when a page first names one of its globals (js/genes.py: each //@lazy
// block runs with G the global object and the core's helpers above)
//@lazy ArrayBuffer Int8Array Uint8Array Uint8ClampedArray Int16Array Uint16Array Int32Array Uint32Array Float32Array Float64Array BigInt64Array BigUint64Array DataView %TypedArrayPrototype
// ---- ArrayBuffer, typed arrays, DataView: in C (js/mujs/jsbytes.c); the generic methods here
G['%bytes']();
var TAP = G['%TypedArrayPrototype'];
['values', 'keys', 'entries', 'forEach', 'every', 'some', 'find', 'findIndex', 'findLast', 'findLastIndex', 'indexOf', 'lastIndexOf',
 'includes', 'join', 'reduce', 'reduceRight', 'reverse', 'at', 'toLocaleString'].forEach(function (n) { if (AP[n]) hid(TAP, n, AP[n]); });
hid(TAP, ITER, AP[ITER] || AP.values);
hid(TAP, 'toString', AP.toString);
function num(a, b) { return a < b ? -1 : a > b ? 1 : a === b ? (a === 0 && 1 / a < 1 / b ? -1 : 0) : a !== a ? (b !== b ? 0 : 1) : -1; }
hid(TAP, 'sort', function (f) { var a = slice.call(this).sort(f || num); this.set(a); return this; });
hid(TAP, 'toSorted', function (f) { return new this.constructor(slice.call(this).sort(f || num)); });
hid(TAP, 'toReversed', function () { return new this.constructor(slice.call(this).reverse()); });
hid(TAP, 'with', function (i, v) { var r = new this.constructor(this); i = i < 0 ? i + this.length : i; if (!(i >= 0 && i < this.length)) throw new RangeError('Invalid typed array index'); r[i] = v; return r; });
hid(TAP, 'map', function (f, t) { var n = this.length, r = new this.constructor(n); for (var i = 0; i < n; i++) r[i] = f.call(t, this[i], i, this); return r; });
hid(TAP, 'filter', function (f, t) { var a = []; for (var i = 0, n = this.length; i < n; i++) if (f.call(t, this[i], i, this)) a.push(this[i]); return new this.constructor(a); });
['Int8Array', 'Uint8Array', 'Uint8ClampedArray', 'Int16Array', 'Uint16Array', 'Int32Array', 'Uint32Array', 'Float32Array', 'Float64Array', 'BigInt64Array', 'BigUint64Array'].forEach(function (n) {
    var C = G[n];
    hid(C.prototype, '@@toStringTag', n);
    hid(C, 'from', function (a, f, t) { return new this(Array.from(a, f, t)); });
    hid(C, 'of', function () { return new this(slice.call(arguments)); });
});
hid(G.ArrayBuffer.prototype, '@@toStringTag', 'ArrayBuffer');
hid(G.DataView.prototype, '@@toStringTag', 'DataView');
//@end
//@lazy WebAssembly
// ---- WebAssembly: the interpreter is C (js/mujs/jswasm.c, %wasm*); the API here
G['%wasminit']();
var WA = {}, W = function (n) { return G['%wasm' + n]; };
function wErr(name) {
    function E(m) {
        if (!(this instanceof E)) return new E(m);
        var e = new Error(m === undefined ? '' : m);
        this.message = e.message;
        hid(this, 'stackTrace', e.stackTrace);
    }
    E.prototype = Object.create(Error.prototype);
    hid(E.prototype, 'constructor', E);
    hid(E.prototype, 'name', name);
    return E;
}
WA.CompileError = wErr('CompileError'); WA.LinkError = wErr('LinkError'); WA.RuntimeError = wErr('RuntimeError');
hid(G, '%wasmTrap', function (m) { throw new WA.RuntimeError(m); });
hid(G, '%wasmCompileError', function (m) { throw new WA.CompileError('WebAssembly.Module(): ' + m); });
function Module(bytes) {
    if (!(this instanceof Module)) throw new TypeError("WebAssembly.Module must be invoked with 'new'");
    hid(this, '%m', W('Compile')(bytes));
}
Module.imports = function (m) { return W('Imports')(m['%m']); };
Module.exports = function (m) { return W('Exports')(m['%m']).map(function (e) { return { name: e.name, kind: e.kind }; }); };
Module.customSections = function () { return []; };
function Memory(d) {
    if (!(this instanceof Memory)) throw new TypeError("WebAssembly.Memory must be invoked with 'new'");
    var n = d && d.initial !== undefined ? d.initial >>> 0 : 0, mx = d && d.maximum !== undefined ? d.maximum >>> 0 : 65536;
    if (n > 32768 || n > mx) throw new RangeError('WebAssembly.Memory(): could not allocate memory');
    hid(this, '%b', new G.ArrayBuffer(n * 65536));
    hid(this, '%max', mx);
}
defp(Memory.prototype, 'buffer', { get: function () { return this['%b']; }, configurable: true });
hid(Memory.prototype, 'grow', function (n) {
    var r = W('Grow')(this['%b'], n >>> 0, this['%max']);
    if (r < 0) throw new RangeError('WebAssembly.Memory.grow(): Maximum memory size exceeded');
    return r;
});
function Global(d, v) {
    if (!(this instanceof Global)) throw new TypeError("WebAssembly.Global must be invoked with 'new'");
    hid(this, '%t', d && d.value); hid(this, '%mut', !!(d && d.mutable)); hid(this, '%v', v === undefined ? 0 : +v);
}
defp(Global.prototype, 'value', {
    get: function () { return this['%i'] ? W('Global')(this['%i'], this['%g']) : this['%v']; },
    set: function (v) { if (!this['%mut']) throw new TypeError("Can't set the value of an immutable global."); if (this['%i']) W('Global')(this['%i'], this['%g'], v); else this['%v'] = +v; },
    configurable: true
});
hid(Global.prototype, 'valueOf', function () { return this.value; });
function fnFor(inst, i, np) {
    var c = inst['%fc'] || (hid(inst, '%fc', {}), inst['%fc']);
    if (c[i]) return c[i];
    var f = function () { return W('Call')(inst, i, arguments); };
    hid(f, '%wi', [inst, i]);
    return c[i] = f;
}
function Table(d) {
    if (!(this instanceof Table)) throw new TypeError("WebAssembly.Table must be invoked with 'new'");
    hid(this, '%a', []); var n = d && d.initial >>> 0; for (var i = 0; i < n; i++) this['%a'].push(null);
}
defp(Table.prototype, 'length', { get: function () { return this['%i'] ? W('Table')(this['%i']) : this['%a'].length; }, configurable: true });
hid(Table.prototype, 'get', function (i) {
    if (!this['%i']) return this['%a'][i] === undefined ? null : this['%a'][i];
    var f = W('Table')(this['%i'], i >>> 0);
    return f < 0 ? null : fnFor(this['%i'], f);
});
hid(Table.prototype, 'set', function (i, f) {
    if (!this['%i']) { this['%a'][i] = f; return; }
    if (f == null) { W('Table')(this['%i'], i >>> 0, null); return; }
    if (!f['%wi'] || f['%wi'][0] !== this['%i']) throw new TypeError('WebAssembly.Table.set(): only this instance\'s functions');
    W('Table')(this['%i'], i >>> 0, f['%wi'][1]);
});
hid(Table.prototype, 'grow', function () { throw new RangeError('WebAssembly.Table.grow(): not supported'); });
function Instance(module, imports) {
    if (!(this instanceof Instance)) throw new TypeError("WebAssembly.Instance must be invoked with 'new'");
    if (!(module instanceof Module)) throw new TypeError('WebAssembly.Instance(): Argument 0 must be a WebAssembly.Module');
    var m = module['%m'], imps = W('Imports')(m), vals = [], mem = null, info = W('MemoryInfo')(m), ex = {};
    for (var i = 0; i < imps.length; i++) {
        var im = imps[i], o = imports && imports[im.module];
        if (!isObj(o)) throw new TypeError('WebAssembly.Instance(): Import #' + i + ' "' + im.module + '": module is not an object or function');
        var v = o[im.name];
        if (im.kind === 'function') {
            if (typeof v !== 'function') throw new WA.LinkError('WebAssembly.Instance(): Import #' + i + ' "' + im.module + '" "' + im.name + '": function import requires a callable');
            vals[i] = v;
        } else if (im.kind === 'memory') {
            if (!(v instanceof Memory)) throw new WA.LinkError('WebAssembly.Instance(): Import #' + i + ' "' + im.module + '" "' + im.name + '": memory import must be a WebAssembly.Memory object');
            mem = v; vals[i] = null;
        } else if (im.kind === 'global') vals[i] = v instanceof Global ? v.value : +v;
        else vals[i] = v;
    }
    if (info && !info[2]) mem = new Memory({ initial: info[0], maximum: info[1] });
    var inst = W('Instantiate')(m, vals, mem ? mem['%b'] : null);
    var es = W('Exports')(m);
    for (i = 0; i < es.length; i++) {
        var e = es[i];
        if (e.kind === 'function') ex[e.name] = fnFor(inst, e.index, e.params);
        else if (e.kind === 'memory') ex[e.name] = mem;
        else if (e.kind === 'global') { var g = Object.create(Global.prototype); hid(g, '%i', inst); hid(g, '%g', e.index); hid(g, '%mut', true); ex[e.name] = g; }
        else if (e.kind === 'table') { var t = Object.create(Table.prototype); hid(t, '%i', inst); ex[e.name] = t; }
    }
    hid(this, '%i', inst);
    this.exports = ex;
    W('Start')(inst);
}
function bytesOf(b) { return b; }
WA.Module = Module; WA.Instance = Instance; WA.Memory = Memory; WA.Global = Global; WA.Table = Table;
WA.validate = function (b) { try { W('Compile')(b); return true; } catch (e) { return false; } };
WA.compile = function (b) { return new G.Promise(function (r) { r(new Module(b)); }); };
WA.instantiate = function (src, imports) {
    return new G.Promise(function (r) {
        if (src instanceof Module) r(new Instance(src, imports));
        else { var m = new Module(src); r({ module: m, instance: new Instance(m, imports) }); }
    });
};
function streamed(resp) { return G.Promise.resolve(resp).then(function (r) { return r.arrayBuffer(); }); }
WA.compileStreaming = function (resp) { return streamed(resp).then(function (b) { return new Module(b); }); };
WA.instantiateStreaming = function (resp, imports) { return streamed(resp).then(function (b) { return WA.instantiate(b, imports); }); };
hid(WA, '@@toStringTag', 'WebAssembly');
hid(G, 'WebAssembly', WA);
//@end
//@lazy Map Set WeakMap WeakSet WeakRef FinalizationRegistry
// ---- Map, Set (insertion order; object keys get a hidden id)
var idN = 0;
function keyOf(k, make) {
    var t = typeof k;
    if (t === 'string') return 's' + k;
    if (t === 'number') return k !== k ? 'NaN' : 'n' + (k === 0 ? 0 : k);
    if (t === 'boolean' || t === 'undefined' || k === null) return t + k;
    if (!hasOwn.call(k, '@@id')) {
        if (!make) return null;
        if (Object.isExtensible && !Object.isExtensible(k)) return null;
        hid(k, '@@id', ++idN);
    }
    return 'o' + k['@@id'];
}
function MapImpl(set) { this.h = {}; this.k = []; this.v = []; this.n = 0; this.set = set; this.frozen = []; }
function find(m, k, make) {
    var key = keyOf(k, make);
    if (key !== null) return hasOwn.call(m.h, key) ? m.h[key] : -1;
    for (var i = 0; i < m.frozen.length; i++) if (m.k[m.frozen[i]] === k) return m.frozen[i];
    return -1;
}
function add(m, k, v) {
    var i = find(m, k, true), key;
    if (i >= 0) { m.v[i] = v; return; }
    i = m.k.length; m.k.push(k); m.v.push(v); m.n++;
    key = keyOf(k, true);
    if (key !== null) m.h[key] = i; else m.frozen.push(i);
}
function del(m, k) {
    var i = find(m, k, false), key;
    if (i < 0) return false;
    key = keyOf(k, false);
    if (key !== null) delete m.h[key];
    else m.frozen.splice(m.frozen.indexOf(i), 1);
    m.k[i] = m.v[i] = undefined; m.dead = (m.dead || {}); m.dead[i] = 1; m.n--;
    return true;
}
function CollIter(m, kind) { this.m = m; this.i = 0; this.kind = kind; }
hid(CollIter.prototype, 'next', function () {
    var m = this.m;
    while (m && this.i < m.k.length) {
        var i = this.i++;
        if (m.dead && m.dead[i]) continue;
        return iterResult(this.kind === 0 ? m.k[i] : this.kind === 1 ? m.v[i] : [m.k[i], m.v[i]], false);
    }
    this.m = null;
    return iterResult(undefined, true);
});
hid(CollIter.prototype, ITER, function () { return this; });
function Map() {
    var it = arguments[0];
    if (!(this instanceof Map)) throw new TypeError("Constructor Map requires 'new'");
    hid(this, '%m', new MapImpl(false));
    if (it != null) { var a = toArr(it); for (var i = 0; i < a.length; i++) this.set(a[i][0], a[i][1]); }
}
function Set() {
    var it = arguments[0];
    if (!(this instanceof Set)) throw new TypeError("Constructor Set requires 'new'");
    hid(this, '%m', new MapImpl(true));
    if (it != null) { var a = toArr(it); for (var i = 0; i < a.length; i++) this.add(a[i]); }
}
function M(o) { var m = o && o['%m']; if (!m) throw new TypeError('incompatible receiver'); return m; }
var MP = Map.prototype, SetP = Set.prototype;
hid(MP, 'get', function (k) { var m = M(this), i = find(m, k, false); return i < 0 ? undefined : m.v[i]; });
hid(MP, 'set', function (k, v) { add(M(this), k, v); return this; });
hid(MP, 'has', function (k) { return find(M(this), k, false) >= 0; });
hid(MP, 'delete', function (k) { return del(M(this), k); });
hid(MP, 'clear', function () { hid(this, '%m', new MapImpl(false)); });
hid(MP, 'forEach', function (f, t) { var m = M(this); for (var i = 0; i < m.k.length; i++) if (!(m.dead && m.dead[i])) f.call(t, m.v[i], m.k[i], this); });
hid(MP, 'keys', function () { return new CollIter(M(this), 0); });
hid(MP, 'values', function () { return new CollIter(M(this), 1); });
hid(MP, 'entries', function () { return new CollIter(M(this), 2); });
hid(MP, ITER, MP.entries);
defp(MP, 'size', { get: function () { return M(this).n; }, configurable: true });
hid(MP, '@@toStringTag', 'Map');
hid(SetP, 'add', function (k) { add(M(this), k, k); return this; });
hid(SetP, 'has', MP.has);
hid(SetP, 'delete', MP['delete']);
hid(SetP, 'clear', function () { hid(this, '%m', new MapImpl(true)); });
hid(SetP, 'forEach', function (f, t) { var m = M(this); for (var i = 0; i < m.k.length; i++) if (!(m.dead && m.dead[i])) f.call(t, m.k[i], m.k[i], this); });
hid(SetP, 'values', function () { return new CollIter(M(this), 0); });
hid(SetP, 'keys', SetP.values);
hid(SetP, 'entries', function () { return new CollIter(M(this), 2); });
hid(SetP, ITER, SetP.values);
defp(SetP, 'size', { get: function () { return M(this).n; }, configurable: true });
hid(SetP, '@@toStringTag', 'Set');
def(Map, 'groupBy', function (it, f) { var m = new Map(), a = toArr(it); for (var i = 0; i < a.length; i++) { var k = f(a[i], i); if (!m.has(k)) m.set(k, []); m.get(k).push(a[i]); } return m; });
// WeakMap / WeakSet: a hidden property on the key (nothing is held by the map)
var wmN = 0;
function WeakMap() {
    var it = arguments[0];
    if (!(this instanceof WeakMap)) throw new TypeError("Constructor WeakMap requires 'new'");
    hid(this, '%w', '@@wm' + (++wmN));
    if (it != null) { var a = toArr(it); for (var i = 0; i < a.length; i++) this.set(a[i][0], a[i][1]); }
}
function wkey(k) { if (!isObj(k)) throw new TypeError('Invalid value used as weak map key'); return k; }
var WMP = WeakMap.prototype;
hid(WMP, 'get', function (k) { return isObj(k) && hasOwn.call(k, this['%w']) ? k[this['%w']] : undefined; });
hid(WMP, 'set', function (k, v) {
    wkey(k);
    if (hasOwn.call(k, this['%w'])) k[this['%w']] = v;
    else if (!Object.isExtensible || Object.isExtensible(k)) hid(k, this['%w'], v);
    else { if (!this['%f']) hid(this, '%f', new Map()); this['%f'].set(k, v); }
    return this;
});
hid(WMP, 'has', function (k) { return isObj(k) && (hasOwn.call(k, this['%w']) || !!(this['%f'] && this['%f'].has(k))); });
hid(WMP, 'delete', function (k) { if (isObj(k) && hasOwn.call(k, this['%w'])) { delete k[this['%w']]; return true; } return !!(this['%f'] && this['%f']['delete'](k)); });
function WeakSet() {
    var it = arguments[0];
    if (!(this instanceof WeakSet)) throw new TypeError("Constructor WeakSet requires 'new'");
    WeakMap.call(this);
    if (it != null) { var a = toArr(it); for (var i = 0; i < a.length; i++) this.add(a[i]); }
}
WeakSet.prototype = Object.create(WMP);
hid(WeakSet.prototype, 'constructor', WeakSet);
hid(WeakSet.prototype, 'add', function (k) { WMP.set.call(this, k, true); return this; });
function WeakRef(o) { hid(this, '%o', o); }
hid(WeakRef.prototype, 'deref', function () { return this['%o']; });
function FinalizationRegistry() {}
hid(FinalizationRegistry.prototype, 'register', function () {});
hid(FinalizationRegistry.prototype, 'unregister', function () { return false; });
if (typeof G.Map !== 'function') hid(G, 'Map', Map);
if (typeof G.Set !== 'function') hid(G, 'Set', Set);
if (typeof G.WeakMap !== 'function') hid(G, 'WeakMap', WeakMap);
if (typeof G.WeakSet !== 'function') hid(G, 'WeakSet', WeakSet);
if (typeof G.WeakRef !== 'function') hid(G, 'WeakRef', WeakRef);
if (typeof G.FinalizationRegistry !== 'function') hid(G, 'FinalizationRegistry', FinalizationRegistry);
//@end
//@lazy Promise queueMicrotask %runJobs %unhandled %async %abox %asyncgen
// ---- Promise (jobs run by %runJobs, which the host calls after each script and callback)
var jobs = [], jobHead = 0;
function queue(f, a, b) { jobs.push(f, a, b); }
hid(G, '%runJobs', function (max) {
    var n = 0;
    max = max || 100000;
    while (jobHead < jobs.length && n++ < max) {
        var f = jobs[jobHead], a = jobs[jobHead + 1], b = jobs[jobHead + 2];
        jobs[jobHead] = jobs[jobHead + 1] = jobs[jobHead + 2] = undefined;
        jobHead += 3;
        f(a, b);
    }
    if (jobHead >= jobs.length) { jobs.length = 0; jobHead = 0; }
    return jobs.length - jobHead;
});
var unhandled = [];
hid(G, '%unhandled', function () { var r = unhandled; unhandled = []; return r; });
function Promise(ex) {
    if (!(this instanceof Promise)) throw new TypeError("Promise constructor cannot be invoked without 'new'");
    if (typeof ex !== 'function') throw new TypeError('Promise resolver ' + ex + ' is not a function');
    hid(this, '%s', 0); hid(this, '%v', undefined); hid(this, '%q', []); hid(this, '%h', false);
    var r = resolvers(this);
    try { ex(r[0], r[1]); } catch (e) { r[1](e); }
}
function resolvers(p) {
    var done = false;
    return [function (v) { if (!done) { done = true; resolve(p, v); } },
            function (e) { if (!done) { done = true; settle(p, 2, e); } }];
}
function resolve(p, v) {
    if (v === p) return settle(p, 2, new TypeError('Chaining cycle detected for promise'));
    if (isObj(v)) {
        var then;
        try { then = v.then; } catch (e) { return settle(p, 2, e); }
        if (typeof then === 'function') {
            var r = resolvers(p);
            queue(function () { try { then.call(v, r[0], r[1]); } catch (e) { r[1](e); } });
            return;
        }
    }
    settle(p, 1, v);
}
function settle(p, s, v) {
    if (p['%s']) return;
    p['%s'] = s; p['%v'] = v;
    var q = p['%q']; p['%q'] = null;
    if (s === 2 && !q.length) { unhandled.push(p); }
    for (var i = 0; i < q.length; i++) queue(react, p, q[i]);
}
function react(p, h) {
    var f = p['%s'] === 1 ? h.ok : h.err, v = p['%v'];
    if (typeof f !== 'function') { if (p['%s'] === 1) resolve(h.p, v); else settle(h.p, 2, v); return; }
    try { v = f(v); } catch (e) { settle(h.p, 2, e); return; }
    resolve(h.p, v);
}
var PP = Promise.prototype;
function noop() {}
hid(PP, 'then', function (ok, err) {
    var p = new Promise(noop), h = { ok: ok, err: err, p: p }, C = this.constructor, S, cap;
    if (C !== Promise && isObj(C)) {   /* a subclass's then makes its own kind (@@species; core-js tests it) */
        S = C['@@species'];
        if (typeof S === 'function' && S !== Promise) {
            var res, rej;
            cap = new S(function (a, b) { res = a; rej = b; });
            p['%q'].push({ ok: function (v) { res(v); }, err: function (e) { rej(e); }, p: new Promise(noop) });
            p['%h'] = true;
        }
    }
    if (this['%s'] === 2 && !this['%h']) { var i = unhandled.indexOf(this); if (i >= 0) unhandled.splice(i, 1); }
    this['%h'] = true;
    if (this['%s']) queue(react, this, h); else this['%q'].push(h);
    return cap || p;
});
defp(Promise, '@@species', { get: function () { return this; }, configurable: true });
hid(PP, 'catch', function (err) { return this.then(undefined, err); });
hid(PP, 'finally', function (f) {
    return this.then(function (v) { return Promise.resolve(f()).then(function () { return v; }); },
                     function (e) { return Promise.resolve(f()).then(function () { throw e; }); });
});
hid(PP, '@@toStringTag', 'Promise');
function PC(c) { return typeof c === 'function' && c !== Promise ? c : Promise; }   /* (a subclass: its own kind) */
hid(Promise, 'resolve', function (v) { var C = PC(this); if (v instanceof C && v.constructor === C) return v; return new C(function (r) { r(v); }); });
hid(Promise, 'reject', function (e) { return new (PC(this))(function (r, j) { j(e); }); });
hid(Promise, 'withResolvers', function () { var o = {}; o.promise = new Promise(function (a, b) { o.resolve = a; o.reject = b; }); return o; });
function combine(it, onv, one, all, fin) {
    return new Promise(function (res, rej) {
        var a = toArr(it), n = a.length, out = new Array(n), left = n;
        if (!n) return fin(res, rej, out);
        for (var i = 0; i < n; i++) (function (i) {
            Promise.resolve(a[i]).then(function (v) { onv(i, v, out, res, rej, function () { if (--left === 0) fin(res, rej, out); }); },
                                       function (e) { one(i, e, out, res, rej, function () { if (--left === 0) fin(res, rej, out); }); });
        })(i);
    });
}
hid(Promise, 'all', function (it) {
    return combine(it, function (i, v, o, res, rej, d) { o[i] = v; d(); }, function (i, e, o, res, rej) { rej(e); }, 0, function (res, rej, o) { res(o); });
});
hid(Promise, 'allSettled', function (it) {
    return combine(it, function (i, v, o, res, rej, d) { o[i] = { status: 'fulfilled', value: v }; d(); },
                   function (i, e, o, res, rej, d) { o[i] = { status: 'rejected', reason: e }; d(); }, 0, function (res, rej, o) { res(o); });
});
hid(Promise, 'race', function (it) {
    return new Promise(function (res, rej) { var a = toArr(it); for (var i = 0; i < a.length; i++) Promise.resolve(a[i]).then(res, rej); });
});
hid(Promise, 'any', function (it) {
    return combine(it, function (i, v, o, res) { res(v); }, function (i, e, o, res, rej, d) { o[i] = e; d(); }, 0,
                   function (res, rej, o) { var e = new Error('All promises were rejected'); e.errors = o; rej(e); });
});
if (typeof G.Promise !== 'function') hid(G, 'Promise', Promise);
def(G, 'queueMicrotask', function (f) { queue(function () { f(); }); });

// an async function's body runs as a coroutine: await yields the awaited value
hid(G, '%async', function (g) {
    return new Promise(function (res, rej) {
        function step(mode, v) {
            var r;
            try { r = g['%co'](mode, v); } catch (e) { rej(e); return; }
            if (r[1]) { res(r[0]); return; }
            Promise.resolve(r[0]).then(function (x) { step(0, x); }, function (e) { step(1, e); });
        }
        step(0, undefined);
    });
});
// async generators: await in their body yields a box (%abox), their yields are the values
function ABox(v) { this.v = v; }
hid(G, '%abox', function (v) { return new ABox(v); });
function AsyncGen(g) { hid(this, '%g', g); hid(this, '%q', []); hid(this, '%busy', false); }
var AGP = AsyncGen.prototype;
function agrun(a) {
    if (a['%busy'] || !a['%q'].length) return;
    var req = a['%q'].shift(), g = a['%g'];
    a['%busy'] = true;
    function step(mode, v) {
        var r;
        try { r = g['%co'](mode, v); } catch (e) { a['%busy'] = false; req.rej(e); agrun(a); return; }
        if (!r[1] && r[0] instanceof ABox) {
            Promise.resolve(r[0].v).then(function (x) { step(0, x); }, function (e) { step(1, e); });
            return;
        }
        if (r[1]) { a['%busy'] = false; req.res(iterResult(r[0], true)); agrun(a); return; }
        Promise.resolve(r[0]).then(function (x) { a['%busy'] = false; req.res(iterResult(x, false)); agrun(a); },
                                   function (e) { step(1, e); });
    }
    step(req.mode, req.v);
}
function agreq(a, mode, v) {
    return new Promise(function (res, rej) { a['%q'].push({ mode: mode, v: v, res: res, rej: rej }); agrun(a); });
}
hid(AGP, 'next', function (v) { return agreq(this, 0, v); });
hid(AGP, 'throw', function (e) { return agreq(this, 1, e); });
hid(AGP, 'return', function (v) { return agreq(this, 2, v); });
hid(AGP, AITER, function () { return this; });
hid(AGP, '@@toStringTag', 'AsyncGenerator');
hid(G, '%asyncgen', function (g) { return new AsyncGen(g); });
//@end
