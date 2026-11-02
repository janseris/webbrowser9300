(function () { var s = 0; for (var i = 0; i < 20000; i++) { s = (s + i * i) % 1000003; } return s; })()
