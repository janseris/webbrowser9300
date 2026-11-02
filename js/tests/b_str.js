(function () {
var a = []; for (var i = 0; i < 5000; i++) a.push('item' + i);
var s = a.join(','); var p = s.split(','); var t = s.replace(/item(\d+)/g, 'x$1');
return p.length + t.length + s.indexOf('item4999'); })()
