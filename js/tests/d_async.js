(function () {
var log = [];
async function aw(x) { log.push('start ' + x); const v = await Promise.resolve(x * 2); log.push('got ' + v); return v + 1; }
async function chain() { try { const a = await aw(1); const b = await aw(a); await Promise.reject(new Error('bad')); } catch (e) { log.push('err ' + e.message); } return 'done'; }
chain().then(v => log.push('then ' + v));
log.push('sync');
const fa = async () => { await null; return typeof this; }; fa().then(v => log.push('arrow ' + v));
Promise.all([1, Promise.resolve(2), new Promise(r => r(3))]).then(v => log.push('all ' + v));
Promise.allSettled([Promise.reject(1), 2]).then(v => log.push('settled ' + JSON.stringify(v)));
Promise.race([new Promise(() => {}), Promise.resolve('r')]).then(v => log.push('race ' + v));
Promise.any([Promise.reject(1), Promise.resolve('any')]).then(v => log.push(v));
new Promise((_, j) => j('x')).catch(e => 'c' + e).finally(() => log.push('fin')).then(v => log.push(v));
queueMicrotask(() => log.push('micro'));
class Q { async load() { await 0; return [1, 2].map(async x => x * 3); } }
new Q().load().then(ps => Promise.all(ps)).then(v => log.push('class ' + v));
async function seq() { var r = []; for (const x of [1, 2, 3]) r.push(await x); return r; }
seq().then(v => log.push('seq ' + v));
async function thrower() { throw new TypeError('t'); }
thrower().catch(e => log.push(e.name));
if (this['%runJobs']) this['%runJobs']();
return log.join(';');
})()
