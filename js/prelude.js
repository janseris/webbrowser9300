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
function __err(e) { var t = e && typeof e === 'object' && typeof e.stackTrace === 'string' ? e.stackTrace.split('\n').slice(1, 3).join(' ') : ''; return String(e) + t; }
var window = this, self = this, top = this, parent = this, frames = this, globalThis = this;
function Window() { throw new TypeError('Illegal constructor'); }
Object.setPrototypeOf(this, Window.prototype);
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
var performance = (function () {   /* marks and measures kept (React and Next.js read them back) */
    var ents = [];
    function now() { return Date.now() - __t0; }
    function rm(type) { return function (n) { ents = ents.filter(function (e) { return e.entryType !== type || (n !== undefined && e.name !== n); }); }; }
    return { now: now, timeOrigin: __t0, timing: { navigationStart: __t0 }, navigation: { type: 0, redirectCount: 0 },
        mark: function (n, o) { var e = { name: String(n), entryType: 'mark', startTime: o && o.startTime !== undefined ? o.startTime : now(), duration: 0, detail: o ? o.detail : null }; ents.push(e); return e; },
        measure: function (n, a, b) {
            function at(x, d) { if (x === undefined) return d; if (typeof x === 'number') return x; for (var i = ents.length - 1; i >= 0; i--) if (ents[i].name === x) return ents[i].startTime; return d; }
            var s = typeof a === 'object' && a ? at(a.start, 0) : at(a, 0), f = typeof a === 'object' && a ? at(a.end, now()) : at(b, now());
            var e = { name: String(n), entryType: 'measure', startTime: s, duration: f - s, detail: null }; ents.push(e); return e;
        },
        getEntries: function () { return ents.slice(); },
        getEntriesByType: function (t) { return ents.filter(function (e) { return e.entryType === t; }); },
        getEntriesByName: function (n, t) { return ents.filter(function (e) { return e.name === n && (!t || e.entryType === t); }); },
        clearMarks: rm('mark'), clearMeasures: rm('measure'), clearResourceTimings: __noop, setResourceTimingBufferSize: __noop,
        addEventListener: __noop, removeEventListener: __noop, toJSON: function () { return {}; } };
})();

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
            catch (e) { __say(ev.type + ' listener: ' + __err(e)); }
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
        catch (e) { __say('timer: ' + __err(e)); }
        window['%runJobs'](20000);
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
    function out() { __say('console: ' + Array.prototype.map.call(arguments, function (a) { return a instanceof Error ? __err(a) : a; }).join(' ')); }
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
function IntersectionObserver(cb, o) { this.__cb = cb; this.root = o && o.root || null; this.rootMargin = o && o.rootMargin || '0px'; this.thresholds = [0]; }
function IntersectionObserverEntry(d) { for (var k in d) this[k] = d[k]; }
IntersectionObserverEntry.prototype.isIntersecting = true; IntersectionObserverEntry.prototype.intersectionRatio = 1;
IntersectionObserver.prototype.observe = function (el) {
    var o = this;
    setTimeout(function () { o.__cb([new IntersectionObserverEntry({ target: el, isIntersecting: true, intersectionRatio: 1, time: __now,
        boundingClientRect: { top: 0, left: 0, width: 0, height: 0 }, intersectionRect: { top: 0, left: 0, width: 0, height: 0 }, rootBounds: null })], o); }, 0);
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
function DocumentType() { throw new TypeError('Illegal constructor'); }
DocumentType.prototype = Object.create(__pNode); DocumentType.prototype.constructor = DocumentType;
Node.prototype = __pNode; Element.prototype = __pEl; Text.prototype = __pText; Comment.prototype = __pComment;
CharacterData.prototype = __pText; DocumentFragment.prototype = __pFrag; Document.prototype = __pDoc;
__pNode.constructor = Node; __pEl.constructor = Element; __pText.constructor = Text; __pComment.constructor = Comment;
__pFrag.constructor = DocumentFragment; __pDoc.constructor = Document;
var HTMLElement = Element, HTMLDocument = Document, SVGElement = Element, HTMLUnknownElement = Element;
['Anchor', 'Body', 'BR', 'Button', 'Canvas', 'Div', 'Form', 'Head', 'Heading', 'Html', 'IFrame', 'Image', 'Input', 'Label', 'LI',
 'Link', 'Meta', 'OList', 'Option', 'Paragraph', 'Pre', 'Script', 'Select', 'Span', 'Style', 'Table', 'TableCell', 'TableRow',
 'TextArea', 'Title', 'UList', 'Video', 'Audio', 'Media', 'Template', 'Picture', 'Source', 'Dialog', 'Details', 'Slot',
 'Area', 'Map', 'Object', 'Embed', 'FieldSet', 'Legend', 'Progress', 'Meter', 'Output', 'Time', 'Data', 'Menu', 'Quote', 'Mod',
 'DList', 'Base', 'HR', 'TableSection', 'TableCaption', 'TableCol', 'OptGroup', 'DataList', 'Track', 'Param', 'Summary'].forEach(function (k) { window['HTML' + k + 'Element'] = Element; });
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
    def0(N, 'isEqualNode', function eq(o) {
        if (!o || o.nodeType !== this.nodeType || o.nodeName !== this.nodeName) return false;
        if (this.nodeType === 3 || this.nodeType === 8) return this.data === o.data;
        if (this.nodeType === 1) {
            var a = this.getAttributeNames(), b = o.getAttributeNames();
            if (a.length !== b.length) return false;
            for (var i = 0; i < a.length; i++) if (this.getAttribute(a[i]) !== o.getAttribute(a[i])) return false;
        }
        var x = this.firstChild, y = o.firstChild;
        for (; x && y; x = x.nextSibling, y = y.nextSibling) if (!x.isEqualNode(y)) return false;
        return !x && !y;
    });
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
var __blockTags = /^(address|article|aside|blockquote|body|center|dd|details|dialog|dir|div|dl|dt|fieldset|figcaption|figure|footer|form|h[1-6]|header|hgroup|hr|html|legend|main|menu|nav|ol|p|pre|section|summary|ul)$/;
function getComputedStyle(el) {
    var s = new CSSStyleDeclaration(el && el.getAttribute ? el.getAttribute('style') : '');
    if (el && el.__st) for (var k in el.__st) if (el.__st.hasOwnProperty(k)) s[k] = el.__st[k];
    if (!s.display && el && el.localName) {   /* (what the tag gives) */
        var t = el.localName;
        s.display = el.hasAttribute('hidden') || /^(head|script|style|template|title|meta|link|noscript)$/.test(t) ? 'none' : __blockTags.test(t) ? 'block' :
            t === 'li' ? 'list-item' : t === 'table' ? 'table' : t === 'tr' ? 'table-row' : t === 'td' || t === 'th' ? 'table-cell' : 'inline';
    }
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
    (function (d) {   /* <template>.content: a fragment its children move into (when read) */
        Object.defineProperty(P, 'content', { get: function () {
            if (this.localName !== 'template') return d.get.call(this);
            var f = this['%tc'];
            if (!f) { f = document.createDocumentFragment(); def0(this, '%tc', f); }
            var c = this.firstChild;
            if (c && c.nodeType === 3 && !c.nextSibling && c.data.indexOf('<') >= 0) {   /* (the tree keeps it as text) */
                var div = document.createElement('div');
                div.innerHTML = c.data;
                this.removeChild(c);
                while (div.firstChild) f.appendChild(div.firstChild);
            }
            while (this.firstChild) f.appendChild(this.firstChild);
            return f;
        }, set: d.set, configurable: true });
    })(Object.getOwnPropertyDescriptor(P, 'content'));
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
    def0(P, 'hasAttributes', function () { return this.getAttributeNames().length > 0; });
    Object.defineProperty(P, 'textTracks', { get: function () {   /* (media elements: none) */
        return this.localName === 'video' || this.localName === 'audio' ? (this['%tt'] || (def0(this, '%tt', (function (l) { l.addEventListener = l.removeEventListener = __noop; l.getTrackById = function () { return null; }; l.onchange = l.onaddtrack = l.onremovetrack = null; return l; })([])), this['%tt'])) : undefined; }, configurable: true });
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
// TreeWalker / NodeIterator (what lit-html's templates walk)
//@lazy
var NodeFilter = { FILTER_ACCEPT: 1, FILTER_REJECT: 2, FILTER_SKIP: 3, SHOW_ALL: 0xFFFFFFFF, SHOW_ELEMENT: 1, SHOW_ATTRIBUTE: 2,
    SHOW_TEXT: 4, SHOW_CDATA_SECTION: 8, SHOW_PROCESSING_INSTRUCTION: 64, SHOW_COMMENT: 128, SHOW_DOCUMENT: 256,
    SHOW_DOCUMENT_TYPE: 512, SHOW_DOCUMENT_FRAGMENT: 1024 };
function TreeWalker(root, what, filter) {
    this.root = root; this.whatToShow = what === undefined || what === null ? 0xFFFFFFFF : what >>> 0;
    this.filter = filter || null; this.currentNode = root;
}
(function (P) {
    function ok(w, n) {
        if (!(w.whatToShow & (1 << (n.nodeType - 1)))) return 3;
        var f = w.filter;
        return !f ? 1 : typeof f === 'function' ? f(n) : f.acceptNode(n);
    }
    P.nextNode = function () {
        var n = this.currentNode, r = 1, t;
        for (;;) {
            while (r !== 2 && n.firstChild) { n = n.firstChild; r = ok(this, n); if (r === 1) return this.currentNode = n; }
            for (t = n; t; t = t.parentNode) { if (t === this.root) return null; if (t.nextSibling) break; }
            if (!t) return null;
            n = t.nextSibling; r = ok(this, n);
            if (r === 1) return this.currentNode = n;
        }
    };
    P.previousNode = function () {
        var n = this.currentNode;
        while (n !== this.root) {
            var s = n.previousSibling;
            while (s) { n = s; while (n.lastChild) n = n.lastChild; if (ok(this, n) === 1) return this.currentNode = n; s = n.previousSibling; }
            if (n === this.root || !n.parentNode) return null;
            n = n.parentNode;
            if (ok(this, n) === 1) return this.currentNode = n;
        }
        return null;
    };
    P.parentNode = function () {
        for (var n = this.currentNode; n && n !== this.root; ) { n = n.parentNode; if (n && ok(this, n) === 1) return this.currentNode = n; }
        return null;
    };
    function child(first) {
        return function () {
            for (var n = first ? this.currentNode.firstChild : this.currentNode.lastChild; n; n = first ? n.nextSibling : n.previousSibling)
                if (ok(this, n) === 1) return this.currentNode = n;
            return null;
        };
    }
    function sib(next) {
        return function () {
            if (this.currentNode === this.root) return null;
            for (var n = next ? this.currentNode.nextSibling : this.currentNode.previousSibling; n; n = next ? n.nextSibling : n.previousSibling)
                if (ok(this, n) === 1) return this.currentNode = n;
            return null;
        };
    }
    P.firstChild = child(true); P.lastChild = child(false);
    P.nextSibling = sib(true); P.previousSibling = sib(false);
})(TreeWalker.prototype);
function NodeIterator(root, what, filter) { TreeWalker.call(this, root, what, filter); this.referenceNode = root; this['%s'] = false; }
NodeIterator.prototype.nextNode = function () {
    if (!this['%s']) { this['%s'] = true; var r = this.whatToShow & (1 << (this.root.nodeType - 1)) ? 1 : 3; if (r === 1 && (!this.filter || (typeof this.filter === 'function' ? this.filter(this.root) : this.filter.acceptNode(this.root)) === 1)) return this.referenceNode = this.root; }
    this.currentNode = this.referenceNode;
    var n = TreeWalker.prototype.nextNode.call(this);
    if (n) this.referenceNode = n;
    return n;
};
NodeIterator.prototype.detach = __noop;
//@end
document.createTreeWalker = function (root, what, filter) { return new TreeWalker(root, what, filter); };
document.createNodeIterator = function (root, what, filter) { return new NodeIterator(root, what, filter); };
if (!document.importNode) document.importNode = function (n, deep) { return n.cloneNode(!!deep); };

// ---- web APIs newer pages call (ES2015+ built-ins are the engine's: js/es.js)
// node lists are arrays here
function NodeList() { throw new TypeError('Illegal constructor'); }
NodeList.prototype = Array.prototype;
var HTMLCollection = NodeList, NamedNodeMap = NodeList, DOMTokenList = NodeList, StyleSheetList = NodeList, HTMLAllCollection = NodeList;
function CSSStyleSheet() { this.cssRules = []; }
CSSStyleSheet.prototype.insertRule = function (r, i) { this.cssRules.splice(i | 0, 0, { cssText: r }); return i | 0; };
CSSStyleSheet.prototype.deleteRule = function (i) { this.cssRules.splice(i, 1); };
CSSStyleSheet.prototype.replaceSync = function (t) { this.cssRules = [{ cssText: String(t) }]; };
CSSStyleSheet.prototype.replace = function (t) { this.replaceSync(t); return Promise.resolve(this); };
def0(Node, '%noctor', true); def0(Element, '%noctor', true); def0(Document, '%noctor', true); def0(CharacterData, '%noctor', true);
//@lazy
// fetch / XMLHttpRequest: a GET's text is what the browser fetched for the page (__fetchText; null:
// not yet, the URL is noted and the page runs again with it); other methods are not sent
function Headers(h) {
    def0(this, '%h', {});
    if (h instanceof Headers) h.forEach(function (v, k) { this.set(k, v); }, this);
    else if (h instanceof Array) for (var i = 0; i < h.length; i++) this.append(h[i][0], h[i][1]);
    else if (h) for (var k in h) if (h.hasOwnProperty(k)) this.append(k, h[k]);
}
Headers.prototype.get = function (k) { var v = this['%h'][String(k).toLowerCase()]; return v === undefined ? null : v; };
Headers.prototype.set = function (k, v) { this['%h'][String(k).toLowerCase()] = String(v); };
Headers.prototype.append = function (k, v) { k = String(k).toLowerCase(); var o = this['%h']; o[k] = o[k] === undefined ? String(v) : o[k] + ', ' + v; };
Headers.prototype.has = function (k) { return this['%h'][String(k).toLowerCase()] !== undefined; };
Headers.prototype['delete'] = function (k) { delete this['%h'][String(k).toLowerCase()]; };
Headers.prototype.forEach = function (f, t) { var o = this['%h']; for (var k in o) f.call(t, o[k], k, this); };
Headers.prototype.entries = function () { var r = []; this.forEach(function (v, k) { r.push([k, v]); }); return r[Symbol.iterator](); };
Headers.prototype.keys = function () { var r = []; this.forEach(function (v, k) { r.push(k); }); return r[Symbol.iterator](); };
Headers.prototype[Symbol.iterator] = Headers.prototype.entries;
function __ctype(u) { return /\.json(\?|#|$)/i.test(u) ? 'application/json' : /\.js(\?|#|$)/i.test(u) ? 'text/javascript' : /\.(html?)(\?|#|$)/i.test(u) ? 'text/html' : 'text/plain'; }
function Response(body, init) {
    init = init || {};
    this.status = init.status === undefined ? 200 : init.status; this.ok = this.status >= 200 && this.status < 300;
    this.statusText = init.statusText || (this.ok ? 'OK' : ''); this.headers = new Headers(init.headers);
    this.url = init.url || ''; this.type = 'basic'; this.redirected = false; this.bodyUsed = false;
    def0(this, '%b', body == null ? '' : String(body));
}
Response.prototype.text = function () { this.bodyUsed = true; return Promise.resolve(this['%b']); };
Response.prototype.json = function () { var b = this['%b']; this.bodyUsed = true; return new Promise(function (r) { r(JSON.parse(b)); }); };
Response.prototype.arrayBuffer = function () {
    this.bodyUsed = true;
    var b = this['%f'] ? __fetchBytes(this['%f']) : null;
    return Promise.resolve(b || new TextEncoder().encode(this['%b']).buffer);
};
Response.prototype.blob = function () { this.bodyUsed = true; return Promise.resolve(new Blob([this['%b']], { type: this.headers.get('content-type') || '' })); };
Response.prototype.clone = function () { return new Response(this['%b'], { status: this.status, statusText: this.statusText, headers: this.headers, url: this.url }); };
Response.json = function (d, i) { return new Response(JSON.stringify(d), i); };
Response.error = function () { return new Response('', { status: 0 }); };
function Request(u, init) {
    init = init || {};
    if (u instanceof Request) { init = u; u = u.url; }
    this.url = __resolve(document.baseURI || __href, String(u)); this.method = String(init.method || 'GET').toUpperCase();
    this.headers = new Headers(init.headers); this.body = init.body === undefined ? null : init.body; this.signal = init.signal || null;
    this.credentials = init.credentials || 'same-origin'; this.mode = init.mode || 'cors';
}
function __pending() { return new Promise(function () {}); }
function fetch(u, init) {
    var q;
    try { q = new Request(u, init); } catch (e) { return Promise.reject(e); }
    if (q.signal && q.signal.aborted) return Promise.reject(new DOMException('The operation was aborted.', 'AbortError'));
    if (q.method !== 'GET' && q.method !== 'HEAD') { __say('fetch: ' + q.method + ' ' + q.url + ' not sent'); return __pending(); }
    if (/^data:/i.test(q.url)) {
        var m = /^data:([^,;]*)(;base64)?,(.*)$/i.exec(q.url) || [];
        return Promise.resolve(new Response(m[2] ? atob(m[3] || '') : decodeURIComponent(m[3] || ''), { url: q.url, headers: { 'content-type': m[1] || 'text/plain' } }));
    }
    var t = __fetchText(q.url), r;
    if (t === null) return __pending();
    r = new Response(q.method === 'HEAD' ? '' : t, { url: q.url, headers: { 'content-type': __ctype(q.url) } });
    def0(r, '%f', q.url);   /* (its bytes, for arrayBuffer) */
    return Promise.resolve(r);
}
function XMLHttpRequest() {
    this.readyState = 0; this.status = 0; this.statusText = ''; this.responseText = ''; this.response = '';
    this.responseType = ''; this.responseURL = ''; this.responseXML = null; this.timeout = 0; this.withCredentials = false;
    this.onreadystatechange = this.onload = this.onerror = this.onloadend = this.onabort = this.ontimeout = this.onprogress = null;
    def0(this, '%hd', {}); this.upload = { addEventListener: __noop, removeEventListener: __noop };
}
XMLHttpRequest.UNSENT = 0; XMLHttpRequest.OPENED = 1; XMLHttpRequest.HEADERS_RECEIVED = 2; XMLHttpRequest.LOADING = 3; XMLHttpRequest.DONE = 4;
(function (X) {
    function fire(x, t) {
        var e = new Event(t); __dispatch(x, e);
        if (typeof x['on' + t] === 'function') try { x['on' + t](e); } catch (er) { __say('xhr: ' + er); }
    }
    X.open = function (m, u, async) {
        this['%m'] = String(m).toUpperCase(); this['%u'] = __resolve(document.baseURI || __href, String(u)); this['%a'] = async !== false;
        this.readyState = 1; fire(this, 'readystatechange');
    };
    X.setRequestHeader = function (k, v) { this['%hd'][String(k).toLowerCase()] = String(v); };
    X.overrideMimeType = __noop;
    X.getResponseHeader = function (k) { return String(k).toLowerCase() === 'content-type' && this.readyState === 4 ? __ctype(this['%u']) : null; };
    X.getAllResponseHeaders = function () { return this.readyState === 4 ? 'content-type: ' + __ctype(this['%u']) + '\r\n' : ''; };
    X.abort = function () { this['%x'] = true; this.readyState = 0; fire(this, 'abort'); };
    X.send = function () {
        var x = this, t;
        if (x['%m'] !== 'GET' && x['%m'] !== 'HEAD') { __say('XMLHttpRequest: ' + x['%m'] + ' ' + x['%u'] + ' not sent'); return; }
        t = __fetchText(x['%u']);
        if (t === null) { if (!x['%a']) { x.readyState = 4; x.status = 0; } return; }
        function done() {
            if (x['%x']) return;
            x.status = 200; x.statusText = 'OK'; x.responseURL = x['%u']; x.responseText = t; x.readyState = 4;
            if (x.responseType === 'json') { try { x.response = JSON.parse(t); } catch (e) { x.response = null; } }
            else if (x.responseType === 'arraybuffer') x.response = __fetchBytes(x['%u']);
            else if (x.responseType === 'blob') x.response = new Blob([t]);
            else if (x.responseType === 'document') { try { x.response = x.responseXML = new DOMParser().parseFromString(t, 'text/html'); } catch (e) { x.response = null; } }
            else x.response = t;
            fire(x, 'readystatechange'); fire(x, 'load'); fire(x, 'loadend');
        }
        if (x['%a']) setTimeout(done, 0); else done();
    };
    X.addEventListener = function (t, f, o) { __listen(this, t, f, o); };
    X.removeEventListener = function (t, f) { __unlisten(this, t, f); };
    X.dispatchEvent = function (e) { return __dispatch(this, e); };
})(XMLHttpRequest.prototype);
function AbortSignal() { this.aborted = false; this.reason = undefined; this.onabort = null; }
AbortSignal.prototype.addEventListener = function (t, f, o) { __listen(this, t, f, o); };
AbortSignal.prototype.removeEventListener = function (t, f) { __unlisten(this, t, f); };
AbortSignal.prototype.throwIfAborted = function () { if (this.aborted) throw this.reason; };
AbortSignal.abort = function (r) { var s = new AbortSignal(); s.aborted = true; s.reason = r; return s; };
AbortSignal.timeout = function () { return new AbortSignal(); };
function AbortController() { this.signal = new AbortSignal(); }
AbortController.prototype.abort = function (r) {
    var s = this.signal; if (s.aborted) return;
    s.aborted = true; s.reason = r === undefined ? new DOMException('signal is aborted without reason', 'AbortError') : r;
    var e = new Event('abort'); __dispatch(s, e); if (typeof s.onabort === 'function') s.onabort(e);
};
//@end
function PromiseRejectionEvent(t, o) { var e = new Event(t, o); e.promise = o && o.promise; e.reason = o && o.reason; return e; }
function DOMException(m, n) { this.message = m === undefined ? '' : String(m); this.name = n || 'Error'; }
DOMException.prototype = Object.create(Error.prototype); DOMException.prototype.constructor = DOMException;
function EventTarget() {}
EventTarget.prototype.addEventListener = function (t, f, o) { __listen(this, t, f, o); };
EventTarget.prototype.removeEventListener = function (t, f) { __unlisten(this, t, f); };
EventTarget.prototype.dispatchEvent = function (e) { return __dispatch(this, e); };

//@lazy
// URL, URLSearchParams
function URLSearchParams(init) {
    def0(this, '%l', []);
    if (init instanceof URLSearchParams) init = init.toString();
    if (typeof init === 'string') {
        init = init.replace(/^\?/, '');
        if (init) init.split('&').forEach(function (p) {
            if (!p) return;
            var i = p.indexOf('='), dec = function (s) { try { return decodeURIComponent(s.replace(/\+/g, ' ')); } catch (e) { return s; } };
            this['%l'].push(i < 0 ? [dec(p), ''] : [dec(p.substring(0, i)), dec(p.substring(i + 1))]);
        }, this);
    } else if (init && typeof init === 'object') {
        var a = init instanceof Array ? init : (typeof init[Symbol.iterator] === 'function' ? Array.from(init) : Object.entries(init));
        for (var j = 0; j < a.length; j++) this['%l'].push([String(a[j][0]), String(a[j][1])]);
    }
}
(function (P) {
    function enc(s) { return encodeURIComponent(s).replace(/%20/g, '+').replace(/[!'()~]/g, function (c) { return '%' + c.charCodeAt(0).toString(16).toUpperCase(); }); }
    function up(p) { if (p['%u']) p['%u']['%q'](p.toString()); }
    P.get = function (k) { var l = this['%l']; for (var i = 0; i < l.length; i++) if (l[i][0] === String(k)) return l[i][1]; return null; };
    P.getAll = function (k) { return this['%l'].filter(function (e) { return e[0] === String(k); }).map(function (e) { return e[1]; }); };
    P.has = function (k) { return this.get(k) !== null; };
    P.append = function (k, v) { this['%l'].push([String(k), String(v)]); up(this); };
    P.set = function (k, v) {
        var l = this['%l'], f = false; k = String(k);
        for (var i = 0; i < l.length; i++) if (l[i][0] === k) { if (f) { l.splice(i--, 1); } else { l[i][1] = String(v); f = true; } }
        if (!f) l.push([k, String(v)]);
        up(this);
    };
    P['delete'] = function (k) { def0(this, '%l', this['%l'].filter(function (e) { return e[0] !== String(k); })); up(this); };
    P.sort = function () { this['%l'].sort(function (a, b) { return a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0; }); up(this); };
    P.forEach = function (f, t) { var l = this['%l']; for (var i = 0; i < l.length; i++) f.call(t, l[i][1], l[i][0], this); };
    P.toString = function () { return this['%l'].map(function (e) { return enc(e[0]) + '=' + enc(e[1]); }).join('&'); };
    P.entries = function () { return this['%l'].map(function (e) { return [e[0], e[1]]; })[Symbol.iterator](); };
    P.keys = function () { return this['%l'].map(function (e) { return e[0]; })[Symbol.iterator](); };
    P.values = function () { return this['%l'].map(function (e) { return e[1]; })[Symbol.iterator](); };
    P[Symbol.iterator] = P.entries;
    Object.defineProperty(P, 'size', { get: function () { return this['%l'].length; } });
})(URLSearchParams.prototype);
// a reference against a base the browser's resolver does not take (not http: x:/, blob:, ...)
function __urlJoin(b, r) {
    if (/^[a-z][a-z0-9+.-]*:/i.test(r)) return r;
    var m = /^([a-z][a-z0-9+.-]*:)(\/\/[^\/?#]*)?([^?#]*)(\?[^#]*)?/i.exec(b);
    if (!m) return r;
    var sch = m[1], auth = m[2] || '', path = m[3] || '', q = m[4] || '';
    if (r.substring(0, 2) === '//') return sch + r;
    if (r.charAt(0) === '#') return sch + auth + path + q + r;
    if (r.charAt(0) === '?') return sch + auth + path + r;
    var t = /^([^?#]*)(.*)$/.exec(r), p = t[1];
    if (p.charAt(0) !== '/') p = (auth && !path ? '/' : path.replace(/[^\/]*$/, '')) + p;
    var out = [], seg = p.split('/');
    for (var i = 0; i < seg.length; i++) {
        if (seg[i] === '..') { if (out.length > 1) out.pop(); if (i === seg.length - 1) out.push(''); }
        else if (seg[i] === '.') { if (i === seg.length - 1) out.push(''); }
        else out.push(seg[i]);
    }
    return sch + auth + out.join('/') + t[2];
}
function URL(u, base) {
    var s = String(u);
    if (base !== undefined) {
        var b = String(base instanceof URL ? base.href : base);
        s = /^https?:/i.test(b) ? __resolve(b, s) : __urlJoin(b, s);
    } else if (/^https?:\/\/[^\/]/i.test(s) && /\/\.\.?(\/|[?#]|$)/.test(s)) s = __resolve(s, s);   /* (dot segments) */
    var m = /^([a-z][a-z0-9+.-]*:)(\/\/(([^\/?#@]*)@)?([^\/?#:]*|\[[^\]]*\])(:(\d*))?)?([^?#]*)(\?[^#]*)?(#.*)?$/i.exec(s);
    if (!m || (base === undefined && !/^[a-z][a-z0-9+.-]*:/i.test(s))) throw new TypeError("Failed to construct 'URL': Invalid URL");
    var ui = (m[4] || '').split(':');
    def0(this, '%p', { protocol: m[1].toLowerCase(), username: ui[0] || '', password: ui[1] || '', hostname: (m[5] || '').toLowerCase(),
                       port: m[7] || '', pathname: m[8] || (m[2] ? '/' : ''), search: m[9] && m[9] !== '?' ? m[9] : '', hash: m[10] && m[10] !== '#' ? m[10] : '', slashes: !!m[2] });
    var me = this, sp = new URLSearchParams(this['%p'].search);
    def0(sp, '%u', this);
    def0(this, '%q', function (q) { me['%p'].search = q ? '?' + q : ''; });
    def0(this, '%sp', sp);
}
(function (P) {
    function acc(n, set) { Object.defineProperty(P, n, { get: function () { return this['%p'][n]; }, set: set || function (v) { this['%p'][n] = String(v); }, enumerable: true, configurable: true }); }
    ['protocol', 'username', 'password', 'hostname', 'port', 'pathname'].forEach(function (n) { acc(n); });
    acc('search', function (v) { v = String(v); this['%p'].search = v && v !== '?' ? (v.charAt(0) === '?' ? v : '?' + v) : ''; def0(this['%sp'], '%l', new URLSearchParams(this['%p'].search)['%l']); });
    acc('hash', function (v) { v = String(v); this['%p'].hash = v && v !== '#' ? (v.charAt(0) === '#' ? v : '#' + v) : ''; });
    Object.defineProperty(P, 'host', { get: function () { var p = this['%p']; return p.hostname + (p.port ? ':' + p.port : ''); }, set: function (v) { var a = String(v).split(':'); this['%p'].hostname = a[0]; this['%p'].port = a[1] || ''; }, enumerable: true });
    Object.defineProperty(P, 'origin', { get: function () { var p = this['%p']; return p.slashes ? p.protocol + '//' + this.host : 'null'; }, enumerable: true });
    Object.defineProperty(P, 'searchParams', { get: function () { return this['%sp']; }, enumerable: true });
    Object.defineProperty(P, 'href', { get: function () {
        var p = this['%p'], a = p.username ? p.username + (p.password ? ':' + p.password : '') + '@' : '';
        return p.protocol + (p.slashes ? '//' + a + this.host : '') + p.pathname + p.search + p.hash;
    }, set: function (v) { var n = new URL(v); def0(this, '%p', n['%p']); def0(this['%sp'], '%l', n['%sp']['%l']); }, enumerable: true });
    P.toString = P.toJSON = function () { return this.href; };
})(URL.prototype);
URL.canParse = function (u, b) { try { new URL(u, b); return true; } catch (e) { return false; } };
URL.createObjectURL = function () { return 'blob:' + __loc.origin + '/0'; };
URL.revokeObjectURL = __noop;
var webkitURL = URL;
function FormData(form) {
    def0(this, '%l', []);
    if (form && form.elements) for (var i = 0; i < form.elements.length; i++) { var e = form.elements[i]; if (e.name && !e.disabled && !((e.type === 'checkbox' || e.type === 'radio') && !e.checked)) this.append(e.name, e.value); }
}
FormData.prototype = Object.create(URLSearchParams.prototype); FormData.prototype.constructor = FormData;
//@end

//@lazy
// atob / btoa, TextEncoder / TextDecoder (UTF-8)
var __b64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
function btoa(s) {
    s = String(s); var r = '', i, a, b, c;
    for (i = 0; i < s.length; i += 3) {
        a = s.charCodeAt(i); b = s.charCodeAt(i + 1); c = s.charCodeAt(i + 2);
        if (a > 255 || b > 255 || c > 255) throw new DOMException('The string to be encoded contains characters outside of the Latin1 range.', 'InvalidCharacterError');
        r += __b64.charAt(a >> 2) + __b64.charAt(((a & 3) << 4) | (b >> 4)) +
             (i + 1 < s.length ? __b64.charAt(((b & 15) << 2) | (c >> 6)) : '=') + (i + 2 < s.length ? __b64.charAt(c & 63) : '=');
    }
    return r;
}
function atob(s) {
    s = String(s).replace(/[\s=]+/g, ''); var r = '', i, n = 0, bits = 0, v;
    for (i = 0; i < s.length; i++) {
        v = __b64.indexOf(s.charAt(i)); if (v < 0) throw new DOMException('The string to be decoded is not correctly encoded.', 'InvalidCharacterError');
        n = (n << 6) | v; bits += 6;
        if (bits >= 8) { bits -= 8; r += String.fromCharCode((n >> bits) & 255); }
    }
    return r;
}
function TextEncoder() { this.encoding = 'utf-8'; }
TextEncoder.prototype.encode = function (s) {
    s = s === undefined ? '' : String(s); var o = [], i, c, d;
    for (i = 0; i < s.length; i++) {
        c = s.charCodeAt(i);
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < s.length) { d = s.charCodeAt(i + 1); if (d >= 0xDC00 && d < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00); i++; } }
        if (c < 0x80) o.push(c);
        else if (c < 0x800) o.push(0xC0 | (c >> 6), 0x80 | (c & 63));
        else if (c < 0x10000) o.push(0xE0 | (c >> 12), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
        else o.push(0xF0 | (c >> 18), 0x80 | ((c >> 12) & 63), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
    }
    return new Uint8Array(o);
};
function TextDecoder(l) { this.encoding = 'utf-8'; this.fatal = false; this.ignoreBOM = false; }
TextDecoder.prototype.decode = function (b) {
    if (b == null) return '';
    var a = b instanceof ArrayBuffer ? new Uint8Array(b) : b.buffer instanceof ArrayBuffer && !(b instanceof Uint8Array) ? new Uint8Array(b.buffer, b.byteOffset, b.byteLength) : b;
    var r = '', i = 0, c, n = a.length;
    while (i < n) {
        c = a[i++];
        if (c >= 0xF0 && i + 2 < n + 0) { c = ((c & 7) << 18) | ((a[i] & 63) << 12) | ((a[i + 1] & 63) << 6) | (a[i + 2] & 63); i += 3; c -= 0x10000; r += String.fromCharCode(0xD800 + (c >> 10), 0xDC00 + (c & 1023)); continue; }
        if (c >= 0xE0) { c = ((c & 15) << 12) | ((a[i] & 63) << 6) | (a[i + 1] & 63); i += 2; }
        else if (c >= 0xC0) { c = ((c & 31) << 6) | (a[i] & 63); i++; }
        r += String.fromCharCode(c);
    }
    return r;
};

// Blob, File, FormData (not sent anywhere), crypto, structuredClone, customElements
function Blob(parts, o) { var s = (parts || []).map(function (p) { return typeof p === 'string' ? p : p && p['%s'] !== undefined ? p['%s'] : String(p); }).join(''); def0(this, '%s', s); this.size = s.length; this.type = (o && o.type) || ''; }
Blob.prototype.text = function () { return Promise.resolve(this['%s']); };
Blob.prototype.slice = function (a, b, t) { return new Blob([this['%s'].slice(a, b)], { type: t }); };
function File(parts, name, o) { Blob.call(this, parts, o); this.name = String(name); this.lastModified = Date.now(); }
File.prototype = Object.create(Blob.prototype);
function FileReader() { this.result = null; this.readyState = 0; this.onload = null; }
FileReader.prototype.readAsText = function (b) { var r = this; setTimeout(function () { r.result = b['%s']; r.readyState = 2; if (r.onload) r.onload({ target: r }); }, 0); };
FileReader.prototype.readAsDataURL = function (b) { var r = this; setTimeout(function () { r.result = 'data:' + b.type + ';base64,' + btoa(unescape(encodeURIComponent(b['%s']))); r.readyState = 2; if (r.onload) r.onload({ target: r }); }, 0); };
FileReader.prototype.addEventListener = function (t, f) { if (t === 'load') this.onload = f; };
//@end
//@lazy
var crypto = {
    getRandomValues: function (a) { for (var i = 0; i < a.length; i++) a[i] = Math.floor(Math.random() * 256); return a; },
    randomUUID: function () { return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, function (c) { var r = Math.random() * 16 | 0; return (c === 'x' ? r : (r & 3 | 8)).toString(16); }); },
    subtle: {}
};
function structuredClone(v) {
    var seen = [];
    function c(x) {
        if (x === null || typeof x !== 'object') return x;
        if (x instanceof Date) return new Date(x.getTime());
        if (x instanceof RegExp) return new RegExp(x.source, (x.global ? 'g' : '') + (x.ignoreCase ? 'i' : '') + (x.multiline ? 'm' : ''));
        for (var i = 0; i < seen.length; i += 2) if (seen[i] === x) return seen[i + 1];
        var r;
        if (x instanceof Map) { r = new Map(); seen.push(x, r); x.forEach(function (v, k) { r.set(c(k), c(v)); }); return r; }
        if (x instanceof Set) { r = new Set(); seen.push(x, r); x.forEach(function (v) { r.add(c(v)); }); return r; }
        r = x instanceof Array ? [] : {}; seen.push(x, r);
        for (var k in x) if (Object.prototype.hasOwnProperty.call(x, k)) r[k] = c(x[k]);
        return r;
    }
    return c(v);
}
//@end
var customElements = (function () {
    var defs = {}, waits = {};
    function upgrade(el, C) {
        if (el['%ce']) return;
        def0(el, '%ce', true);
        try {
            Object.setPrototypeOf(el, C.prototype);
            C.call(el);
            if (typeof el.connectedCallback === 'function') el.connectedCallback();
            var ao = C.observedAttributes;
            if (ao && typeof el.attributeChangedCallback === 'function')
                for (var i = 0; i < ao.length; i++) if (el.hasAttribute(ao[i])) el.attributeChangedCallback(ao[i], null, el.getAttribute(ao[i]));
        } catch (e) { __say('custom element <' + el.localName + '>: ' + e); }
    }
    var ce = {
        define: function (n, C) {
            n = String(n).toLowerCase();
            if (defs[n]) throw new DOMException('the name "' + n + '" has already been used with this registry', 'NotSupportedError');
            defs[n] = C;
            var l = document.querySelectorAll(n);
            for (var i = 0; i < l.length; i++) upgrade(l[i], C);
            if (waits[n]) { waits[n].forEach(function (r) { r(C); }); delete waits[n]; }
        },
        get: function (n) { return defs[String(n).toLowerCase()]; },
        whenDefined: function (n) { n = String(n).toLowerCase(); return defs[n] ? Promise.resolve(defs[n]) : new Promise(function (r) { (waits[n] = waits[n] || []).push(r); }); },
        upgrade: function (root) { for (var n in defs) { var l = root.querySelectorAll ? root.querySelectorAll(n) : []; for (var i = 0; i < l.length; i++) upgrade(l[i], defs[n]); } },
        '%up': function (el) { var C = el && el.localName && defs[el.localName]; if (C) upgrade(el, C); return el; }
    };
    return ce;
})();
(function (D) {
    var ce = D.createElement;
    D.createElement = function (n, o) { return customElements['%up'](ce.call(this, n, o)); };
})(document);
//@lazy
function MessageChannel() {
    var a = { onmessage: null }, b = { onmessage: null };
    a.postMessage = function (d) { setTimeout(function () { if (b.onmessage) b.onmessage({ data: d }); }, 0); };
    b.postMessage = function (d) { setTimeout(function () { if (a.onmessage) a.onmessage({ data: d }); }, 0); };
    a.start = b.start = a.close = b.close = __noop; a.addEventListener = function (t, f) { if (t === 'message') a.onmessage = f; }; b.addEventListener = function (t, f) { if (t === 'message') b.onmessage = f; };
    this.port1 = a; this.port2 = b;
}
function BroadcastChannel() { this.postMessage = this.close = __noop; this.onmessage = null; this.addEventListener = this.removeEventListener = __noop; }
function Worker() { throw new Error('Workers are not supported'); }
//@end
var Reflect = {
    apply: function (f, t, a) { return Function.prototype.apply.call(f, t, a); },
    construct: function (C, a) { return this['%c'](C, a); },
    defineProperty: function (o, k, d) { try { Object.defineProperty(o, k, d); return true; } catch (e) { return false; } },
    deleteProperty: function (o, k) { return delete o[k]; },
    get: function (o, k) { return o[k]; }, set: function (o, k, v) { o[k] = v; return true; },
    has: function (o, k) { return k in o; }, ownKeys: function (o) { return Object.getOwnPropertyNames(o); },
    getPrototypeOf: Object.getPrototypeOf, setPrototypeOf: function (o, p) { Object.setPrototypeOf(o, p); return true; },
    getOwnPropertyDescriptor: Object.getOwnPropertyDescriptor,
    isExtensible: Object.isExtensible, preventExtensions: function (o) { Object.preventExtensions(o); return true; }
};
def0(Reflect, '%c', window['%construct']);

// ES modules (<script type="module">; js/mujs/jsparse.c compiles one into a function (namespace,
// meta) that imports with %imp first): each module's namespace by URL, run once. A module whose
// text the browser has not fetched yet is noted (__fetchText) and stops the ones importing it:
// the page runs again when the browser has it.
//@lazy %imp %impcheck %exp %expall %dimp __runModule
(function (G) {
    var mods = {}, MISSING = { missing: 'a module not fetched yet' };
    function hid(n, f) { Object.defineProperty(G, n, { value: f, writable: true, configurable: true, enumerable: false }); }
    function run(t, u, key) {
        var ns = {}, meta = { url: u, resolve: function (s) { return __resolve(u, String(s)); } };
        Object.defineProperty(ns, '@@toStringTag', { value: 'Module' });
        mods[key] = ns;
        try {   /* (an async function: top-level await; what it threw before its first await is thrown here) */
            var p = __module(t, u.replace(/[?#].*$/, '').replace(/^.*\//, '') || u)(ns, meta);
            if (p && p['%s'] === 2) { p.then(undefined, __noop); throw p['%v']; }
        } catch (e) { delete mods[key]; throw e; }
        return ns;
    }
    function load(u) {
        if (Object.prototype.hasOwnProperty.call(mods, u)) return mods[u];
        var t = __fetchText(u, 1);
        return t == null ? null : run(t, u, u);
    }
    hid('%imp', function (spec, meta) {
        var u = __resolve(meta.url, String(spec)), ns;
        try { ns = load(u); } catch (e) { if (e !== MISSING) throw e; ns = null; }
        if (!ns) { meta['%p'] = (meta['%p'] || 0) + 1; return {}; }
        return ns;
    });
    hid('%impcheck', function (meta) { if (meta['%p']) throw MISSING; });
    hid('%exp', function (ns, k, g) { Object.defineProperty(ns, k, { get: g, enumerable: true, configurable: true }); });
    hid('%expall', function (ns, m) {
        Object.keys(m).forEach(function (k) { if (k !== 'default' && !Object.prototype.hasOwnProperty.call(ns, k)) G['%exp'](ns, k, function () { return m[k]; }); });
    });
    hid('%dimp', function (spec, meta) {
        var u = __resolve(meta ? meta.url : __href, String(spec));
        return new Promise(function (res, rej) {
            var ns = null;
            try { ns = load(u); } catch (e) { if (e !== MISSING) { rej(e); return; } }
            if (ns) res(ns); else rej(new TypeError('Failed to fetch dynamically imported module: ' + u));
        });
    });
    G.__runModule = function (t, u, name) { try { run(t, u, u === __href ? __href + '#' + name : u); } catch (e) { if (e !== MISSING) throw e; } };
})(this);
//@end

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
