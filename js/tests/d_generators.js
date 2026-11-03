(function () {
var out = [];
function p(x) { out.push(typeof x === 'object' ? JSON.stringify(x) : String(x)); }
function* count(n) { for (let i = 0; i < n; i++) { const r = yield i; if (r) p('got ' + r); } return 'end'; }
var g = count(3); p(g.next()); p(g.next('x')); p(g.next()); p(g.next()); p(g.next());
p([...count(4)]);
function* inner() { yield 'a'; yield 'b'; return 'ri'; }
function* outer() { const r = yield* inner(); yield r; yield* [1, 2]; }
p(Array.from(outer()));
function* tc() { try { yield 1; yield 2; } catch (e) { p('caught ' + e); yield 'after'; } finally { p('fin'); } }
var t = tc(); p(t.next()); p(t.throw('E')); p(t.next());
var it = { *[Symbol.iterator]() { yield 10; yield 20; } }; for (const v of it) p(v);
class Tree { constructor(v, k) { this.v = v; this.k = k || []; } *walk() { yield this.v; for (const c of this.k) yield* c.walk(); } }
p([...new Tree(1, [new Tree(2), new Tree(3, [new Tree(4)])]).walk()]);
var fib = function* () { let [a, b] = [0, 1]; while (true) { yield a; [a, b] = [b, a + b]; } };
var f = fib(), fs = []; for (let i = 0; i < 10; i++) fs.push(f.next().value); p(fs);
var m = new Map([['a', 1], ['b', 2]]); m.set('c', 3); m.delete('a'); p([...m]); p(m.size); p([...m.keys()]);
var s = new Set([1, 2, 2, 3]); p([...s]); p(s.has(2));
var wm = new WeakMap(), ko = {}; wm.set(ko, 5); p(wm.get(ko)); p(Object.keys(ko).length);
return out.join(";");
})()
