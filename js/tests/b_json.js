(function () {
var a = []; for (var i = 0; i < 1000; i++) a.push({ id: i, name: 'Item ' + i, price: i * 1.5, tags: ['a', 'b'], ok: i % 2 == 0 });
var t = JSON.stringify(a); var b = JSON.parse(t);
return t.length + b.length + b[999].price; })()
