(function () {
    var d = new Date(2026, 0, 31, 13, 45, 7);
    d.setMonth(1);
    var u = new Date(Date.UTC(2000, 1, 29, 12, 0, 0));
    return [d.getFullYear(), d.getMonth(), d.getDate(), d.getHours(), d.getMinutes(), d.getDay(), u.toISOString(),
            u.getUTCDay(), Date.UTC(1970, 0, 2), typeof Date.now(), new Date(0).getTime(), new Date(2026, 10, 2) < new Date(2026, 10, 3)].join(' ');
})()
