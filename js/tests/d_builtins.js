(function () {
var o = [];
function p(x) { o.push(typeof x === 'object' ? JSON.stringify(x) : String(x)); }
p(Object.fromEntries([['a', 1], ['b', 2]])); p(Object.entries({ x: 1, y: [2] }));
p([1, [2, [3, [4]]]].flat()); p([1, [2, [3, [4]]]].flat(Infinity)); p([1, 2].flatMap(x => [x, x * 10]));
p([5, 6, 7].at(-1)); p('abc'.at(-2)); p('a-b-c'.replaceAll('-', '+')); p('x.y'.replaceAll('.', '$&'));
p([3, 1, 2].toSorted()); p(Array.from({ length: 3 }, (_, i) => i * i)); p(Array.from(new Set('hello')).join(''));
p(Object.is(NaN, NaN)); p(Object.is(0, -0)); p(Number.isInteger(5.0)); p(Math.trunc(-4.7)); p(Math.sign(-3));
p(String.fromCodePoint(0x1F600).length); p('😀'.codePointAt(0)); p(String.raw`a\nb${1}`);
p([...'a😀b'].length); p('abc'.padStart(5, '*')); p('  x '.trimEnd() + '|');
var m = new Map(); m.set(NaN, 'nan').set(0, 'zero').set('0', 'str'); p([m.get(NaN), m.get(-0), m.get('0'), m.size]);
var k1 = {}, k2 = {}; m.set(k1, 1).set(k2, 2); m.delete(k1); p([m.has(k1), m.get(k2), [...m.values()].length]);
var s = new Set([1, 2]); s.add(2).add(3); s.delete(1); var it = []; s.forEach(v => it.push(v)); p(it);
var sym = Symbol('d'), so = { [sym]: 5 }; p(so[sym]); p(typeof Symbol.iterator); p(Symbol.for('k') === Symbol.for('k'));
var gen = Object.entries({ a: 1 })[Symbol.iterator](); p(gen.next());
p(Array.prototype.includes.call([NaN], NaN)); p([1, 2, 3].findLast(x => x < 3));
var grp = Object.groupBy([1, 2, 3, 4], x => x % 2 ? 'odd' : 'even'); p(grp);
p(new Map(Object.entries({ q: 1 })).get('q'));
p(JSON.stringify({ b: 1, a: 2, 1: 3 })); var ord = {}; ord.z = 1; ord.y = 2; delete ord.z; ord.z = 3; p(Object.keys(ord));
// regexp flags y (sticky), s (dotAll), u
(function () { var re = /\d+/y, s = 'ab12 34';
re.lastIndex = 2; p([re.exec(s)[0], re.lastIndex]); p(re.exec(s)); p(re.lastIndex);
var re2 = new RegExp('a.c', 'sy'); p([re2.test('a\nc'), re2.flags, String(re2), re2.sticky, re2.dotAll, /a.c/.test('a\nc'), new RegExp('x', 'gu').flags]);
var t = /foo|bar/y, out = [], m; while ((m = t.exec('foobarbaz'))) out.push(m[0]); p([out.join('+'), t.lastIndex, 'xaxb'.replace(/x/y, '-')]); })();
(function () { var r = [], s = '\u010d\u0159 ab\u2014cd \ud83d\ude00x ab', g = /ab/g, m; while ((m = g.exec(s))) r.push(m.index + ':' + g.lastIndex);
var y = /\w+/y; y.lastIndex = 3; r.push(y.exec(s)[0], y.lastIndex); var t = /x/g; r.push(t.test(s), t.lastIndex); p(r); })();
p([[5].reduce(function (a, b) { return a + b; }), [5].reduceRight(function (a, b) { return a + b; }), [, , 7].reduce(function (a, b) { return a + b; }), [1, 2, 3].reduceRight(function (a, b) { return a + '-' + b; })]);
// what polyfill bundles test before they replace a built-in
p([Symbol.length, Map.length, Set.length, WeakMap.length, String.fromCodePoint.length, 'name' in Function.prototype, (function foo() {}).name, Math.max.name,
   Object.keys('ab').join(), Object.seal('1'), Object.getOwnPropertyNames(1).length, Object.getOwnPropertyDescriptor('13.7', 1).value,
   JSON.stringify('\t\n\x0B\f\r \xa0       　﻿'.trim()), '​᠎'.trim().length,
   JSON.stringify(' 　a b  '.trimStart()), JSON.stringify(' 　a b  '.trimEnd()), 1 / parseFloat('　﻿-0'), Number('　 12  '), Math.acosh(1)]);
(function () { var e = Promise.resolve(1), t = function (x) { x(function () {}, function () {}); }; (e.constructor = {})[Symbol.species] = t;
class P2 extends Promise {} p([e.then(function () {}) instanceof t, Promise[Symbol.species] === Promise, P2.resolve(2).then(function (v) { return v; }) instanceof P2]); })();
return o.join(';');
})()
