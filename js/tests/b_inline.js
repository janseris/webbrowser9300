var doc = { els: {}, getElementById: function (id) { return this.els[id] || (this.els[id] = { id: id, style: { display: 'none' }, className: '', innerHTML: '' }); } };
function toggle(id) { var e = doc.getElementById(id); e.style.display = e.style.display == 'none' ? 'block' : 'none'; e.className = e.style.display == 'block' ? 'open' : ''; }
function setupMenu(n) { var html = ''; for (var i = 0; i < n; i++) html += '<li><a href="#s' + i + '" onclick="toggle(\'m' + i + '\')">Item ' + i + '</a></li>'; doc.getElementById('menu').innerHTML = '<ul>' + html + '</ul>'; }
function validate(form) { var ok = true; for (var k in form) { if (!form[k] || form[k].length < 2) ok = false; } return ok; }
setupMenu(30);
for (var k = 0; k < 30; k++) toggle('m' + k);
var d = new Date(2026, 10, 2);
var s = 'Today ' + (d.getMonth() + 1) + '/' + d.getDate();
doc.getElementById('menu').innerHTML.length + s.length + (validate({ name: 'Jan', mail: 'jan@example.cz' }) ? 1 : 0)
