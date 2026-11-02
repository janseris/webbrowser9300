(function () {
    function d(n) { return n ? d(n - 1) + 1 : 0; }
    var r = [d(300)], i, src = '', a = [1];
    for (i = 0; i < 100; i++) src += '(';
    src += '1';
    for (i = 0; i < 100; i++) src += ')';
    r.push(eval(src));
    for (i = 0; i < 100; i++) a = [a];
    r.push(JSON.stringify(a).length, JSON.parse(JSON.stringify(a)).length);
    r.push([5, 3, 9, 1].sort(function (x, y) { return [x].map(function (v) { return v; })[0] - y; }).join(''));
    try { (function inf() { inf(); })(); } catch (e) { r.push('caught'); }
    return r.join(' ');
})()
