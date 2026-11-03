(function () {
var log = [];
async function* ticker(n) { for (let i = 0; i < n; i++) { await null; yield i * 10; } return 'end'; }
async function* both() { yield* [1, 2]; const x = await Promise.resolve(3); yield x; }
async function main() {
  for await (const v of ticker(3)) log.push('t' + v);
  for await (const v of [Promise.resolve('p1'), 'p2']) log.push(v);
  const it = both(); log.push(JSON.stringify(await it.next())); log.push(JSON.stringify(await it.next()));
  log.push(JSON.stringify(await it.next())); log.push(JSON.stringify(await it.next()));
  const g = ticker(5); await g.next(); log.push(JSON.stringify(await g.return('r'))); log.push(JSON.stringify(await g.next()));
  return 'done';
}
main().then(v => log.push(v), e => log.push('ERR ' + e));
if (this["%runJobs"]) this["%runJobs"]();
return log.join(';');
})()
