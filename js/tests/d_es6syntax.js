(function () {
var out = [];
function p(x) { out.push(typeof x === 'object' ? JSON.stringify(x) : String(x)); }
// let / const, block shadowing
let a = 1; const b = 2;
{ let a = 10; p(a + b); }
p(a);
for (let i = 0; i < 2; i++) { const a = i * 5; p(a); }
p(a);
// arrows and this
var o = { v: 7, f: function () { return [1, 2].map(x => x * this.v); }, g: function () { return (() => arguments[0])(); } };
p(o.f()); p(o.g(42));
var add = (x, y = 3) => x + y; p(add(1)); p(add(1, 1));
var obj = () => ({ k: 1 }); p(obj());
// templates
var n = 5; p(`n=${n} next=${n + 1}${''} ${`in${n}`}`);
function tag(s, ...v) { return s.raw.join('|') + ':' + v.join(','); }
p(tag`a${1}b\n${2}c`);
// spread, rest, destructuring
function sum(...xs) { return xs.reduce((s, x) => s + x, 0); }
p(sum(...[1, 2, 3], 4));
var [x, , y = 9, ...rest] = [1, 2, undefined, 4, 5]; p([x, y, rest]);
var { q, r: { s = 'd' } = {}, ...others } = { q: 1, t: 2, u: 3 }; p([q, s, others]);
var m = 1, k = 2; [m, k] = [k, m]; p([m, k]);
function pt({ x, y } = { x: 0, y: 0 }, [z] = [3]) { return x + y + z; } p(pt()); p(pt({ x: 1, y: 2 }, [4]));
p([...'héllo'].length); p([0, ...[1, 2], 3]);
p(Math.max(...[3, 9, 1]));
var d = new Date(...[2020, 1, 2]); p(d.getMonth());
// object literal
var key = 'dyn', sh = 4;
var lit = { sh, [key + 1]: 2, meth() { return this.sh; }, get gg() { return 5; }, ...{ z1: 1 }, 'q-q': 3 };
p(lit); p(lit.meth()); p(lit.gg);
// ?. ?? ** and logical assignment
var cfg = { a: { b: null, f() { return 'F'; } } };
p(cfg?.a?.b?.c); p(cfg.x?.y.z); p(cfg.a.f?.()); p(cfg.a.g?.()); p(cfg?.['a']?.f());
p(null ?? 'def'); p(0 ?? 'def'); p(2 ** 10); p((-2) ** 3);
var la = null; la ??= 5; var lb = 0; lb ||= 6; var lc = 1; lc &&= 7; p([la, lb, lc]);
// for-of
var acc = []; for (const [i, v] of [['a', 1], ['b', 2]].entries()) acc.push(i + v[0] + v[1]); p(acc);
for (var ch of 'ab') acc.push(ch); p(acc);
var outer = 0; lab: for (const v of [1, 2, 3]) { for (const w of [1, 2]) { if (w == 2) continue lab; outer += v; } } p(outer);
// numbers
p([0b101, 0o17, 1_000_000, 0xFF]);
// classes
class Animal {
  static count = 0;
  #secret = 'hid';
  legs = 4;
  constructor(name) { this.name = name; Animal.count++; }
  speak() { return `${this.name} makes a sound`; }
  get secret() { return this.#secret; }
  static create(n) { return new this(n); }
}
class Dog extends Animal {
  sound = 'woof';
  constructor(name) { super(name); this.kind = 'dog'; }
  speak() { return super.speak() + ' ' + this.sound; }
  toString() { return 'Dog ' + this.name; }
}
var dg = new Dog('Rex'); p(dg.speak()); p(dg.legs); p(dg.secret); p(Animal.count); p(dg instanceof Animal); p(String(dg));
p(Object.keys(dg).sort()); p(Animal.create('Cat').speak());
class E2 extends Error { constructor(m) { super(m); this.name = 'E2'; } }
try { throw new E2('boom'); } catch (e) { p(e instanceof E2 && e instanceof Error); p(e.message); }
try { null.x; } catch { p('caught'); }
try { throw { code: 3 }; } catch ({ code }) { p(code); }
var C3 = class { m() { return 'anon'; } }; p(new C3().m());
// arrows in class fields keep this
class Btn { label = 'ok'; click = () => this.label; } var bt = new Btn(); var cl = bt.click; p(cl());
return out.join(';');
})()
