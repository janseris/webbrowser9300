(function () {
var o = []; for (var i = 0; i < 10000; i++) o.push({ a: i, b: 'x' + (i % 100), c: [i, i + 1] });
var n = []; for (var j = 0; j < 3000; j++) n.push((j * 7919) % 3001);
n.sort(function (x, y) { return x - y; });
var m = {}; for (var q = 0; q < o.length; q++) m[o[q].b] = (m[o[q].b] || 0) + 1;
return n[0] + n[2999] + Object.keys(m).length; })()
