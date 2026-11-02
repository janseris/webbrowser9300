// Browser 9300: what a page's scripts find before they run (js/bwhost.c compiles it into every
// page's engine; js/genprelude.py makes prelude.h from it). The host gives __doc (the page's
// document: its nodes are the C tree's, js/bwdomjs.c), the node prototypes __pNode, __pEl,
// __pText, __pComment, __pFrag, __pDoc, and __href, __ua, __ref, __lang, __cookie, __w, __h,
// __write, __say, __navigate, __setCookie. ES5 only (MuJS): no arrow functions, let / const.

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

// ---- the window
var window = this, self = this, top = this, parent = this, frames = this, globalThis = this;
var __noop = function () {};
var __t0 = Date.now();
def0(Array.prototype, 'item', function (i) { var v = this[i]; return v === undefined ? null : v; });
function def0(o, n, f) { Object.defineProperty(o, n, { value: f, writable: true, configurable: true, enumerable: false }); }

var navigator = {
    userAgent: __ua, appName: 'Netscape', appCodeName: 'Mozilla', appVersion: __ua.replace(/^Mozilla\//, ''),
    platform: 'Symbian', product: 'Gecko', vendor: '', language: __lang, languages: [__lang],
    cookieEnabled: true, onLine: true, plugins: [], mimeTypes: [], maxTouchPoints: 0,
    javaEnabled: function () { return false; }, sendBeacon: function () { return false; }
};
var screen = { width: __w, height: __h, availWidth: __w, availHeight: __h, colorDepth: 16, pixelDepth: 16, orientation: { type: 'landscape-primary', angle: 0 } };
var innerWidth = __w, innerHeight = __h, outerWidth = __w, outerHeight = __h, devicePixelRatio = 1;
var pageXOffset = 0, pageYOffset = 0, scrollX = 0, scrollY = 0, name = '', closed = false, opener = null, frameElement = null;
var history = { length: 1, back: __noop, forward: __noop, go: __noop, pushState: __noop, replaceState: __noop, state: null, scrollRestoration: 'auto' };
var performance = { now: function () { return Date.now() - __t0; }, timing: { navigationStart: __t0 }, mark: __noop, measure: __noop, getEntriesByType: function () { return []; }, getEntriesByName: function () { return []; } };

// location: reading it gives the page's address; setting it sends the browser there
var __loc = (function (href) {
    var m = /^([a-z][a-z0-9+.-]*:)\/\/([^\/?#:]*)(:\d+)?([^?#]*)(\?[^#]*)?(#.*)?$/i.exec(href) || [];
    var l = { protocol: m[1] || '', hostname: m[2] || '', port: m[3] ? m[3].substring(1) : '', pathname: m[4] || '/', search: m[5] || '', hash: m[6] || '' };
    l.host = l.hostname + (l.port ? ':' + l.port : '');
    l.origin = l.protocol + '//' + l.host;
    return l;
})(__href);
Object.defineProperty(__loc, 'href', { get: function () { return __href; }, set: function (u) { __navigate(String(u)); }, enumerable: true });
__loc.assign = __loc.replace = function (u) { __navigate(String(u)); };
__loc.reload = __noop;
__loc.toString = function () { return __href; };
Object.defineProperty(this, 'location', { get: function () { return __loc; }, set: function (u) { __navigate(String(u)); }, configurable: true });

// ---- events
function Event(type, init) {
    this.type = String(type); this.bubbles = !!(init && init.bubbles); this.cancelable = !!(init && init.cancelable);
    this.defaultPrevented = false; this.target = null; this.currentTarget = null; this.timeStamp = Date.now();
    this.isTrusted = false; this.eventPhase = 0; this.__stop = false;
}
Event.prototype.preventDefault = function () { if (this.cancelable) this.defaultPrevented = true; this.returnValue = false; };
Event.prototype.stopPropagation = function () { this.__stop = true; this.cancelBubble = true; };
Event.prototype.stopImmediatePropagation = function () { this.__stop = true; this.__stopNow = true; };
Event.prototype.initEvent = function (t, b, c) { this.type = String(t); this.bubbles = !!b; this.cancelable = !!c; };
Event.prototype.composedPath = function () { return []; };
function CustomEvent(type, init) { Event.call(this, type, init); this.detail = init ? init.detail : null; }
CustomEvent.prototype = Object.create(Event.prototype);
CustomEvent.prototype.initCustomEvent = function (t, b, c, d) { this.initEvent(t, b, c); this.detail = d; };
var UIEvent = Event, MouseEvent = Event, KeyboardEvent = Event, FocusEvent = Event, PopStateEvent = Event, HashChangeEvent = Event, MessageEvent = Event, ErrorEvent = Event;
function __listen(t, type, fn, opts) {
    if (!fn) return;
    var l = t.__ev || (t.__ev = {}), a = l[type] || (l[type] = []);
    for (var i = 0; i < a.length; i++) if (a[i].fn === fn) return;
    a.push({ fn: fn, once: !!(opts && typeof opts === 'object' && opts.once) });
}
function __unlisten(t, type, fn) {
    var a = t.__ev && t.__ev[type];
    if (a) for (var i = 0; i < a.length; i++) if (a[i].fn === fn) { a.splice(i, 1); return; }
}
function __call(t, ev) {
    var a = t.__ev && t.__ev[ev.type], h = t['on' + ev.type];
    ev.currentTarget = t;
    if (typeof h === 'function') { try { if (h.call(t, ev) === false) ev.preventDefault(); } catch (e) { __say('on' + ev.type + ': ' + e); } }
    if (a) {
        a = a.slice();
        for (var i = 0; i < a.length && !ev.__stopNow; i++) {
            var f = a[i].fn;
            if (a[i].once) __unlisten(t, ev.type, f);
            try { if (typeof f === 'function') f.call(t, ev); else if (f && typeof f.handleEvent === 'function') f.handleEvent(ev); }
            catch (e) { __say(ev.type + ' listener: ' + e); }
        }
    }
}
function __dispatch(t, ev) {
    var path = [], n = t;
    ev.target = t;
    while (n) { path.push(n); n = n.parentNode; }
    if (t === window || path[path.length - 1] === document) path.push(window);
    for (var i = 0; i < path.length; i++) { __call(path[i], ev); if (ev.__stop || !ev.bubbles) break; }
    return !ev.defaultPrevented;
}
// the event handler properties a browser's window and elements have ("onload" in window)
['load', 'unload', 'beforeunload', 'resize', 'scroll', 'error', 'focus', 'blur', 'hashchange', 'popstate', 'message',
 'online', 'offline', 'pageshow', 'pagehide', 'storage', 'click', 'dblclick', 'mousedown', 'mouseup', 'mouseover',
 'mouseout', 'mousemove', 'keydown', 'keyup', 'keypress', 'submit', 'reset', 'change', 'input', 'select',
 'focusin', 'focusout', 'touchstart', 'touchend', 'touchmove', 'contextmenu', 'wheel', 'animationend', 'transitionend'].forEach(function (e) {
    if (!('on' + e in window)) window['on' + e] = null;
});
function addEventListener(type, fn, opts) { __listen(window, type, fn, opts); }
function removeEventListener(type, fn) { __unlisten(window, type, fn); }
function dispatchEvent(ev) { return __dispatch(window, ev); }
function attachEvent(type, fn) { __listen(window, String(type).replace(/^on/, ''), fn); }
function detachEvent(type, fn) { __unlisten(window, String(type).replace(/^on/, ''), fn); }

// ---- timers: run (in a virtual time) after the page's scripts, up to a few seconds' worth
var __timers = [], __now = 0, __tid = 0;
function __addTimer(fn, ms, args, every) {
    ms = +ms || 0; if (ms < 0) ms = 0;
    var t = { id: ++__tid, fn: fn, at: __now + ms, args: args, every: every ? Math.max(ms, 10) : 0, runs: 0 };
    __timers.push(t);
    return t.id;
}
function setTimeout(fn, ms) { return __addTimer(fn, ms, Array.prototype.slice.call(arguments, 2), false); }
function setInterval(fn, ms) { return __addTimer(fn, ms, Array.prototype.slice.call(arguments, 2), true); }
function clearTimeout(id) { for (var i = 0; i < __timers.length; i++) if (__timers[i].id === id) { __timers.splice(i, 1); return; } }
var clearInterval = clearTimeout;
function setImmediate(fn) { return setTimeout(fn, 0); }
function requestAnimationFrame(fn) { return setTimeout(function () { fn(__now); }, 16); }
var cancelAnimationFrame = clearTimeout, clearImmediate = clearTimeout;
function requestIdleCallback(fn) { return setTimeout(function () { fn({ didTimeout: false, timeRemaining: function () { return 10; } }); }, 1); }
var cancelIdleCallback = clearTimeout;
function queueMicrotask(fn) { setTimeout(fn, 0); }
// the due timers up to `until` ms, `max` calls at most; how many ran
function __runTimers(until, max) {
    var ran = 0;
    while (ran < max) {
        var best = -1;
        for (var i = 0; i < __timers.length; i++)
            if (__timers[i].at <= until && (best < 0 || __timers[i].at < __timers[best].at)) best = i;
        if (best < 0) break;
        var t = __timers[best];
        if (t.every && t.runs < 2) { t.at += t.every; t.runs++; } else __timers.splice(best, 1);
        __now = t.at;
        ran++;
        try { if (typeof t.fn === 'function') t.fn.apply(window, t.args); else eval(String(t.fn)); }
        catch (e) { __say('timer: ' + e); }
    }
    return ran;
}

function alert(m) { __say('alert: ' + m); }
function confirm(m) { __say('confirm: ' + m); return false; }
function prompt(m) { __say('prompt: ' + m); return null; }
function open() { return null; }
function close() {}
function focus() {}
function blur() {}
function print() {}
function stop() {}
function scrollTo() {}
function scrollBy() {}
function scroll() {}
function getSelection() { return { rangeCount: 0, toString: function () { return ''; }, removeAllRanges: __noop, addRange: __noop }; }
function matchMedia(q) { return { matches: false, media: String(q), addListener: __noop, removeListener: __noop, addEventListener: __noop, removeEventListener: __noop }; }
function postMessage() {}
var console = (function () {
    function out() { __say('console: ' + Array.prototype.join.call(arguments, ' ')); }
    return { log: out, info: out, warn: out, error: out, debug: __noop, trace: __noop, dir: __noop, table: __noop,
             group: __noop, groupCollapsed: __noop, groupEnd: __noop, time: __noop, timeEnd: __noop, assert: __noop, count: __noop };
})();
var localStorage = (function () {
    var d = {};
    return { getItem: function (k) { return d.hasOwnProperty(k) ? d[k] : null; }, setItem: function (k, v) { d[k] = String(v); },
             removeItem: function (k) { delete d[k]; }, clear: function () { d = {}; }, key: function () { return null; }, length: 0 };
})();
var sessionStorage = localStorage;

// observers: what is observed counts as on the screen at once (lazy pictures load)
function IntersectionObserver(cb) { this.__cb = cb; }
IntersectionObserver.prototype.observe = function (el) {
    var o = this;
    setTimeout(function () { o.__cb([{ target: el, isIntersecting: true, intersectionRatio: 1, time: __now,
        boundingClientRect: { top: 0, left: 0, width: 0, height: 0 }, intersectionRect: { top: 0, left: 0, width: 0, height: 0 }, rootBounds: null }], o); }, 0);
};
IntersectionObserver.prototype.unobserve = IntersectionObserver.prototype.disconnect = __noop;
IntersectionObserver.prototype.takeRecords = function () { return []; };
function MutationObserver() {}
MutationObserver.prototype.observe = MutationObserver.prototype.disconnect = __noop;
MutationObserver.prototype.takeRecords = function () { return []; };
var WebKitMutationObserver = MutationObserver, ResizeObserver = MutationObserver, PerformanceObserver = MutationObserver;

// ---- the DOM: the nodes are the C tree's (bwdomjs.c); built on them here
var document = __doc;
function Node() { throw new TypeError('Illegal constructor'); }
function Element() { throw new TypeError('Illegal constructor'); }
function CharacterData() { throw new TypeError('Illegal constructor'); }
function Text(t) { return document.createTextNode(t === undefined ? '' : t); }
function Comment(t) { return document.createComment(t === undefined ? '' : t); }
function DocumentFragment() { return document.createDocumentFragment(); }
function Document() { throw new TypeError('Illegal constructor'); }
Node.prototype = __pNode; Element.prototype = __pEl; Text.prototype = __pText; Comment.prototype = __pComment;
CharacterData.prototype = __pText; DocumentFragment.prototype = __pFrag; Document.prototype = __pDoc;
__pNode.constructor = Node; __pEl.constructor = Element; __pText.constructor = Text; __pComment.constructor = Comment;
__pFrag.constructor = DocumentFragment; __pDoc.constructor = Document;
var HTMLElement = Element, HTMLDocument = Document, SVGElement = Element, HTMLUnknownElement = Element;
['Anchor', 'Body', 'BR', 'Button', 'Canvas', 'Div', 'Form', 'Head', 'Heading', 'Html', 'IFrame', 'Image', 'Input', 'Label', 'LI',
 'Link', 'Meta', 'OList', 'Option', 'Paragraph', 'Pre', 'Script', 'Select', 'Span', 'Style', 'Table', 'TableCell', 'TableRow',
 'TextArea', 'Title', 'UList', 'Video', 'Audio', 'Media', 'Template', 'Picture', 'Source'].forEach(function (k) { window['HTML' + k + 'Element'] = Element; });
(function (N) {
    var c = { ELEMENT_NODE: 1, ATTRIBUTE_NODE: 2, TEXT_NODE: 3, CDATA_SECTION_NODE: 4, COMMENT_NODE: 8, DOCUMENT_NODE: 9,
              DOCUMENT_TYPE_NODE: 10, DOCUMENT_FRAGMENT_NODE: 11 };
    for (var k in c) { Node[k] = c[k]; N[k] = c[k]; }
    def0(N, 'addEventListener', function (t, f, o) { __listen(this, t, f, o); });
    def0(N, 'removeEventListener', function (t, f) { __unlisten(this, t, f); });
    def0(N, 'dispatchEvent', function (e) { return __dispatch(this, e); });
    def0(N, 'attachEvent', function (t, f) { __listen(this, String(t).replace(/^on/, ''), f); return true; });
    def0(N, 'detachEvent', function (t, f) { __unlisten(this, String(t).replace(/^on/, ''), f); });
    def0(N, 'compareDocumentPosition', function (o) { return this === o ? 0 : (this.contains(o) ? 20 : (o.contains(this) ? 10 : 4)); });
    def0(N, 'isSameNode', function (o) { return this === o; });
    def0(N, 'getRootNode', function () { var n = this; while (n.parentNode) n = n.parentNode; return n; });
    Object.defineProperty(N, 'innerText', { get: function () { return this.textContent; }, set: function (v) { this.textContent = v; }, configurable: true });
})(__pNode);
Object.defineProperty(__pText, 'length', { get: function () { return this.data.length; }, configurable: true });
Object.defineProperty(__pText, 'wholeText', { get: function () { return this.data; }, configurable: true });
def0(__pText, 'splitText', function (i) { var d = this.data, t = document.createTextNode(d.substring(i)); this.data = d.substring(0, i); if (this.parentNode) this.parentNode.insertBefore(t, this.nextSibling); return t; });
def0(__pText, 'appendData', function (s) { this.data += s; });

function __nodes(args) {
    var f = document.createDocumentFragment();
    for (var i = 0; i < args.length; i++) f.appendChild(typeof args[i] === 'object' && args[i] ? args[i] : document.createTextNode(String(args[i])));
    return f;
}
function __camel(k) { k = String(k); return k.indexOf('--') === 0 ? k : k.replace(/-([a-z])/g, function (m, c) { return c.toUpperCase(); }); }
function __kebab(k) { return k.indexOf('--') === 0 ? k : k.replace(/[A-Z]/g, function (c) { return '-' + c.toLowerCase(); }).replace(/^(webkit|moz|ms)-/, '-$1-'); }
function __cssOf(st) {
    var out = [];
    for (var k in st) if (st.hasOwnProperty(k) && k !== '__el' && typeof st[k] !== 'function' && st[k] !== '' && st[k] != null) out.push(__kebab(k) + ': ' + st[k]);
    return out.join('; ');
}
// el.style: an object from the style attribute, written back into it before the page goes out
function CSSStyleDeclaration(css) { if (css) this.cssText = css; }
CSSStyleDeclaration.prototype.setProperty = function (k, v) { this[__camel(k)] = v == null ? '' : String(v); };
CSSStyleDeclaration.prototype.getPropertyValue = function (k) { var v = this[__camel(k)]; return v == null ? '' : String(v); };
CSSStyleDeclaration.prototype.removeProperty = function (k) { var c = __camel(k), o = this[c]; delete this[c]; return o == null ? '' : o; };
CSSStyleDeclaration.prototype.getPropertyPriority = function () { return ''; };
Object.defineProperty(CSSStyleDeclaration.prototype, 'cssText', {
    get: function () { return __cssOf(this); },
    set: function (t) {
        for (var k in this) if (this.hasOwnProperty(k)) delete this[k];
        var ds = String(t).split(';');
        for (var i = 0; i < ds.length; i++) { var c = ds[i].indexOf(':'); if (c > 0) this[__camel(ds[i].substring(0, c).trim().toLowerCase())] = ds[i].substring(c + 1).trim(); }
    }, configurable: true
});
Object.defineProperty(CSSStyleDeclaration.prototype, 'length', { get: function () { return __cssOf(this) ? __cssOf(this).split('; ').length : 0; }, configurable: true });
var __styled = [];
function __flushStyles() {
    for (var i = 0; i < __styled.length; i++) {
        var el = __styled[i], c = __cssOf(el.__st);
        if (c !== el.__st0) { if (c) el.setAttribute('style', c); else el.removeAttribute('style'); }
    }
}
function getComputedStyle(el) {
    var s = new CSSStyleDeclaration(el && el.getAttribute ? el.getAttribute('style') : '');
    if (el && el.__st) for (var k in el.__st) if (el.__st.hasOwnProperty(k)) s[k] = el.__st[k];
    return s;
}

(function (P) {
    function str(prop, attr) {
        Object.defineProperty(P, prop, { get: function () { var v = this.getAttribute(attr); return v === null ? '' : v; },
            set: function (v) { this.setAttribute(attr, v); }, configurable: true });
    }
    function bool(prop, attr) {
        Object.defineProperty(P, prop, { get: function () { return this.hasAttribute(attr); },
            set: function (v) { if (v) this.setAttribute(attr, ''); else this.removeAttribute(attr); }, configurable: true });
    }
    ['id', 'title', 'lang', 'dir', 'name', 'type', 'alt', 'rel', 'target', 'action', 'method', 'placeholder', 'content',
     'charset', 'media', 'role', 'src', 'href', 'srcset', 'sizes', 'accept', 'enctype', 'label', 'pattern', 'poster',
     'download', 'crossOrigin', 'integrity', 'nonce', 'slot', 'colSpan', 'rowSpan', 'span', 'size', 'step', 'min', 'max',
     'autocomplete', 'inputMode', 'httpEquiv', 'tabIndex', 'accessKey', 'contentEditable', 'draggable'].forEach(function (p) {
        str(p, p === 'crossOrigin' ? 'crossorigin' : p === 'colSpan' ? 'colspan' : p === 'rowSpan' ? 'rowspan' : p === 'httpEquiv' ? 'http-equiv' :
               p === 'tabIndex' ? 'tabindex' : p === 'accessKey' ? 'accesskey' : p === 'contentEditable' ? 'contenteditable' : p === 'inputMode' ? 'inputmode' : p.toLowerCase());
    });
    str('className', 'class'); str('htmlFor', 'for');
    ['checked', 'disabled', 'hidden', 'selected', 'required', 'multiple', 'async', 'defer', 'autofocus', 'open', 'controls',
     'autoplay', 'loop', 'muted', 'noModule', 'reversed', 'inert'].forEach(function (p) { bool(p, p.toLowerCase()); });
    bool('readOnly', 'readonly'); bool('noValidate', 'novalidate'); bool('defaultChecked', 'checked');
    ['width', 'height'].forEach(function (p) {
        Object.defineProperty(P, p, { get: function () { return parseInt(this.getAttribute(p), 10) || 0; }, set: function (v) { this.setAttribute(p, v); }, configurable: true });
    });
    Object.defineProperty(P, 'value', {
        get: function () {
            var t = this.tagName;
            if (t === 'TEXTAREA') return this.textContent;
            if (t === 'SELECT') { var o = this.querySelector('option[selected]') || this.querySelector('option'); return o ? o.value : ''; }
            if (t === 'OPTION' && !this.hasAttribute('value')) return this.textContent;
            var v = this.getAttribute('value'); return v === null ? '' : v;
        },
        set: function (v) { if (this.tagName === 'TEXTAREA') this.textContent = v; else this.setAttribute('value', v); }, configurable: true
    });
    Object.defineProperty(P, 'text', { get: function () { return this.textContent; }, set: function (v) { this.textContent = v; }, configurable: true });
    Object.defineProperty(P, 'defaultValue', { get: function () { return this.getAttribute('value') || ''; }, set: function (v) { this.setAttribute('value', v); }, configurable: true });
    Object.defineProperty(P, 'style', {
        get: function () {
            if (!this.__st) { var s = this.getAttribute('style') || ''; this.__st = new CSSStyleDeclaration(s); this.__st0 = __cssOf(this.__st); __styled.push(this); }
            return this.__st;
        },
        set: function (v) { this.style.cssText = v; }, configurable: true
    });
    Object.defineProperty(P, 'classList', {
        get: function () {
            var el = this;
            function list() { return (el.getAttribute('class') || '').split(/\s+/).filter(function (c) { return c; }); }
            function put(a) { el.setAttribute('class', a.join(' ')); }
            var cl = {
                add: function () { var a = list(); for (var i = 0; i < arguments.length; i++) if (a.indexOf(String(arguments[i])) < 0) a.push(String(arguments[i])); put(a); },
                remove: function () { var a = list(), r = Array.prototype.map.call(arguments, String); put(a.filter(function (c) { return r.indexOf(c) < 0; })); },
                contains: function (c) { return list().indexOf(String(c)) >= 0; },
                toggle: function (c, force) { c = String(c); var has = this.contains(c); if (force === undefined ? has : !force) { this.remove(c); return false; } this.add(c); return true; },
                replace: function (a, b) { var l = list(), i = l.indexOf(String(a)); if (i < 0) return false; l[i] = String(b); put(l); return true; },
                item: function (i) { var l = list(); return i < l.length ? l[i] : null; },
                toString: function () { return el.getAttribute('class') || ''; }
            };
            Object.defineProperty(cl, 'length', { get: function () { return list().length; } });
            Object.defineProperty(cl, 'value', { get: function () { return el.getAttribute('class') || ''; } });
            return cl;
        }, configurable: true
    });
    Object.defineProperty(P, 'dataset', {
        get: function () {
            var d = {}, names = this.getAttributeNames();
            for (var i = 0; i < names.length; i++) if (names[i].indexOf('data-') === 0) d[__camel(names[i].substring(5))] = this.getAttribute(names[i]);
            return d;
        }, configurable: true
    });
    ['offsetWidth', 'offsetHeight', 'offsetTop', 'offsetLeft', 'clientWidth', 'clientHeight', 'clientTop', 'clientLeft',
     'scrollWidth', 'scrollHeight', 'scrollTop', 'scrollLeft'].forEach(function (p) {
        Object.defineProperty(P, p, { get: function () { return 0; }, set: __noop, configurable: true });
    });
    Object.defineProperty(P, 'offsetParent', { get: function () { return this.parentNode; }, configurable: true });
    Object.defineProperty(P, 'form', { get: function () { return this.closest('form'); }, configurable: true });
    Object.defineProperty(P, 'elements', { get: function () { return this.querySelectorAll('input,select,textarea,button,fieldset'); }, configurable: true });
    Object.defineProperty(P, 'options', { get: function () { return this.getElementsByTagName('option'); }, configurable: true });
    Object.defineProperty(P, 'selectedIndex', {
        get: function () { var o = this.getElementsByTagName('option'); for (var i = 0; i < o.length; i++) if (o[i].hasAttribute('selected')) return i; return o.length ? 0 : -1; },
        set: function (v) { var o = this.getElementsByTagName('option'); for (var i = 0; i < o.length; i++) o[i].selected = i === +v; }, configurable: true
    });
    def0(P, 'getBoundingClientRect', function () { return { top: 0, left: 0, right: 0, bottom: 0, width: 0, height: 0, x: 0, y: 0 }; });
    def0(P, 'getClientRects', function () { return []; });
    def0(P, 'focus', __noop); def0(P, 'blur', __noop); def0(P, 'select', __noop); def0(P, 'scrollIntoView', __noop);
    def0(P, 'scrollTo', __noop); def0(P, 'scrollBy', __noop); def0(P, 'submit', __noop); def0(P, 'reset', __noop);
    def0(P, 'setSelectionRange', __noop); def0(P, 'checkValidity', function () { return true; }); def0(P, 'reportValidity', function () { return true; });
    def0(P, 'play', function () { return { then: __noop, catch: __noop }; }); def0(P, 'pause', __noop); def0(P, 'load', __noop);
    def0(P, 'click', function () { this.dispatchEvent(new Event('click', { bubbles: true, cancelable: true })); });
    def0(P, 'toggleAttribute', function (n, f) { var h = this.hasAttribute(n); if (f === undefined ? h : !f) { this.removeAttribute(n); return false; } this.setAttribute(n, ''); return true; });
    def0(P, 'append', function () { this.appendChild(__nodes(arguments)); });
    def0(P, 'prepend', function () { this.insertBefore(__nodes(arguments), this.firstChild); });
    def0(P, 'before', function () { if (this.parentNode) this.parentNode.insertBefore(__nodes(arguments), this); });
    def0(P, 'after', function () { if (this.parentNode) this.parentNode.insertBefore(__nodes(arguments), this.nextSibling); });
    def0(P, 'replaceWith', function () { if (this.parentNode) { var p = this.parentNode; p.insertBefore(__nodes(arguments), this); p.removeChild(this); } });
    def0(P, 'replaceChildren', function () { while (this.firstChild) this.removeChild(this.firstChild); this.appendChild(__nodes(arguments)); });
    def0(P, 'insertAdjacentElement', function (pos, el) {
        pos = String(pos).toLowerCase();
        if (pos === 'beforebegin') this.before(el); else if (pos === 'afterbegin') this.prepend(el); else if (pos === 'beforeend') this.appendChild(el); else if (pos === 'afterend') this.after(el);
        return el;
    });
    def0(P, 'insertAdjacentText', function (pos, t) { this.insertAdjacentElement(pos, document.createTextNode(t)); });
    def0(P, 'attachShadow', function () { return this; });
    def0(P, 'getElementsByName', function (n) { return this.querySelectorAll('[name="' + String(n).replace(/"/g, '\\"') + '"]'); });
})(__pEl);
def0(__pFrag, 'append', __pEl.append);
def0(__pFrag, 'prepend', __pEl.prepend);

// the document
(function (D) {
    var cookies = __cookie;
    Object.defineProperty(D, 'cookie', {
        get: function () { return cookies; },
        set: function (v) {
            v = String(v);
            __setCookie(v);
            var nv = v.split(';')[0], eq = nv.indexOf('=');
            if (eq < 0) return;
            var k = nv.substring(0, eq).trim(), parts = cookies ? cookies.split('; ') : [], out = [], expired = /expires=([^;]*)/i.exec(v);
            var gone = (expired && new Date(expired[1]).getTime() < Date.now()) || /max-age=(0|-)/i.test(v);
            for (var i = 0; i < parts.length; i++) if (parts[i].split('=')[0] !== k) out.push(parts[i]);
            if (!gone) out.push(nv.trim());
            cookies = out.join('; ');
        }, configurable: true
    });
    Object.defineProperty(D, 'location', { get: function () { return __loc; }, set: function (u) { __navigate(String(u)); }, configurable: true });
    D.write = function () { for (var i = 0; i < arguments.length; i++) __write(String(arguments[i])); };
    D.writeln = function () { for (var i = 0; i < arguments.length; i++) __write(String(arguments[i])); __write('\n'); };
    D.open = D.close = __noop;
    D.getElementsByName = __pEl.getElementsByName;
    D.createEvent = function () { return new Event(''); };
    D.createRange = function () { return { setStart: __noop, setEnd: __noop, selectNodeContents: __noop, collapse: __noop,
        createContextualFragment: function (h) { var t = document.createElement('div'); t.innerHTML = h; var f = document.createDocumentFragment(); while (t.firstChild) f.appendChild(t.firstChild); return f; },
        getBoundingClientRect: __pEl.getBoundingClientRect }; };
    D.hasFocus = function () { return true; };
    D.execCommand = function () { return false; };
    D.elementFromPoint = function () { return null; };
    D.referrer = __ref; D.readyState = 'loading'; D.compatMode = 'CSS1Compat'; D.characterSet = D.charset = D.inputEncoding = 'UTF-8';
    D.contentType = 'text/html'; D.visibilityState = 'visible'; D.hidden = false; D.designMode = 'off'; D.dir = '';
    D.URL = D.documentURI = __href; D.domain = __loc.hostname; D.defaultView = window; D.currentScript = null;
    D.implementation = {
        hasFeature: function () { return true; },
        createHTMLDocument: function (t) {
            var html = document.createElement('html'), head = document.createElement('head'), body = document.createElement('body');
            html.appendChild(head); html.appendChild(body);
            return { documentElement: html, head: head, body: body, title: t || '', createElement: function (n) { return document.createElement(n); },
                     createTextNode: function (s) { return document.createTextNode(s); }, createDocumentFragment: function () { return document.createDocumentFragment(); },
                     querySelector: function (s) { return html.querySelector(s); }, querySelectorAll: function (s) { return html.querySelectorAll(s); },
                     getElementById: function (id) { return html.querySelector('#' + id); }, getElementsByTagName: function (n) { return html.getElementsByTagName(n); } };
        }
    };
    Object.defineProperty(D, 'scripts', { get: function () { return document.getElementsByTagName('script'); } });
    Object.defineProperty(D, 'images', { get: function () { return document.getElementsByTagName('img'); } });
    Object.defineProperty(D, 'links', { get: function () { return document.querySelectorAll('a[href],area[href]'); } });
    Object.defineProperty(D, 'forms', { get: function () { return document.getElementsByTagName('form'); } });
    Object.defineProperty(D, 'anchors', { get: function () { return document.querySelectorAll('a[name]'); } });
    Object.defineProperty(D, 'styleSheets', { get: function () { return []; } });
    Object.defineProperty(D, 'activeElement', { get: function () { return document.body; } });
    Object.defineProperty(D, 'scrollingElement', { get: function () { return document.documentElement; } });
})(document);

function Image(w, h) { var i = document.createElement('img'); if (w) i.width = w; if (h) i.height = h; return i; }
function Option(t, v, d, s) { var o = document.createElement('option'); if (t !== undefined) o.textContent = t; if (v !== undefined) o.value = v; if (s) o.selected = true; return o; }
function DOMParser() {}
DOMParser.prototype.parseFromString = function (s) {
    var d = document.implementation.createHTMLDocument('');
    d.body.innerHTML = s;
    return d;
};
function XMLSerializer() {}
XMLSerializer.prototype.serializeToString = function (n) { return n.outerHTML || n.textContent || ''; };

// the page is read: DOMContentLoaded, then load (and <body onload>)
function __loaded(phase) {
    if (phase === 1) {
        document.readyState = 'interactive';
        __dispatch(document, new Event('readystatechange'));
        __dispatch(document, new Event('DOMContentLoaded', { bubbles: true }));
    } else {
        document.readyState = 'complete';
        __dispatch(document, new Event('readystatechange'));
        var b = document.body, a = b && b.getAttribute('onload');
        if (a && typeof window.onload !== 'function') { try { window.onload = new Function('event', a); } catch (e) { __say('body onload: ' + e); } }
        __call(window, new Event('load'));
        __call(window, new Event('pageshow'));
    }
}
