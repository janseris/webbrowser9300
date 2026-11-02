(function () {
    var o = { b: 2, a: 1 }; o.c = 3; delete o.b;
    var k = []; for (var p in o) k.push(p);
    var arr = [5, 1, 10, 2];
    return [k.join(','), Object.keys([5, 6, 7]).length, 'a' in o, o.hasOwnProperty('b'), arr.slice().sort().join(''),
            arr.sort(function (x, y) { return x - y; }).join(','), [1, 2, 3].indexOf(2), Array.isArray([]), typeof null,
            typeof function () {}, [1, [2, [3]]].toString(), [1, 2, 3, 4].filter(function (x) { return x % 2; }).join(''),
            [1, 2, 3].reduce(function (a, b) { return a + b; }, 0), String([].concat([1], 2, [[3]])), JSON.stringify({ a: [1, 'two', { b: null }], c: true, d: 1.5e-7 }),
            JSON.parse('{"x":[1,2,{"y":"z\\u010d"}]}').x[2].y, Object.prototype.toString.call([])].join(' ');
})()
