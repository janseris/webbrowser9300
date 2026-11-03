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
return o.join(';');
})()
