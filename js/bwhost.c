/* Browser 9300: a page's inline scripts, run before the page is read (see bwhost.h).
 *
 * The HTML is scanned for <script> elements (outside comments, noscript, textarea, xmp, style,
 * template and title); each one that is JavaScript and has no src runs in order in one engine
 * with prelude.js's world, and the element is replaced by what it wrote. Text written with a
 * <script> in it goes back in front of the rest of the page and is scanned again, as a browser
 * reads document.write's output; other text is passed straight on. An error ends that script
 * only; running out of time or memory ends the scripts (the rest stay as they are, unrun).
 * C89, no static data; tested on a PC (test/pagetest.c). */

#include <string.h>
#include <stdlib.h>
#include "mujs.h"
#include "bwhost.h"
#include "prelude.h"

#define BW_MAX_SCRIPTS 500
#define BW_MAX_WRITTEN (2 * 1024 * 1024)
#define BW_MAX_SAY 8192
#define BW_MAX_SCRIPT_BYTES (64 * 1024)   /* bigger inline ones (frameworks, page data) are not run:
                                           * compiling one is not covered by the time limit */

typedef struct { char* p; unsigned int n, cap; int oom; } Gb;

static void gb_add(Gb* g, const char* s, unsigned int n)
{
    if (g->oom || !n) return;
    if (g->n + n + 1 > g->cap) {
        unsigned int cap = g->cap ? g->cap : 1024;
        char* q;
        while (cap < g->n + n + 1) cap *= 2;
        q = (char*)realloc(g->p, cap);
        if (!q) { g->oom = 1; return; }
        g->p = q; g->cap = cap;
    }
    memcpy(g->p + g->n, s, n);
    g->n += n;
    g->p[g->n] = 0;
}
static void gb_str(Gb* g, const char* s) { gb_add(g, s, (unsigned int)strlen(s)); }

typedef struct
    {
    Gb wr;          /* what the running script wrote */
    Gb say;
    unsigned int total;
    } Host;

/* ---- the host functions */

static void h_write(js_State* J)
{
    Host* h = (Host*)js_getcontext(J);
    const char* s = js_tostring(J, 1);
    unsigned int n = (unsigned int)strlen(s);
    if (h->total + n > BW_MAX_WRITTEN) js_error(J, "document.write: too much");
    h->total += n;
    gb_add(&h->wr, s, n);
    if (h->wr.oom) js_error(J, "out of memory");
    js_pushundefined(J);
}

static void h_say(js_State* J)
{
    Host* h = (Host*)js_getcontext(J);
    const char* s = js_tostring(J, 1);
    unsigned int n = (unsigned int)strlen(s);
    if (h->say.n + n + 2 < BW_MAX_SAY) {
        gb_add(&h->say, s, n > 300 ? 300 : n);
        gb_add(&h->say, "\n", 1);
    }
    js_pushundefined(J);
}

/* ---- the HTML scan */

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

/* the '>' that ends the tag starting at i (quotes respected), or n */
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

typedef struct
    {
    unsigned int start, contentStart, contentEnd, end;
    int hasSrc, isJs;
    } Script;

/* the value of attribute name (lower case) in the tag s[a..b): its start and length, or -1 */
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

static int is_js_type(const char* s, unsigned int vs, unsigned int vl)
{
    static const char* const ok[] = { "text/javascript", "application/javascript", "application/x-javascript",
        "text/ecmascript", "application/ecmascript", "text/jscript", "text/livescript", "text/x-javascript", 0 };
    unsigned int e = vs + vl, k;
    while (vs < e && (s[vs] == ' ' || s[vs] == '\t')) vs++;
    if (vs == e) return 1;
    {   /* up to a ';' (charset=...) */
        unsigned int t = vs;
        while (t < e && s[t] != ';' && s[t] != ' ') t++;
        e = t;
    }
    for (k = 0; ok[k]; k++) if (e - vs == strlen(ok[k]) && at_ci(s, e, vs, ok[k])) return 1;
    return 0;
}

/* elements whose content is not markup: skipped whole (scripts in them do not run) */
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

/* the next script element from i: 1 and *sc, or 0 */
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
            sc->contentStart = te < n ? te + 1 : n;
            ce = find_ci(s, n, sc->contentStart, "</script");
            sc->contentEnd = ce;
            sc->end = ce >= n ? n : tag_end(s, n, ce + 8) + 1;
            if (sc->end > n) sc->end = n;
            sc->hasSrc = attr(s, i + 7, te, "src", &vs, &vl) == 0;
            if (attr(s, i + 7, te, "type", &vs, &vl) == 0) sc->isJs = is_js_type(s, vs, vl);
            else if (attr(s, i + 7, te, "language", &vs, &vl) == 0)
                sc->isJs = vl == 0 || at_ci(s, vs + vl, vs, "javascript") || at_ci(s, vs + vl, vs, "jscript") ||
                           at_ci(s, vs + vl, vs, "ecmascript") || at_ci(s, vs + vl, vs, "livescript");
            else sc->isJs = 1;
            return 1;
        }
        k = skip_raw(s, n, i);
        i = k ? k : i + 1;
    }
    return 0;
}

/* "document.write" in s[a..b) */
static int has_write(const char* s, unsigned int a, unsigned int b)
{
    unsigned int i;
    for (i = a; i + 14 <= b; i++) if (s[i] == 'd' && memcmp(s + i, "document.write", 14) == 0) return 1;
    return 0;
}

int bw_js_has_scripts(const char* html, unsigned int len)
{
    /* Without a DOM, document.write is the only way a script changes the page: none of the
     * inline scripts calls it, none runs (modern pages: their time is spent for nothing) */
    Script sc;
    unsigned int i = 0;
    while (next_script(html, len, i, &sc)) {
        if (sc.isJs && !sc.hasSrc && sc.contentEnd - sc.contentStart <= BW_MAX_SCRIPT_BYTES &&
            has_write(html, sc.contentStart, sc.contentEnd)) return 1;
        i = sc.end;
    }
    return 0;
}

/* the source of s[a..b) zero-terminated, without an old "<!-- ... -->" wrapper */
static char* script_source(const char* s, unsigned int a, unsigned int b)
{
    char* r;
    unsigned int t = a;
    while (t < b && (s[t] == ' ' || s[t] == '\t' || s[t] == '\n' || s[t] == '\r')) t++;
    if (t + 4 <= b && s[t] == '<' && s[t + 1] == '!' && s[t + 2] == '-' && s[t + 3] == '-') {
        while (t < b && s[t] != '\n') t++;
        a = t;
    }
    t = b;
    while (t > a && (s[t - 1] == ' ' || s[t - 1] == '\t' || s[t - 1] == '\n' || s[t - 1] == '\r')) t--;
    if (t >= a + 3 && s[t - 1] == '>' && s[t - 2] == '-' && s[t - 3] == '-') {
        t -= 3;
        while (t > a && s[t - 1] != '\n') t--;
        b = t;
    }
    r = (char*)malloc(b - a + 1);
    if (!r) return 0;
    memcpy(r, s + a, b - a);
    r[b - a] = 0;
    return r;
}

static int contains_script(const char* s, unsigned int n) { return find_ci(s, n, 0, "<script") < n; }

/* "script ran too long" / "out of memory": the scripts stop */
static int fatal(const char* m)
{
    return strstr(m, "ran too long") != 0 || strstr(m, "out of memory") != 0 || strstr(m, "too much") != 0;
}

static void note(Host* h, const char* what, int k, const char* m)
{
    char num[16];
    int i = 0, v = k;
    char t[12];
    if (h->say.n > BW_MAX_SAY) return;
    do { t[i++] = (char)('0' + v % 10); v /= 10; } while (v && i < 11);
    { int j; for (j = 0; j < i; j++) num[j] = t[i - 1 - j]; num[i] = 0; }
    gb_str(&h->say, "script ");
    gb_str(&h->say, num);
    gb_str(&h->say, what);
    gb_add(&h->say, m, (unsigned int)(strlen(m) > 300 ? 300 : strlen(m)));
    gb_add(&h->say, "\n", 1);
}

int bw_js_page(js_State* J, BwJsPage* pg)
{
    Host h;
    Gb out;
    const char* in = pg->html;
    unsigned int n = pg->len, pos = 0;
    char* owned = 0;
    Script sc;
    memset(&h, 0, sizeof h);
    memset(&out, 0, sizeof out);
    pg->out = 0; pg->outLen = 0; pg->say = 0; pg->sayLen = 0;
    pg->run = pg->failed = pg->skipped = pg->external = pg->written = pg->stopped = pg->big = 0;

    js_setcontext(J, &h);
    js_newcfunction(J, h_write, "__write", 1); js_setglobal(J, "__write");
    js_newcfunction(J, h_say, "__say", 1); js_setglobal(J, "__say");
    js_pushstring(J, pg->href ? pg->href : ""); js_setglobal(J, "__href");
    js_pushstring(J, pg->ua ? pg->ua : ""); js_setglobal(J, "__ua");
    js_pushstring(J, pg->ref ? pg->ref : ""); js_setglobal(J, "__ref");
    js_pushstring(J, pg->lang ? pg->lang : "en"); js_setglobal(J, "__lang");
    js_pushnumber(J, pg->w); js_setglobal(J, "__w");
    js_pushnumber(J, pg->h); js_setglobal(J, "__h");
    if (js_ploadstring(J, "[prelude]", kBwPrelude)) { note(&h, " (prelude): ", 0, js_tostring(J, -1)); js_pop(J, 1); goto fail; }
    js_pushundefined(J);
    if (js_pcall(J, 0)) { note(&h, " (prelude): ", 0, js_tostring(J, -1)); js_pop(J, 1); goto fail; }
    js_pop(J, 1);

    while (next_script(in, n, pos, &sc)) {
        char* src;
        if (!pg->stopped && pg->expired && pg->expired(pg->ctx)) pg->stopped = 1;   /* compiling counts too */
        if (!sc.isJs || sc.hasSrc || pg->stopped || pg->run >= BW_MAX_SCRIPTS ||
            sc.contentEnd - sc.contentStart > BW_MAX_SCRIPT_BYTES) {
            if (sc.hasSrc && sc.isJs) pg->external++;
            else if (sc.isJs && sc.contentEnd - sc.contentStart > BW_MAX_SCRIPT_BYTES) pg->big++;
            else if (sc.isJs) pg->skipped++;
            gb_add(&out, in + pos, sc.end - pos);       /* as it was */
            pos = sc.end;
            continue;
        }
        gb_add(&out, in + pos, sc.start - pos);
        pos = sc.end;
        src = script_source(in, sc.contentStart, sc.contentEnd);
        if (!src) { pg->stopped = 1; continue; }
        pg->run++;
        h.wr.n = 0;
        if (h.wr.p) h.wr.p[0] = 0;
        if (js_ploadstring(J, "[script]", src)) {
            const char* m = js_tostring(J, -1);
            pg->failed++;
            note(&h, ": ", pg->run, m);
            if (fatal(m)) pg->stopped = 1;
            js_pop(J, 1);
        } else {
            js_pushundefined(J);
            if (js_pcall(J, 0)) {
                const char* m = js_tostring(J, -1);
                pg->failed++;
                note(&h, ": ", pg->run, m);
                if (fatal(m)) pg->stopped = 1;
            }
            js_pop(J, 1);
        }
        free(src);
        if (h.wr.n) {
            pg->written += h.wr.n;
            if (contains_script(h.wr.p, h.wr.n)) {
                /* read again: what it wrote, then the rest of the page */
                char* again = (char*)malloc(h.wr.n + (n - pos) + 1);
                if (!again) { pg->stopped = 1; gb_add(&out, h.wr.p, h.wr.n); continue; }
                memcpy(again, h.wr.p, h.wr.n);
                memcpy(again + h.wr.n, in + pos, n - pos);
                n = h.wr.n + (n - pos);
                again[n] = 0;
                free(owned);
                owned = again;
                in = again;
                pos = 0;
            } else gb_add(&out, h.wr.p, h.wr.n);
        }
        if (out.oom) { pg->stopped = 1; break; }
    }
    gb_add(&out, in + pos, n - pos);
    free(owned);
    free(h.wr.p);
    if (out.oom) { free(out.p); out.p = 0; out.n = 0; pg->stopped = 1; }
    if (pg->written && out.p) { pg->out = out.p; pg->outLen = out.n; }
    else free(out.p);
    pg->say = h.say.p; pg->sayLen = h.say.n;
    return 0;
fail:
    free(h.wr.p);
    pg->say = h.say.p; pg->sayLen = h.say.n;
    return -1;
}
