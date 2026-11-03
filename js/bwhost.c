/* Browser 9300: a page's scripts (see bwhost.h).
 *
 * The page becomes a tree (bwdom.c) that its scripts see as the DOM (bwdomjs.c, prelude.js).
 * Scripts run in document order, one at a time: the first script element not run yet, so the
 * ones a script adds (document.write, createElement + appendChild) run in their turn too.
 * What a script writes is read into the tree right after it. Then DOMContentLoaded, load and
 * <body onload>, then the timers due in the first few seconds (in a virtual time), and any
 * scripts those added. The tree, if it changed, goes back to the browser as HTML.
 *
 * External scripts come from the host (fetch); the ones it did not have are listed in
 * pg->missing for it to fetch before the next run. An error ends that script only; running out
 * of time or memory ends them all (the page is then as they left it). C89, no static data;
 * tested on a PC (test/pagetest.c) and in the ARM emulator (test/emu). */

#include <string.h>
#include <stdlib.h>
#include "bwjs.h"
#include "bwhost.h"
#include "prelude.h"

/* from the browser's core (core/bwurl.c, igutil.c) */
char* bw_url_resolve(const char* base, const char* rel, unsigned int relLen);
void ig_free(void* p);

#define BW_MAX_SCRIPTS 600
#define BW_MAX_INLINE (128 * 1024)      /* bigger inline ones (page data, mostly) are not run */
#define BW_MAX_EXTERNAL (512 * 1024)
#define BW_MAX_NODES 150000
#define BW_TIMER_MS 3000                /* the timers of the page's first 3 s */
#define BW_TIMER_CALLS 300

/* ------------------------------------------------------------------ the quick scan (raw HTML) */

static int lc(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int at_ci(const char* s, unsigned int n, unsigned int i, const char* lit)
{
    unsigned int k;
    for (k = 0; lit[k]; k++) if (i + k >= n || lc((unsigned char)s[i + k]) != lit[k]) return 0;
    return 1;
}

static unsigned int find_ci(const char* s, unsigned int n, unsigned int from, const char* lit)
{
    unsigned int i;
    for (i = from; i < n; i++) if (lc((unsigned char)s[i]) == lit[0] && at_ci(s, n, i, lit)) return i;
    return n;
}

static int name_end(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '>' || c == '/'; }

static unsigned int tag_end(const char* s, unsigned int n, unsigned int i)
{
    char q = 0;
    for (; i < n; i++) {
        if (q) { if (s[i] == q) q = 0; }
        else if (s[i] == '"' || s[i] == '\'') q = s[i];
        else if (s[i] == '>') return i;
    }
    return n;
}

/* attribute name's value in the tag s[a..b): 0 and its place, or -1 */
static int attr(const char* s, unsigned int a, unsigned int b, const char* name, unsigned int* vs, unsigned int* vl)
{
    unsigned int i = a;
    while (i < b) {
        unsigned int ns, ne;
        while (i < b && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r' || s[i] == '\f' || s[i] == '/')) i++;
        ns = i;
        while (i < b && !name_end((unsigned char)s[i]) && s[i] != '=') i++;
        ne = i;
        while (i < b && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
        *vs = i; *vl = 0;
        if (i < b && s[i] == '=') {
            i++;
            while (i < b && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++;
            if (i < b && (s[i] == '"' || s[i] == '\'')) {
                char q = s[i++];
                *vs = i;
                while (i < b && s[i] != q) i++;
                *vl = i - *vs;
                if (i < b) i++;
            } else {
                *vs = i;
                while (i < b && !name_end((unsigned char)s[i])) i++;
                *vl = i - *vs;
            }
        }
        if (ne > ns && ne - ns == strlen(name) && at_ci(s, b, ns, name)) return 0;
        if (ne == ns) i++;
    }
    return -1;
}

/* is the script type (or language) JavaScript? */
static int js_type_ok(const char* t, unsigned int n, int isLanguage)
{
    static const char* const ok[] = { "text/javascript", "application/javascript", "application/x-javascript",
        "text/ecmascript", "application/ecmascript", "text/jscript", "text/livescript", "text/x-javascript", 0 };
    unsigned int e, k, s = 0;
    while (s < n && (t[s] == ' ' || t[s] == '\t')) s++;
    if (s == n) return 1;
    if (isLanguage)
        return at_ci(t, n, s, "javascript") || at_ci(t, n, s, "jscript") || at_ci(t, n, s, "ecmascript") || at_ci(t, n, s, "livescript");
    e = s;
    while (e < n && t[e] != ';' && t[e] != ' ') e++;
    for (k = 0; ok[k]; k++) if (e - s == strlen(ok[k]) && at_ci(t, e, s, ok[k])) return 1;
    return 0;
}

static unsigned int skip_raw(const char* s, unsigned int n, unsigned int i)
{
    static const char* const raw[] = { "noscript", "textarea", "xmp", "style", "template", "title", "noembed", "noframes", 0 };
    int k;
    for (k = 0; raw[k]; k++) {
        unsigned int l = (unsigned int)strlen(raw[k]);
        if (at_ci(s, n, i + 1, raw[k]) && i + 1 + l < n && name_end((unsigned char)s[i + 1 + l])) {
            char close[16];
            unsigned int c;
            close[0] = '<'; close[1] = '/';
            memcpy(close + 2, raw[k], l + 1);
            c = find_ci(s, n, i + 1 + l, close);
            return c >= n ? n : tag_end(s, n, c) + 1;
        }
    }
    return 0;
}

typedef struct { unsigned int start, tagEnd, end; int isJs; int hasSrc; unsigned int srcS, srcL; } Script;

static int next_script(const char* s, unsigned int n, unsigned int i, Script* sc)
{
    while (i < n) {
        unsigned int k;
        if (s[i] != '<') { const char* q = (const char*)memchr(s + i, '<', n - i); if (!q) return 0; i = (unsigned int)(q - s); }
        if (i + 4 <= n && s[i + 1] == '!' && s[i + 2] == '-' && s[i + 3] == '-') {
            unsigned int c = find_ci(s, n, i + 4, "-->");
            i = c >= n ? n : c + 3;
            continue;
        }
        if (at_ci(s, n, i + 1, "script") && i + 7 < n && name_end((unsigned char)s[i + 7])) {
            unsigned int te = tag_end(s, n, i + 7), vs, vl, ce;
            sc->start = i;
            sc->tagEnd = te;
            ce = find_ci(s, n, te < n ? te + 1 : n, "</script");
            sc->end = ce >= n ? n : tag_end(s, n, ce + 8) + 1;
            if (sc->end > n) sc->end = n;
            sc->hasSrc = attr(s, i + 7, te, "src", &sc->srcS, &sc->srcL) == 0;
            if (attr(s, i + 7, te, "type", &vs, &vl) == 0) sc->isJs = js_type_ok(s + vs, vl, 0);
            else if (attr(s, i + 7, te, "language", &vs, &vl) == 0) sc->isJs = js_type_ok(s + vs, vl, 1);
            else sc->isJs = 1;
            return 1;
        }
        k = skip_raw(s, n, i);
        i = k ? k : i + 1;
    }
    return 0;
}

int bw_js_has_scripts(const char* html, unsigned int len)
{
    Script sc;
    unsigned int i = 0;
    while (next_script(html, len, i, &sc)) {
        if (sc.isJs) return 1;
        i = sc.end;
    }
    return 0;
}

/* an absolute URL for a raw (entity-encoded) src, malloc'ed; NULL for none / javascript: */
static char* script_url(const char* base, const char* raw, unsigned int rawLen)
{
    BwBuf dec;
    char* u;
    char* r;
    unsigned int s = 0;
    memset(&dec, 0, sizeof dec);
    while (s < rawLen && (raw[s] == ' ' || raw[s] == '\t' || raw[s] == '\n')) s++;
    if (s >= rawLen) return 0;
    bwd_unescape(raw + s, rawLen - s, &dec);
    if (!dec.p || dec.oom) { free(dec.p); return 0; }
    if (!strncmp(dec.p, "javascript:", 11) || !strncmp(dec.p, "data:", 5)) { free(dec.p); return 0; }
    u = bw_url_resolve(base, dec.p, dec.n);
    free(dec.p);
    if (!u) return 0;
    r = (char*)malloc(strlen(u) + 1);
    if (r) strcpy(r, u);
    ig_free(u);
    return r;
}

static int listed(const BwBuf* b, const char* url)
{
    unsigned int l = (unsigned int)strlen(url);
    const char* p = b->p;
    if (!p) return 0;
    while (*p) {
        const char* e = strchr(p, '\n');
        unsigned int n = e ? (unsigned int)(e - p) : (unsigned int)strlen(p);
        if (n == l && !memcmp(p, url, l)) return 1;
        if (!e) break;
        p = e + 1;
    }
    return 0;
}

char* bw_js_script_urls(const char* html, unsigned int len, const char* base, unsigned int* outLen)
{
    Script sc;
    unsigned int i = 0;
    BwBuf b;
    memset(&b, 0, sizeof b);
    while (next_script(html, len, i, &sc)) {
        if (sc.isJs && sc.hasSrc) {
            char* u = script_url(base, html + sc.srcS, sc.srcL);
            if (u && !listed(&b, u)) { bwb_str(&b, u); bwb_add(&b, "\n", 1); }
            free(u);
        }
        i = sc.end;
    }
    if (b.oom) { free(b.p); b.p = 0; b.n = 0; }
    *outLen = b.n;
    return b.p;
}

/* ------------------------------------------------------------------ the host functions */

static void h_write(js_State* J)
{
    BwJsDom* d = (BwJsDom*)js_getcontext(J);
    const char* s = js_tostring(J, 1);
    unsigned int n = (unsigned int)strlen(s);
    if (!d->parsing) { js_pushundefined(J); return; }     /* (after load it would replace the page: ignored) */
    if (d->writtenTotal + n > BW_MAX_WRITTEN) js_error(J, "document.write: too much");
    d->writtenTotal += n;
    bwb_add(&d->wr, s, n);
    if (d->wr.oom) js_error(J, "out of memory");
    js_pushundefined(J);
}

/* eval as a function (indirect eval): the code in the global scope, its value back */
static void h_eval(js_State* J)
{
    if (!js_isstring(J, 1)) { js_copy(J, 1); return; }
    js_loadstring(J, "[eval]", js_tostring(J, 1));
    js_pushundefined(J);
    js_call(J, 0);
}

static void h_say(js_State* J) { bwdj_note((BwJsDom*)js_getcontext(J), js_tostring(J, 1), 0); js_pushundefined(J); }

static void h_navigate(js_State* J)
{
    BwJsDom* d = (BwJsDom*)js_getcontext(J);
    const char* s = js_tostring(J, 1);
    d->navigate.n = 0;
    bwb_str(&d->navigate, s);
    js_pushundefined(J);
}

static void h_setcookie(js_State* J)
{
    BwJsDom* d = (BwJsDom*)js_getcontext(J);
    const char* s = js_tostring(J, 1);
    if (d->cookies.n < 16384 && !strchr(s, '\n')) { bwb_str(&d->cookies, s); bwb_add(&d->cookies, "\n", 1); }
    js_pushundefined(J);
}

/* ------------------------------------------------------------------ running */

typedef struct
    {
    BwJsDom d;
    BwJsPage* pg;
    js_State* J;
    char* base;
    BwBuf missing;
    int data;               /* fetch() / XMLHttpRequest URLs asked for */
    } Run;

#define BW_MAX_DATA 12      /* fetch() / XMLHttpRequest GETs a page may have the browser fetch */

/* __fetchText(url): the text the browser fetched for an absolute URL, or null (then the URL is
 * listed for the browser to fetch before the next run) */
static void h_fetchtext(js_State* J)
{
    Run* r = (Run*)js_getcontext(J);
    const char* url = js_tostring(J, 1);
    const char* text;
    unsigned int len = 0;
    if (strncmp(url, "http://", 7) && strncmp(url, "https://", 8)) { js_pushnull(J); return; }
    text = r->pg->fetch ? r->pg->fetch(r->pg->fetchCtx, url, &len) : 0;
    if (text) { js_pushstring(J, text); return; }
    if (!listed(&r->missing, url) && r->data < BW_MAX_DATA) {
        r->data++;
        bwb_str(&r->missing, url);
        bwb_add(&r->missing, "\n", 1);
        r->pg->externalMissing++;
    }
    js_pushnull(J);
}

/* __resolve(base, url): url made absolute */
static void h_resolve(js_State* J)
{
    const char* u = js_tostring(J, 2);
    char* a = script_url(js_tostring(J, 1), u, (unsigned int)strlen(u));
    if (!a) { js_pushstring(J, u); return; }
    js_pushstring(J, a);
    free(a);
}

static int fatal(const char* m)
{
    return strstr(m, "ran too long") != 0 || strstr(m, "out of memory") != 0 || strstr(m, "too much") != 0;
}

static void num(char* b, unsigned int v)
{
    char t[12];
    int i = 0, j;
    do { t[i++] = (char)('0' + v % 10); v /= 10; } while (v && i < 11);
    for (j = 0; j < i; j++) b[j] = t[i - 1 - j];
    b[i] = 0;
}

static void note_error(Run* r, const char* where, const char* m)
{
    char line[400];
    unsigned int l = 0, k;
    const char* parts[4];
    parts[0] = "error in "; parts[1] = where; parts[2] = ": "; parts[3] = m;
    for (k = 0; k < 4; k++) {
        unsigned int n = (unsigned int)strlen(parts[k]);
        if (l + n >= sizeof line) n = sizeof line - 1 - l;
        memcpy(line + l, parts[k], n);
        l += n;
    }
    line[l] = 0;
    bwdj_note(&r->d, line, 0);
    r->pg->failed++;
    if (fatal(m)) r->pg->stopped = 1;
}

/* the promise jobs a script or callback left (js/es.js), and its unhandled rejections noted */
static void run_jobs(Run* r)
{
    js_State* J = r->J;
    int i, n;
    js_getglobal(J, "%runJobs");
    if (!js_iscallable(J, -1)) { js_pop(J, 1); return; }
    js_pushundefined(J);
    js_pushnumber(J, 20000);
    if (js_pcall(J, 1)) note_error(r, "a promise job", js_tostring(J, -1));
    js_pop(J, 1);
    js_getglobal(J, "%unhandled");
    js_pushundefined(J);
    if (js_pcall(J, 0) || !js_isarray(J, -1)) { js_pop(J, 1); return; }
    n = js_getlength(J, -1);
    for (i = 0; i < n && i < 3; i++) {
        js_getindex(J, -1, i);
        js_getproperty(J, -1, "%v");
        bwdj_note(&r->d, "unhandled promise rejection: ", js_trystring(J, -1, "?"));
        js_pop(J, 2);
    }
    js_pop(J, 1);
}

/* runs a global function by name with a number argument; 0 or -1 if it threw */
static int call_global(Run* r, const char* name, double a, double b, int nargs, double* result)
{
    js_State* J = r->J;
    int err;
    js_getglobal(J, name);
    js_pushundefined(J);
    if (nargs > 0) js_pushnumber(J, a);
    if (nargs > 1) js_pushnumber(J, b);
    err = js_pcall(J, nargs);
    if (err) note_error(r, name, js_tostring(J, -1));
    else if (result) *result = js_tonumber(J, -1);
    js_pop(J, 1);
    if (!r->pg->stopped) run_jobs(r);
    return err ? -1 : 0;
}

static BwNode* next_script_node(BwTree* t)
{
    BwNode* n = t->doc;
    while ((n = bwd_next(t->doc, n, 1)) != 0)
        if (n->type == BWN_ELEMENT && !(n->flags & BWF_RAN) && !strcmp(n->name, "script")) return n;
    return 0;
}

static int script_is_js(BwNode* s)
{
    const char* v = bwd_attr(s, "type");
    if (v) return js_type_ok(v, (unsigned int)strlen(v), 0);
    v = bwd_attr(s, "language");
    if (v) return js_type_ok(v, (unsigned int)strlen(v), 1);
    return 1;
}

/* the source of an inline script, without an old "<!-- ... -->" wrapper (malloc'ed) */
static char* inline_source(BwNode* s, unsigned int* len)
{
    BwBuf b;
    BwNode* k;
    unsigned int a, e;
    char* r;
    memset(&b, 0, sizeof b);
    for (k = s->first; k; k = k->next) if (k->type == BWN_TEXT) bwb_str(&b, k->text);
    if (b.oom) { free(b.p); return 0; }
    if (!b.p) { *len = 0; return (char*)calloc(1, 1); }
    a = 0; e = b.n;
    while (a < e && (b.p[a] == ' ' || b.p[a] == '\t' || b.p[a] == '\n' || b.p[a] == '\r')) a++;
    if (a + 4 <= e && !memcmp(b.p + a, "<!--", 4)) { while (a < e && b.p[a] != '\n') a++; }
    while (e > a && (b.p[e - 1] == ' ' || b.p[e - 1] == '\t' || b.p[e - 1] == '\n' || b.p[e - 1] == '\r')) e--;
    if (e >= a + 3 && !memcmp(b.p + e - 3, "-->", 3)) { e -= 3; while (e > a && b.p[e - 1] != '\n') e--; }
    r = (char*)malloc(e - a + 1);
    if (r) { memcpy(r, b.p + a, e - a); r[e - a] = 0; *len = e - a; }
    free(b.p);
    return r;
}

static void set_current(Run* r, BwNode* s)
{
    js_State* J = r->J;
    js_getglobal(J, "document");
    bwdj_push_node(J, s);
    js_setproperty(J, -2, "currentScript");
    js_pop(J, 1);
}

/* what the script wrote: into the tree right after it */
static void flush_written(Run* r, BwNode* s)
{
    BwJsDom* d = &r->d;
    BwNode* f;
    if (!d->wr.n) return;
    r->pg->written += d->wr.n;
    f = bwd_parse_fragment(d->tree, d->wr.p, d->wr.n, 0);
    d->wr.n = 0;
    if (d->wr.p) d->wr.p[0] = 0;
    if (!f) { r->pg->stopped = 1; return; }
    if (s->parent) bwd_insert_before(s->parent, f, s->next);
    else {
        BwNode* body = bwd_child_element(bwd_child_element(d->tree->doc, "html"), "body");
        if (body) bwd_insert_before(body, f, 0);
    }
    d->dirty = 1;
}

/* the error at the top of the stack, with where it happened (the first two calls of its trace) */
static void note_thrown(Run* r, const char* where)
{
    js_State* J = r->J;
    char m[400], tr[160];
    const char* msg;
    unsigned int n, t = 0;
    tr[0] = 0;
    if (js_isobject(J, -1)) {   /* (first: js_tostring makes the value a string) */
        js_getproperty(J, -1, "stackTrace");
        if (js_isstring(J, -1)) {
            const char* st = js_tostring(J, -1);
            int lines = 0;
            while (st[t] && t < sizeof tr - 1) { if (st[t] == '\n' && ++lines == 3) break; tr[t] = st[t] == '\n' || st[t] == '\t' ? ' ' : st[t]; t++; }
            tr[t] = 0;
        }
        js_pop(J, 1);
    }
    msg = js_tostring(J, -1);
    n = (unsigned int)strlen(msg);
    if (n > 250) n = 250;
    memcpy(m, msg, n);
    if (n + t < sizeof m - 1) { memcpy(m + n, tr, t); n += t; }
    m[n] = 0;
    note_error(r, where, m);
}

static void run_one(Run* r, BwNode* s)
{
    BwJsPage* pg = r->pg;
    js_State* J = r->J;
    const char* src = bwd_attr(s, "src");
    char* own = 0;
    const char* text;
    unsigned int len = 0;
    char name[160];
    s->flags |= BWF_RAN;
    if (!script_is_js(s)) { pg->skipped++; return; }
    if (src) {
        char* url = script_url(r->base, src, (unsigned int)strlen(src));
        if (!url) { pg->skipped++; return; }
        text = pg->fetch ? pg->fetch(pg->fetchCtx, url, &len) : 0;
        if (!text) {
            if (!listed(&r->missing, url)) { bwb_str(&r->missing, url); bwb_add(&r->missing, "\n", 1); }
            pg->externalMissing++;
            free(url);
            return;
        }
        {   /* its name in errors: the file */
            const char* f = strrchr(url, '/');
            unsigned int n;
            f = f && f[1] ? f + 1 : url;
            n = (unsigned int)strlen(f);
            if (n > sizeof name - 1) n = sizeof name - 1;
            memcpy(name, f, n);
            name[n] = 0;
            { char* q = strchr(name, '?'); if (q) *q = 0; }
        }
        free(url);
        if (len > BW_MAX_EXTERNAL) { pg->big++; return; }
        pg->external++;
    } else {
        own = inline_source(s, &len);
        if (!own) { pg->stopped = 1; return; }
        if (!len) { free(own); return; }
        if (len > BW_MAX_INLINE) { pg->big++; free(own); return; }
        text = own;
        memcpy(name, "inline script ", 14);
        num(name + 14, (unsigned int)pg->run + 1);
    }
    pg->run++;
    set_current(r, s);
    r->d.current = s;
    r->d.wr.n = 0;
    if (js_ploadstring(J, name, text)) note_thrown(r, name);
    else {
        js_pushundefined(J);
        if (js_pcall(J, 0)) note_thrown(r, name);
    }
    js_pop(J, 1);
    if (!pg->stopped) run_jobs(r);
    free(own);
    flush_written(r, s);
    r->d.current = 0;
}

static int out_of_time(Run* r)
{
    if (!r->pg->stopped && r->pg->expired && r->pg->expired(r->pg->ctx)) {
        r->pg->stopped = 1;
        bwdj_note(&r->d, "the time for the page's scripts is up", 0);
    }
    return r->pg->stopped;
}

static void run_scripts(Run* r)
{
    BwNode* s;
    while (!out_of_time(r) && r->pg->run < BW_MAX_SCRIPTS && (s = next_script_node(r->d.tree)) != 0) run_one(r, s);
}

static char* take(BwBuf* b) { char* p = b->oom ? (free(b->p), (char*)0) : b->p; b->p = 0; return p; }

int bw_js_page(js_State* J, BwJsPage* pg)
{
    Run* r = (Run*)malloc(sizeof(Run));
    BwJsPage keep;
    int result = -1;
    keep = *pg;
    memset(pg, 0, sizeof *pg);
    pg->html = keep.html; pg->len = keep.len; pg->href = keep.href; pg->ua = keep.ua; pg->ref = keep.ref;
    pg->lang = keep.lang; pg->cookie = keep.cookie; pg->w = keep.w; pg->h = keep.h; pg->expired = keep.expired;
    pg->ctx = keep.ctx; pg->fetch = keep.fetch; pg->fetchCtx = keep.fetchCtx;
    if (!r) return -1;
    memset(r, 0, sizeof(Run));
    r->pg = pg;
    r->J = J;
    r->d.tree = bwd_new(BW_MAX_NODES);
    if (!r->d.tree) { free(r); return -1; }
    bwd_parse_document(r->d.tree, pg->html, pg->len);
    pg->nodes = (int)r->d.tree->count;
    if (r->d.tree->oom) { bwdj_note(&r->d, "the page is too big for its scripts", 0); goto done; }
    {   /* <base href> */
        BwNode* head = bwd_child_element(bwd_child_element(r->d.tree->doc, "html"), "head");
        BwNode* b = bwd_child_element(head, "base");
        const char* h = b ? bwd_attr(b, "href") : 0;
        r->base = h ? script_url(pg->href, h, (unsigned int)strlen(h)) : 0;
        if (!r->base) { r->base = (char*)malloc(strlen(pg->href) + 1); if (r->base) strcpy(r->base, pg->href); }
        if (!r->base) goto done;
    }

    js_setcontext(J, &r->d);
    js_newcfunction(J, h_write, "__write", 1); js_setglobal(J, "__write");
    js_newcfunction(J, h_say, "__say", 1); js_setglobal(J, "__say");
    js_newcfunction(J, h_eval, "eval", 1); js_setglobal(J, "eval");
    js_newcfunction(J, h_navigate, "__navigate", 1); js_setglobal(J, "__navigate");
    js_newcfunction(J, h_setcookie, "__setCookie", 1); js_setglobal(J, "__setCookie");
    js_newcfunction(J, h_fetchtext, "__fetchText", 1); js_setglobal(J, "__fetchText");
    js_newcfunction(J, h_resolve, "__resolve", 2); js_setglobal(J, "__resolve");
    js_pushstring(J, pg->href ? pg->href : ""); js_setglobal(J, "__href");
    js_pushstring(J, pg->ua ? pg->ua : ""); js_setglobal(J, "__ua");
    js_pushstring(J, pg->ref ? pg->ref : ""); js_setglobal(J, "__ref");
    js_pushstring(J, pg->lang ? pg->lang : "en"); js_setglobal(J, "__lang");
    js_pushstring(J, pg->cookie ? pg->cookie : ""); js_setglobal(J, "__cookie");
    js_pushnumber(J, pg->w); js_setglobal(J, "__w");
    js_pushnumber(J, pg->h); js_setglobal(J, "__h");
    if (js_try(J)) { bwdj_note(&r->d, "setting up the DOM failed: ", js_tostring(J, -1)); js_pop(J, 1); goto done; }
    bwdj_setup(J, &r->d);
    js_endtry(J);
    if (js_ploadstring(J, "[prelude]", kBwPrelude)) { bwdj_note(&r->d, "prelude: ", js_tostring(J, -1)); js_pop(J, 1); goto done; }
    js_pushundefined(J);
    if (js_pcall(J, 0)) { bwdj_note(&r->d, "prelude: ", js_tostring(J, -1)); js_pop(J, 1); goto done; }
    js_pop(J, 1);
    result = 0;

    /* the page as it is read */
    r->d.parsing = 1;
    run_scripts(r);
    r->d.parsing = 0;
    /* read: DOMContentLoaded (and the scripts it added), load */
    if (!out_of_time(r)) { call_global(r, "__loaded", 1, 0, 1, 0); run_scripts(r); }
    if (!out_of_time(r)) { call_global(r, "__loaded", 2, 0, 1, 0); run_scripts(r); }
    /* the first seconds' timers */
    while (!out_of_time(r) && pg->timers < BW_TIMER_CALLS) {
        double ran = 0;
        if (call_global(r, "__runTimers", BW_TIMER_MS, 20, 2, &ran) || ran < 1) break;
        pg->timers += (int)ran;
        run_scripts(r);
    }
    js_setlimit(J, 1000000, 0);         /* (our own last step: even after a script was stopped) */
    call_global(r, "__flushStyles", 0, 0, 0, 0);

done:
    if (r->d.navigate.n) pg->navigate = r->base ? script_url(r->base, r->d.navigate.p, r->d.navigate.n) : 0;
    if (pg->navigate && !r->d.tree->oom) {
        /* location.href = ...: as <meta http-equiv=refresh content="0; url=...">, which the browser
         * follows the way it follows the page's own */
        BwNode* head = bwd_child_element(bwd_child_element(r->d.tree->doc, "html"), "head");
        BwNode* m = head ? bwd_node(r->d.tree, BWN_ELEMENT, "meta", 4) : 0;
        BwBuf c;
        memset(&c, 0, sizeof c);
        bwb_str(&c, "0; url=");
        bwd_escape(pg->navigate, (unsigned int)strlen(pg->navigate), 1, &c);
        if (m && c.p && !bwd_set_attr(m, "http-equiv", "refresh") && !bwd_set_attr(m, "content", c.p)) {
            bwd_insert_before(head, m, head->first);
            r->d.dirty = 1;
        }
        free(c.p);
    }
    if (r->d.dirty && !r->d.tree->oom) {
        BwBuf out;
        memset(&out, 0, sizeof out);
        bwd_serialize(r->d.tree->doc, 0, &out);
        if (!out.oom) { pg->out = out.p; pg->outLen = out.n; } else free(out.p);
    }
    free(r->d.navigate.p);
    pg->say = r->d.say.p; pg->sayLen = r->d.say.n;
    pg->cookiesLen = r->d.cookies.n; pg->cookies = take(&r->d.cookies);
    pg->missingLen = r->missing.n; pg->missing = take(&r->missing);
    free(r->d.wr.p);
    free(r->base);
    bwd_free(r->d.tree);
    free(r);
    return result;
}
