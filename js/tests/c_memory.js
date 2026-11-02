(function () {
    var t = 0, keep = [];
    for (var i = 0; i < 200; i++) {
        var a = [];
        for (var j = 0; j < 100; j++) a.push({ v: j, s: 's' + j });
        t += a.length;
        if (i % 50 == 0) keep.push(a);
    }
    var big = new Array(20001).join('x');
    return t + ' ' + keep.length + ' ' + big.length;
})()
