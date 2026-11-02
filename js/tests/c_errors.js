(function () {
    var r = [];
    try { null.x; } catch (e) { r.push(e instanceof TypeError); }
    try { throw new Error('boom'); } catch (e) { r.push(e.message); } finally { r.push('fin'); }
    try { undefinedVar; } catch (e) { r.push(e.name); }
    try { (function () { throw { code: 7 }; })(); } catch (e) { r.push(e.code); }
    try { JSON.parse('{bad'); } catch (e) { r.push(e.name); }
    function deep(n) { if (n == 0) throw new RangeError('bottom'); return deep(n - 1); }
    try { deep(50); } catch (e) { r.push(e.message); }
    return r.join(',');
})()
