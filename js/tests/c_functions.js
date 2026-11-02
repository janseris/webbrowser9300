(function () {
    var fs = [];
    for (var i = 0; i < 5; i++) fs.push((function (j) { return function () { return j * j; }; })(i));
    function fact(n) { return n <= 1 ? 1 : n * fact(n - 1); }
    function depth(n) { return n ? depth(n - 1) + 1 : 0; }
    function Counter() { this.n = 0; }
    Counter.prototype.inc = function () { this.n++; return this; };
    var c = new Counter().inc().inc();
    var add = function (a, b) { return a + b; };
    return [fs.map(function (f) { return f(); }).join(','), fact(20), depth(150), c.n, c instanceof Counter,
            add.apply(null, [2, 3]), add.call(null, 4, 5), (function () { return arguments.length; })(1, 2, 3),
            typeof Function.prototype.bind == 'function' ? add.bind(null, 10)(1) : 'nobind'].join(' ');
})()
