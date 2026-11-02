// Browser 9300: what a page's scripts find before they run (js/bwhost.c compiles it into every
// page's engine; js/genprelude.py makes prelude.h from it). The host sets __href, __ua, __ref,
// __lang, __w and __h first and gives __write (document.write). No DOM yet: the elements are
// dummies that take any call, so a script that builds DOM does nothing instead of stopping.
// ES5 only (MuJS): no arrow functions, let / const, classes.

// ---- the methods newer than ES5 (or Annex B) that pages use most
(function () {
    function def(o, name, f) { if (!o[name]) Object.defineProperty(o, name, { value: f, writable: true, configurable: true, enumerable: false }); }
    var SP = String.prototype, AP = Array.prototype;
    def(SP, 'substr', function (start, len) {
        var s = String(this), n = s.length;
        start = start | 0; if (start < 0) start = Math.max(n + start, 0);
        len = len === undefined ? n - start : len | 0;
        return len <= 0 ? '' : s.substring(start, start + len);
    });
    def(SP, 'trimLeft', function () { return String(this).replace(/^\s+/, ''); });
    def(SP, 'trimRight', function () { return String(this).replace(/\s+$/, ''); });
    def(SP, 'trimStart', SP.trimLeft);
    def(SP, 'trimEnd', SP.trimRight);
    def(SP, 'startsWith', function (t, p) { p = p | 0; t = String(t); return String(this).substring(p, p + t.length) === t; });
    def(SP, 'endsWith', function (t, e) { var s = String(this); t = String(t); e = e === undefined ? s.length : e | 0; return e - t.length >= 0 && s.substring(e - t.length, e) === t; });
    def(SP, 'includes', function (t, p) { return String(this).indexOf(t, p) >= 0; });
    def(SP, 'repeat', function (n) { var s = String(this), r = ''; n = n | 0; if (n < 0) throw new RangeError('Invalid count value'); while (n > 0) { if (n & 1) r += s; n >>= 1; s += s; } return r; });
    function pad(s, n, f, atStart) {
        s = String(s); n = n | 0; f = f === undefined ? ' ' : String(f);
        if (n <= s.length || f === '') return s;
        var fill = ''; while (fill.length < n - s.length) fill += f;
        fill = fill.substring(0, n - s.length);
        return atStart ? fill + s : s + fill;
    }
    def(SP, 'padStart', function (n, f) { return pad(this, n, f, true); });
    def(SP, 'padEnd', function (n, f) { return pad(this, n, f, false); });
    def(SP, 'anchor', function (n) { return '<a name="' + n + '">' + this + '</a>'; });
    def(SP, 'bold', function () { return '<b>' + this + '</b>'; });
    def(SP, 'italics', function () { return '<i>' + this + '</i>'; });
    def(SP, 'link', function (u) { return '<a href="' + u + '">' + this + '</a>'; });
    def(SP, 'fontcolor', function (c) { return '<font color="' + c + '">' + this + '</font>'; });
    def(AP, 'find', function (f, t) { for (var i = 0; i < this.length; i++) if (f.call(t, this[i], i, this)) return this[i]; });
    def(AP, 'findIndex', function (f, t) { for (var i = 0; i < this.length; i++) if (f.call(t, this[i], i, this)) return i; return -1; });
    def(AP, 'includes', function (v, from) {
        for (var i = from | 0; i < this.length; i++) if (this[i] === v || (v !== v && this[i] !== this[i])) return true;
        return false;
    });
    def(AP, 'fill', function (v, s, e) {
        var n = this.length; s = s | 0; e = e === undefined ? n : e | 0;
        if (s < 0) s += n; if (e < 0) e += n;
        for (var i = s; i < e && i < n; i++) this[i] = v;
        return this;
    });
    def(Array, 'from', function (a, f, t) {
        var r = [], i;
        if (a == null) throw new TypeError('Array.from: not an object');
        if (typeof a === 'string') a = a.split('');
        for (i = 0; i < a.length; i++) r.push(f ? f.call(t, a[i], i) : a[i]);
        return r;
    });
    def(Array, 'of', function () { return AP.slice.call(arguments); });
    def(Object, 'assign', function (to) {
        if (to == null) throw new TypeError('Object.assign: not an object');
        to = Object(to);
        for (var i = 1; i < arguments.length; i++) {
            var s = arguments[i];
            if (s != null) for (var k in s) if (Object.prototype.hasOwnProperty.call(s, k)) to[k] = s[k];
        }
        return to;
    });
    def(Object, 'values', function (o) { return Object.keys(o).map(function (k) { return o[k]; }); });
    def(Object, 'entries', function (o) { return Object.keys(o).map(function (k) { return [k, o[k]]; }); });
    def(Number, 'isNaN', function (v) { return typeof v === 'number' && v !== v; });
    def(Number, 'isFinite', function (v) { return typeof v === 'number' && isFinite(v); });
    def(Number, 'isInteger', function (v) { return typeof v === 'number' && isFinite(v) && Math.floor(v) === v; });
    def(Number, 'parseFloat', parseFloat);
    def(Number, 'parseInt', parseInt);
    def(Number, 'EPSILON', Math.pow(2, -52));
    def(Number, 'MAX_SAFE_INTEGER', 9007199254740991);
    def(Number, 'MIN_SAFE_INTEGER', -9007199254740991);
    def(Math, 'sign', function (x) { x = +x; return x > 0 ? 1 : x < 0 ? -1 : x; });
    def(Math, 'trunc', function (x) { x = +x; return x < 0 ? Math.ceil(x) : Math.floor(x); });
    def(Math, 'log10', function (x) { return Math.log(x) / Math.LN10; });
    def(Math, 'log2', function (x) { return Math.log(x) / Math.LN2; });
    def(Math, 'cbrt', function (x) { x = +x; return x < 0 ? -Math.pow(-x, 1 / 3) : Math.pow(x, 1 / 3); });
    def(Math, 'hypot', function () { var s = 0; for (var i = 0; i < arguments.length; i++) s += arguments[i] * arguments[i]; return Math.sqrt(s); });
    def(Date, 'now', function () { return new Date().getTime(); });
})();

// escape / unescape (Annex B; old pages use them for cookies)
function escape(s) {
    s = String(s);
    var r = '', ok = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789@*_+-./', h = '0123456789ABCDEF';
    for (var i = 0; i < s.length; i++) {
        var c = s.charCodeAt(i);
        if (ok.indexOf(s.charAt(i)) >= 0) r += s.charAt(i);
        else if (c < 256) r += '%' + h.charAt(c >> 4) + h.charAt(c & 15);
        else r += '%u' + h.charAt(c >> 12) + h.charAt((c >> 8) & 15) + h.charAt((c >> 4) & 15) + h.charAt(c & 15);
    }
    return r;
}
function unescape(s) {
    s = String(s);
    return s.replace(/%u([0-9a-fA-F]{4})|%([0-9a-fA-F]{2})/g, function (m, u, b) { return String.fromCharCode(parseInt(u || b, 16)); });
}

// ---- the page's world
var window = this, self = this, top = this, parent = this, frames = this, globalThis = this;
var __noop = function () {};

// an element that takes anything (no DOM yet)
function __Element(tag) {
    this.tagName = this.nodeName = String(tag || 'div').toUpperCase();
    this.nodeType = 1;
    this.style = {};
    this.attributes = [];
    this.childNodes = [];
    this.children = [];
    this.className = '';
    this.id = '';
    this.innerHTML = '';
    this.textContent = '';
    this.value = '';
    this.parentNode = null;
    this.dataset = {};
    this.classList = { add: __noop, remove: __noop, toggle: __noop, contains: function () { return false; } };
}
(function (E) {
    var p = E.prototype;
    p.setAttribute = function (n, v) { this['@' + n] = String(v); };
    p.getAttribute = function (n) { var v = this['@' + n]; return v === undefined ? null : v; };
    p.hasAttribute = function (n) { return this['@' + n] !== undefined; };
    p.removeAttribute = function (n) { delete this['@' + n]; };
    p.appendChild = function (c) { this.childNodes.push(c); if (c && typeof c === 'object') c.parentNode = this; return c; };
    p.insertBefore = function (c) { return this.appendChild(c); };
    p.removeChild = function (c) { return c; };
    p.replaceChild = function (n, o) { return o; };
    p.cloneNode = function () { return new E(this.tagName); };
    p.addEventListener = p.removeEventListener = p.attachEvent = p.detachEvent = __noop;
    p.dispatchEvent = function () { return true; };
    p.getElementsByTagName = p.getElementsByClassName = p.querySelectorAll = function () { return []; };
    p.querySelector = p.closest = function () { return null; };
    p.getBoundingClientRect = function () { return { top: 0, left: 0, right: 0, bottom: 0, width: 0, height: 0 }; };
    p.focus = p.blur = p.click = __noop;
    p.offsetWidth = p.offsetHeight = p.clientWidth = p.clientHeight = p.scrollTop = p.scrollLeft = 0;
})(__Element);

var location = (function (href) {
    var m = /^([a-z][a-z0-9+.-]*:)\/\/([^\/?#:]*)(:\d+)?([^?#]*)(\?[^#]*)?(#.*)?$/i.exec(href) || [];
    var l = {
        href: href, protocol: m[1] || '', hostname: m[2] || '', port: m[3] ? m[3].substring(1) : '',
        pathname: m[4] || '/', search: m[5] || '', hash: m[6] || '',
        assign: __noop, replace: __noop, reload: __noop,
        toString: function () { return this.href; }
    };
    l.host = l.hostname + (l.port ? ':' + l.port : '');
    l.origin = l.protocol + '//' + l.host;
    return l;
})(__href);

var navigator = {
    userAgent: __ua, appName: 'Netscape', appCodeName: 'Mozilla', appVersion: __ua.replace(/^Mozilla\//, ''),
    platform: 'Symbian', product: 'Gecko', vendor: '', language: __lang, languages: [__lang],
    cookieEnabled: true, onLine: true, plugins: [], mimeTypes: [], maxTouchPoints: 0,
    javaEnabled: function () { return false; }
};
var screen = { width: __w, height: __h, availWidth: __w, availHeight: __h, colorDepth: 16, pixelDepth: 16 };
var innerWidth = __w, innerHeight = __h, outerWidth = __w, outerHeight = __h, devicePixelRatio = 1;
var pageXOffset = 0, pageYOffset = 0, scrollX = 0, scrollY = 0, name = '', closed = false, opener = null;
var history = { length: 1, back: __noop, forward: __noop, go: __noop, pushState: __noop, replaceState: __noop, state: null };

var document = {
    write: function () { for (var i = 0; i < arguments.length; i++) __write(String(arguments[i])); },
    writeln: function () { for (var i = 0; i < arguments.length; i++) __write(String(arguments[i])); __write('\n'); },
    open: __noop, close: __noop,
    getElementById: function () { return null; },
    getElementsByTagName: function () { return []; },
    getElementsByClassName: function () { return []; },
    getElementsByName: function () { return []; },
    querySelector: function () { return null; },
    querySelectorAll: function () { return []; },
    createElement: function (t) { return new __Element(t); },
    createElementNS: function (ns, t) { return new __Element(t); },
    createTextNode: function (t) { return { nodeType: 3, data: String(t), nodeValue: String(t), textContent: String(t) }; },
    createComment: function (t) { return { nodeType: 8, data: String(t) }; },
    createDocumentFragment: function () { return new __Element('#fragment'); },
    createEvent: function () { return { initEvent: __noop, initCustomEvent: __noop }; },
    addEventListener: __noop, removeEventListener: __noop, attachEvent: __noop, detachEvent: __noop,
    dispatchEvent: function () { return true; },
    hasFocus: function () { return true; },
    cookie: '', referrer: __ref, title: '', readyState: 'loading', compatMode: 'CSS1Compat',
    characterSet: 'UTF-8', charset: 'UTF-8', contentType: 'text/html', visibilityState: 'visible', hidden: false,
    location: location, URL: location.href, documentURI: location.href, domain: location.hostname,
    documentElement: new __Element('html'), head: new __Element('head'), body: new __Element('body'),
    images: [], links: [], forms: [], anchors: [], scripts: [], styleSheets: [], all: undefined
};
document.defaultView = window;

// timers: not run (nothing after the page has been read yet), but scripts get their ids
var __timer = 0;
function setTimeout() { return ++__timer; }
function setInterval() { return ++__timer; }
function clearTimeout() {}
function clearInterval() {}
function requestAnimationFrame() { return ++__timer; }
function cancelAnimationFrame() {}
function addEventListener() {}
function removeEventListener() {}
function attachEvent() {}
function dispatchEvent() { return true; }
function alert(m) { __say('alert: ' + m); }
function confirm(m) { __say('confirm: ' + m); return false; }
function prompt(m) { __say('prompt: ' + m); return null; }
function open() { return null; }
function close() {}
function focus() {}
function blur() {}
function print() {}
function scrollTo() {}
function scrollBy() {}
function getComputedStyle() { return { getPropertyValue: function () { return ''; } }; }
function matchMedia(q) { return { matches: false, media: String(q), addListener: __noop, removeListener: __noop, addEventListener: __noop }; }
var console = {
    log: function () { __say('console: ' + Array.prototype.join.call(arguments, ' ')); },
    info: function () { __say('console: ' + Array.prototype.join.call(arguments, ' ')); },
    warn: function () { __say('console: ' + Array.prototype.join.call(arguments, ' ')); },
    error: function () { __say('console: ' + Array.prototype.join.call(arguments, ' ')); },
    debug: __noop, trace: __noop, dir: __noop, group: __noop, groupEnd: __noop, time: __noop, timeEnd: __noop
};
var Image = function () { return new __Element('img'); };
var Option = function (t, v) { var o = new __Element('option'); o.text = t; o.value = v; return o; };
var localStorage = (function () {
    var d = {};
    return { getItem: function (k) { return d.hasOwnProperty(k) ? d[k] : null; }, setItem: function (k, v) { d[k] = String(v); },
             removeItem: function (k) { delete d[k]; }, clear: function () { d = {}; }, key: function () { return null; }, length: 0 };
})();
var sessionStorage = localStorage;
