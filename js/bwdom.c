/* Browser 9300: a page as a tree (see bwdom.h). C89, no static data. */

#include <string.h>
#include <stdlib.h>
#include "bwdom.h"

#define BW_MAX_DEPTH 256

/* ------------------------------------------------------------------ small things */

static int lc(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int is_ws(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

void bwb_add(BwBuf* b, const char* s, unsigned int n)
{
    if (b->oom || !n) return;
    if (b->n + n + 1 > b->cap) {
        unsigned int cap = b->cap ? b->cap : 256;
        char* q;
        while (cap < b->n + n + 1) cap *= 2;
        q = (char*)realloc(b->p, cap);
        if (!q) { b->oom = 1; return; }
        b->p = q; b->cap = cap;
    }
    memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = 0;
}
void bwb_str(BwBuf* b, const char* s) { bwb_add(b, s, (unsigned int)strlen(s)); }
static void bwb_ch(BwBuf* b, char c) { bwb_add(b, &c, 1); }

static char* dupn(const char* s, unsigned int n)
{
    char* r = (char*)malloc(n + 1);
    if (!r) return 0;
    memcpy(r, s, n);
    r[n] = 0;
    return r;
}

static int eq(const char* a, const char* b) { return a && b && strcmp(a, b) == 0; }

/* lit (lower case) == s[0..n) ignoring case */
static int eq_ci_n(const char* s, unsigned int n, const char* lit)
{
    unsigned int k;
    for (k = 0; k < n; k++) if (!lit[k] || lc((unsigned char)s[k]) != lit[k]) return 0;
    return lit[n] == 0;
}

static int in_list(const char* name, const char* const* list)
{
    int k;
    if (!name) return 0;
    for (k = 0; list[k]; k++) if (strcmp(name, list[k]) == 0) return 1;
    return 0;
}

/* ------------------------------------------------------------------ nodes */

BwTree* bwd_new(unsigned int limit)
{
    BwTree* t = (BwTree*)malloc(sizeof(BwTree));
    if (!t) return 0;
    memset(t, 0, sizeof(BwTree));
    t->limit = limit;
    t->doc = bwd_node(t, BWN_DOCUMENT, "#document", 9);
    if (!t->doc) { free(t); return 0; }
    return t;
}

void bwd_free(BwTree* t)
{
    BwNode* n;
    if (!t) return;
    n = t->allNodes;
    while (n) {
        BwNode* nx = n->all;
        BwAttr* a = n->attrs;
        while (a) { BwAttr* an = a->next; free(a->name); free(a->value); free(a); a = an; }
        free(n->name);
        free(n->text);
        free(n);
        n = nx;
    }
    free(t);
}

BwNode* bwd_node(BwTree* t, int type, const char* name, unsigned int nameLen)
{
    BwNode* n;
    unsigned int k;
    if (t->oom || (t->limit && t->count >= t->limit)) { t->oom = 1; return 0; }
    n = (BwNode*)malloc(sizeof(BwNode));
    if (!n) { t->oom = 1; return 0; }
    memset(n, 0, sizeof(BwNode));
    n->type = type;
    n->serial = ++t->serials;
    if (name) {
        n->name = dupn(name, nameLen);
        if (!n->name) { free(n); t->oom = 1; return 0; }
        if (type == BWN_ELEMENT) for (k = 0; k < nameLen; k++) n->name[k] = (char)lc((unsigned char)n->name[k]);
    }
    n->all = t->allNodes;
    t->allNodes = n;
    t->count++;
    if (type == BWN_ELEMENT) {
        static const char* const voids[] = { "area", "base", "br", "col", "embed", "hr", "img", "input", "keygen", "link",
            "meta", "param", "source", "track", "wbr", 0 };
        static const char* const raws[] = { "script", "style", "textarea", "title", "xmp", "noscript", "noembed",
            "noframes", "iframe", "plaintext", "template", 0 };
        if (in_list(n->name, voids)) n->flags |= BWF_VOID;
        if (in_list(n->name, raws)) n->flags |= BWF_RAW;
    }
    return n;
}

int bwd_set_text(BwNode* n, const char* s, unsigned int len)
{
    char* t = dupn(s, len);
    if (!t) return -1;
    free(n->text);
    n->text = t;
    return 0;
}

const char* bwd_attr(const BwNode* n, const char* name)
{
    const BwAttr* a;
    if (!n || n->type != BWN_ELEMENT) return 0;
    for (a = n->attrs; a; a = a->next) if (strcmp(a->name, name) == 0) return a->value ? a->value : "";
    return 0;
}

int bwd_set_attr(BwNode* n, const char* name, const char* rawValue)
{
    BwAttr* a;
    BwAttr** tail = &n->attrs;
    char* v;
    for (a = n->attrs; a; a = a->next) {
        if (strcmp(a->name, name) == 0) {
            v = rawValue ? dupn(rawValue, (unsigned int)strlen(rawValue)) : 0;
            if (rawValue && !v) return -1;
            free(a->value);
            a->value = v;
            return 0;
        }
        tail = &a->next;
    }
    a = (BwAttr*)malloc(sizeof(BwAttr));
    if (!a) return -1;
    a->name = dupn(name, (unsigned int)strlen(name));
    a->value = rawValue ? dupn(rawValue, (unsigned int)strlen(rawValue)) : 0;
    a->next = 0;
    if (!a->name || (rawValue && !a->value)) { free(a->name); free(a->value); free(a); return -1; }
    {   unsigned int k; for (k = 0; a->name[k]; k++) a->name[k] = (char)lc((unsigned char)a->name[k]); }
    *tail = a;
    return 0;
}

void bwd_remove_attr(BwNode* n, const char* name)
{
    BwAttr** p = &n->attrs;
    while (*p) {
        if (strcmp((*p)->name, name) == 0) {
            BwAttr* a = *p;
            *p = a->next;
            free(a->name); free(a->value); free(a);
            return;
        }
        p = &(*p)->next;
    }
}

/* ------------------------------------------------------------------ tree edits */

void bwd_remove(BwNode* n)
{
    BwNode* p = n->parent;
    if (!p) return;
    if (n->prev) n->prev->next = n->next; else p->first = n->next;
    if (n->next) n->next->prev = n->prev; else p->last = n->prev;
    n->parent = n->prev = n->next = 0;
}

int bwd_insert_before(BwNode* parent, BwNode* child, BwNode* ref)
{
    BwNode* a;
    if (!parent || !child || child->type == BWN_DOCUMENT) return -1;
    if (parent->type != BWN_ELEMENT && parent->type != BWN_DOCUMENT && parent->type != BWN_FRAGMENT) return -1;
    for (a = parent; a; a = a->parent) if (a == child) return -1;          /* a cycle */
    if (ref && ref->parent != parent) return -1;
    if (ref == child) return 0;
    if (child->type == BWN_FRAGMENT) {
        /* its children go, in order */
        while (child->first) {
            BwNode* c = child->first;
            bwd_remove(c);
            if (bwd_insert_before(parent, c, ref)) return -1;
        }
        return 0;
    }
    bwd_remove(child);
    child->parent = parent;
    child->next = ref;
    child->prev = ref ? ref->prev : parent->last;
    if (child->prev) child->prev->next = child; else parent->first = child;
    if (ref) ref->prev = child; else parent->last = child;
    return 0;
}

static void append(BwNode* parent, BwNode* child) { bwd_insert_before(parent, child, 0); }

BwNode* bwd_clone(BwTree* t, BwNode* n, int deep)
{
    BwNode* c = bwd_node(t, n->type, n->name, n->name ? (unsigned int)strlen(n->name) : 0);
    BwAttr* a;
    if (!c) return 0;
    c->flags = n->flags & (BWF_RAW | BWF_VOID | BWF_RAN);
    if (n->text && bwd_set_text(c, n->text, (unsigned int)strlen(n->text))) return 0;
    for (a = n->attrs; a; a = a->next) if (bwd_set_attr(c, a->name, a->value)) return 0;
    if (deep) {
        BwNode* k;
        for (k = n->first; k; k = k->next) {
            BwNode* kc = bwd_clone(t, k, 1);
            if (!kc) return 0;
            append(c, kc);
        }
    }
    return c;
}

/* ------------------------------------------------------------------ the HTML parser */

typedef struct
    {
    BwTree* t;
    BwNode* root;               /* the document or a fragment */
    BwNode* stack[BW_MAX_DEPTH];
    int sp;
    int fragment;
    int noScripts;
    BwNode* html;
    BwNode* head;
    BwNode* body;
    } Ps;

#define TOP(p) ((p)->stack[(p)->sp - 1])

static void push(Ps* p, BwNode* n) { if (p->sp < BW_MAX_DEPTH) p->stack[p->sp++] = n; }

/* pops up to and including the open element named name (searching no further than a boundary);
 * 0 if there was none */
static int pop_to(Ps* p, const char* name, const char* const* boundaries)
{
    int i;
    for (i = p->sp - 1; i > 0; i--) {
        const char* nm = p->stack[i]->name;
        if (eq(nm, name)) { p->sp = i; return 1; }
        if (boundaries && in_list(nm, boundaries)) return 0;
    }
    return 0;
}

static void add_text(Ps* p, const char* s, unsigned int n)
{
    BwNode* top = TOP(p);
    BwNode* last = top->last;
    if (!n) return;
    if (last && last->type == BWN_TEXT) {
        /* next to text already there: one node */
        unsigned int old = (unsigned int)strlen(last->text);
        char* q = (char*)realloc(last->text, old + n + 1);
        if (!q) { p->t->oom = 1; return; }
        memcpy(q + old, s, n);
        q[old + n] = 0;
        last->text = q;
        return;
    }
    {
        BwNode* t = bwd_node(p->t, BWN_TEXT, 0, 0);
        if (!t) return;
        if (bwd_set_text(t, s, n)) { p->t->oom = 1; return; }
        append(top, t);
    }
}

static const char* const kHeadOnly[] = { "base", "basefont", "bgsound", "link", "meta", "title", "style", "script",
    "noscript", "template", 0 };

/* html, head and body for content arriving where it would not go */
static void ensure_html(Ps* p)
{
    if (p->fragment || p->html) return;
    p->html = bwd_node(p->t, BWN_ELEMENT, "html", 4);
    if (!p->html) return;
    append(p->root, p->html);
    p->sp = 1;
    push(p, p->html);
}
static void ensure_head(Ps* p)
{
    ensure_html(p);
    if (p->fragment || p->head || !p->html) return;
    p->head = bwd_node(p->t, BWN_ELEMENT, "head", 4);
    if (p->head) bwd_insert_before(p->html, p->head, p->html->first);
}
static void ensure_body(Ps* p)
{
    ensure_head(p);
    if (p->fragment || p->body || !p->html) return;
    p->body = bwd_node(p->t, BWN_ELEMENT, "body", 4);
    if (!p->body) return;
    append(p->html, p->body);
    p->sp = 2;                  /* doc, html */
    push(p, p->body);
}

/* where content goes: a head element in the head while it is open, anything else in the body */
static void context_for(Ps* p, const char* name, int isText)
{
    BwNode* top;
    if (p->fragment) return;
    top = TOP(p);
    if (top == p->root) ensure_html(p);
    top = TOP(p);
    if (top == p->html && !p->body) {
        if (!isText && in_list(name, kHeadOnly) && !p->head) { ensure_head(p); push(p, p->head); return; }
        if (!isText && in_list(name, kHeadOnly)) {
            /* after </head>, before <body>: in the head still */
            push(p, p->head);
            return;
        }
        ensure_body(p);
        return;
    }
    if (top == p->head && (isText || !in_list(name, kHeadOnly))) {
        p->sp--;                /* the head ends */
        ensure_body(p);
    }
}

static const char* const kCloseP[] = { "address", "article", "aside", "blockquote", "center", "details", "dialog", "dir",
    "div", "dl", "fieldset", "figcaption", "figure", "footer", "form", "h1", "h2", "h3", "h4", "h5", "h6", "header",
    "hgroup", "hr", "li", "main", "menu", "nav", "ol", "p", "pre", "section", "table", "ul", "dd", "dt", "summary",
    "listing", "plaintext", "xmp", 0 };
static const char* const kScopeP[] = { "td", "th", "table", "caption", "button", "object", "marquee", "applet", "html",
    "body", "template", 0 };
static const char* const kScopeList[] = { "ul", "ol", "td", "th", "table", "html", "body", "template", 0 };
static const char* const kScopeDl[] = { "dl", "td", "th", "table", "html", "body", "template", 0 };
static const char* const kScopeRow[] = { "table", "tbody", "thead", "tfoot", "html", "body", "template", 0 };
static const char* const kScopeCell[] = { "tr", "table", "html", "body", "template", 0 };
static const char* const kScopeTable[] = { "table", "html", "body", "template", 0 };
static const char* const kScopeEnd[] = { "td", "th", "table", "html", "body", "template", "caption", 0 };
static const char* const kHeadings[] = { "h1", "h2", "h3", "h4", "h5", "h6", 0 };

/* the end tags a start tag implies */
static void implied_close(Ps* p, const char* name)
{
    if (in_list(name, kCloseP)) pop_to(p, "p", kScopeP);
    if (eq(name, "li")) pop_to(p, "li", kScopeList);
    else if (eq(name, "dd") || eq(name, "dt")) { if (!pop_to(p, "dd", kScopeDl)) pop_to(p, "dt", kScopeDl); }
    else if (eq(name, "option")) { if (eq(TOP(p)->name, "option")) p->sp--; }
    else if (eq(name, "optgroup")) { if (eq(TOP(p)->name, "option")) p->sp--; if (eq(TOP(p)->name, "optgroup")) p->sp--; }
    else if (eq(name, "tr")) { pop_to(p, "td", kScopeCell); pop_to(p, "th", kScopeCell); pop_to(p, "tr", kScopeRow); }
    else if (eq(name, "td") || eq(name, "th")) { if (!pop_to(p, "td", kScopeCell)) pop_to(p, "th", kScopeCell); }
    else if (eq(name, "tbody") || eq(name, "thead") || eq(name, "tfoot")) {
        if (!pop_to(p, "tbody", kScopeTable) && !pop_to(p, "thead", kScopeTable)) pop_to(p, "tfoot", kScopeTable);
    }
    else if (in_list(name, kHeadings) && in_list(TOP(p)->name, kHeadings)) p->sp--;
    else if (eq(name, "a")) pop_to(p, "a", kScopeP);
    else if (eq(name, "button")) pop_to(p, "button", kScopeP);
}

static int tag_char(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == ':' || c == '_' || c == '.'; }

/* attributes of the tag at s[i..), up to its '>'; returns the index after it; *selfClose */
static unsigned int read_attrs(Ps* p, BwNode* el, const char* s, unsigned int n, unsigned int i, int* selfClose)
{
    *selfClose = 0;
    while (i < n) {
        unsigned int ns, ne, vs = 0, vl = 0;
        int hasVal = 0;
        while (i < n && is_ws((unsigned char)s[i])) i++;
        if (i >= n) break;
        if (s[i] == '>') return i + 1;
        if (s[i] == '/') { if (i + 1 < n && s[i + 1] == '>') { *selfClose = 1; return i + 2; } i++; continue; }
        ns = i;
        while (i < n && !is_ws((unsigned char)s[i]) && s[i] != '=' && s[i] != '>' && !(s[i] == '/' && i + 1 < n && s[i + 1] == '>')) i++;
        ne = i;
        while (i < n && is_ws((unsigned char)s[i])) i++;
        if (i < n && s[i] == '=') {
            hasVal = 1;
            i++;
            while (i < n && is_ws((unsigned char)s[i])) i++;
            if (i < n && (s[i] == '"' || s[i] == '\'')) {
                char q = s[i++];
                vs = i;
                while (i < n && s[i] != q) i++;
                vl = i - vs;
                if (i < n) i++;
            } else {
                vs = i;
                while (i < n && !is_ws((unsigned char)s[i]) && s[i] != '>') i++;
                vl = i - vs;
            }
        }
        if (el && ne > ns) {
            char* nm = dupn(s + ns, ne - ns);
            char* v = hasVal ? dupn(s + vs, vl) : 0;
            if (!nm || (hasVal && !v)) p->t->oom = 1;
            else {
                unsigned int k;
                BwBuf esc;
                for (k = 0; nm[k]; k++) nm[k] = (char)lc((unsigned char)nm[k]);
                /* a value with a '"' (it was in single quotes) keeps it as &quot;: values are kept raw */
                memset(&esc, 0, sizeof esc);
                if (v && strchr(v, '"')) {
                    for (k = 0; v[k]; k++) { if (v[k] == '"') bwb_str(&esc, "&quot;"); else bwb_ch(&esc, v[k]); }
                    free(v);
                    v = esc.p ? esc.p : dupn("", 0);
                }
                if (!bwd_attr(el, nm)) { if (bwd_set_attr(el, nm, hasVal ? (v ? v : "") : 0)) p->t->oom = 1; }
            }
            free(nm);
            free(v);
        }
    }
    return n;
}

/* the index of "</name" (any case) from i, or n */
static unsigned int find_end_tag(const char* s, unsigned int n, unsigned int i, const char* name)
{
    unsigned int l = (unsigned int)strlen(name);
    for (; i + 2 + l <= n; i++) {
        if (s[i] == '<' && s[i + 1] == '/' && eq_ci_n(s + i + 2, l, name) && (i + 2 + l == n || !tag_char((unsigned char)s[i + 2 + l])))
            return i;
    }
    return n;
}

static void start_tag(Ps* p, const char* s, unsigned int n, unsigned int* pi)
{
    unsigned int i = *pi + 1, ns = i, ne;
    int selfClose;
    BwNode* el;
    char name[32];
    while (i < n && tag_char((unsigned char)s[i])) i++;
    ne = i;
    if (ne - ns >= sizeof name) { /* not a tag of ours: kept as it was, attributes dropped */
        ne = ns + sizeof name - 1;
    }
    {   unsigned int k; for (k = 0; k < ne - ns; k++) name[k] = (char)lc((unsigned char)s[ns + k]); name[ne - ns] = 0; }

    if (!p->fragment && (eq(name, "html") || eq(name, "body") || eq(name, "head"))) {
        BwNode* target = 0;
        if (eq(name, "html")) { if (!p->html) { ensure_html(p); } target = p->html; }
        else if (eq(name, "head")) { if (!p->head && !p->body) { ensure_head(p); if (p->head) push(p, p->head); } target = p->head; }
        else {
            if (!p->body) {
                if (TOP(p) == p->head) p->sp--;
                ensure_body(p);
            }
            target = p->body;
        }
        *pi = read_attrs(p, target, s, n, i, &selfClose);   /* (attributes not already there) */
        return;
    }
    context_for(p, name, 0);
    implied_close(p, name);
    el = bwd_node(p->t, BWN_ELEMENT, name, (unsigned int)strlen(name));
    i = read_attrs(p, el, s, n, i, &selfClose);
    *pi = i;
    if (!el) return;
    append(TOP(p), el);
    if (el->flags & BWF_VOID) return;
    if (el->flags & BWF_RAW) {
        unsigned int e = find_end_tag(s, n, i, name);
        if (e > i) {
            BwNode* t = bwd_node(p->t, BWN_TEXT, 0, 0);
            if (t) { if (bwd_set_text(t, s + i, e - i)) p->t->oom = 1; else append(el, t); }
        }
        if (p->noScripts && eq(name, "script")) el->flags |= BWF_RAN;
        if (e < n) {
            while (e < n && s[e] != '>') e++;
            e++;
        }
        *pi = e > n ? n : e;
        return;
    }
    if (selfClose) {
        /* <div/> is <div> in HTML, but in svg / math (or a name with ':') it ends the element */
        int k;
        for (k = p->sp - 1; k > 0; k--) if (eq(p->stack[k]->name, "svg") || eq(p->stack[k]->name, "math")) return;
        if (eq(name, "svg") || eq(name, "math") || strchr(name, ':')) return;
    }
    push(p, el);
}

static void end_tag(Ps* p, const char* s, unsigned int n, unsigned int* pi)
{
    unsigned int i = *pi + 2, ns = i;
    char name[32];
    while (i < n && tag_char((unsigned char)s[i])) i++;
    {   unsigned int k, l = i - ns; if (l >= sizeof name) l = sizeof name - 1; for (k = 0; k < l; k++) name[k] = (char)lc((unsigned char)s[ns + k]); name[l] = 0; }
    while (i < n && s[i] != '>') i++;
    *pi = i < n ? i + 1 : n;
    if (!name[0]) return;
    if (!p->fragment) {
        if (eq(name, "head")) { if (TOP(p) == p->head) p->sp--; return; }
        if (eq(name, "body") || eq(name, "html")) return;
    }
    if (eq(name, "br")) {           /* </br> is <br> */
        BwNode* br;
        context_for(p, "br", 0);
        br = bwd_node(p->t, BWN_ELEMENT, "br", 2);
        if (br) append(TOP(p), br);
        return;
    }
    if (eq(name, "td") || eq(name, "th") || eq(name, "tr") || eq(name, "table") || eq(name, "tbody") || eq(name, "thead") ||
        eq(name, "tfoot") || eq(name, "caption"))
        pop_to(p, name, kScopeTable);
    else pop_to(p, name, kScopeEnd);
}

static void parse(Ps* p, const char* s, unsigned int n)
{
    unsigned int i = 0, ts;
    while (i < n && !p->t->oom) {
        if (s[i] == '<' && i + 1 < n) {
            char c = s[i + 1];
            if (c == '!' && i + 3 < n && s[i + 2] == '-' && s[i + 3] == '-') {
                unsigned int e = i + 4, b = e;
                BwNode* cm;
                while (e + 2 < n && !(s[e] == '-' && s[e + 1] == '-' && s[e + 2] == '>')) e++;
                if (e + 2 >= n) e = n;
                cm = bwd_node(p->t, BWN_COMMENT, 0, 0);
                if (cm) { if (bwd_set_text(cm, s + b, (e > b ? e : b) - b)) p->t->oom = 1; else append(TOP(p), cm); }
                i = e >= n ? n : e + 3;
                continue;
            }
            if (c == '!' || c == '?') {
                /* <!DOCTYPE ...> and other declarations: kept as they were */
                unsigned int e = i;
                BwNode* d;
                while (e < n && s[e] != '>') e++;
                if (e < n) e++;
                d = bwd_node(p->t, BWN_DOCTYPE, 0, 0);
                if (d) { if (bwd_set_text(d, s + i, e - i)) p->t->oom = 1; else append(TOP(p), d); }
                i = e;
                continue;
            }
            if (c == '/' && i + 2 < n && ((s[i + 2] >= 'a' && s[i + 2] <= 'z') || (s[i + 2] >= 'A' && s[i + 2] <= 'Z'))) { end_tag(p, s, n, &i); continue; }
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) { start_tag(p, s, n, &i); continue; }
        }
        /* text up to the next '<' that could start markup */
        ts = i;
        i++;
        while (i < n && s[i] != '<') i++;
        {
            unsigned int k;
            int ws = 1;
            for (k = ts; k < i; k++) if (!is_ws((unsigned char)s[k])) { ws = 0; break; }
            if (ws && !p->fragment && (TOP(p) == p->root || TOP(p) == p->html)) continue;   /* (between the parts) */
            if (!ws) context_for(p, 0, 1);
            add_text(p, s + ts, i - ts);
        }
    }
}

void bwd_parse_document(BwTree* t, const char* html, unsigned int len)
{
    Ps* p = (Ps*)malloc(sizeof(Ps));
    if (!p) { t->oom = 1; return; }
    memset(p, 0, sizeof(Ps));
    p->t = t;
    p->root = t->doc;
    push(p, t->doc);
    parse(p, html, len);
    ensure_body(p);                 /* (an empty page has them too) */
    free(p);
}

BwNode* bwd_parse_fragment(BwTree* t, const char* html, unsigned int len, int noScripts)
{
    Ps* p = (Ps*)malloc(sizeof(Ps));
    BwNode* f;
    if (!p) { t->oom = 1; return 0; }
    f = bwd_node(t, BWN_FRAGMENT, "#document-fragment", 18);
    if (!f) { free(p); return 0; }
    memset(p, 0, sizeof(Ps));
    p->t = t;
    p->root = f;
    p->fragment = 1;
    p->noScripts = noScripts;
    push(p, f);
    parse(p, html, len);
    free(p);
    return f;
}

/* ------------------------------------------------------------------ writing out */

void bwd_escape(const char* s, unsigned int len, int attr, BwBuf* out)
{
    unsigned int i, from = 0;
    for (i = 0; i < len; i++) {
        const char* r = 0;
        if (s[i] == '&') r = "&amp;";
        else if (s[i] == '<') r = "&lt;";
        else if (s[i] == '>') r = "&gt;";
        else if (attr && s[i] == '"') r = "&quot;";
        if (r) { bwb_add(out, s + from, i - from); bwb_str(out, r); from = i + 1; }
    }
    bwb_add(out, s + from, len - from);
}

static void put_utf8(BwBuf* out, unsigned long c)
{
    char b[4];
    if (c < 0x80) { b[0] = (char)c; bwb_add(out, b, 1); }
    else if (c < 0x800) { b[0] = (char)(0xC0 | (c >> 6)); b[1] = (char)(0x80 | (c & 0x3F)); bwb_add(out, b, 2); }
    else if (c < 0x10000) { b[0] = (char)(0xE0 | (c >> 12)); b[1] = (char)(0x80 | ((c >> 6) & 0x3F)); b[2] = (char)(0x80 | (c & 0x3F)); bwb_add(out, b, 3); }
    else { b[0] = (char)(0xF0 | (c >> 18)); b[1] = (char)(0x80 | ((c >> 12) & 0x3F)); b[2] = (char)(0x80 | ((c >> 6) & 0x3F)); b[3] = (char)(0x80 | (c & 0x3F)); bwb_add(out, b, 4); }
}

void bwd_unescape(const char* s, unsigned int len, BwBuf* out)
{
    static const char* const names[] = { "amp", "lt", "gt", "quot", "apos", "nbsp", "copy", "reg", "hellip", "mdash",
        "ndash", "laquo", "raquo", "euro", "times", "shy", "middot", "bull", "deg", "trade", 0 };
    static const unsigned short codes[] = { '&', '<', '>', '"', '\'', 0xA0, 0xA9, 0xAE, 0x2026, 0x2014, 0x2013, 0xAB,
        0xBB, 0x20AC, 0xD7, 0xAD, 0xB7, 0x2022, 0xB0, 0x2122 };
    unsigned int i, from = 0;
    for (i = 0; i < len; i++) {
        unsigned int e;
        if (s[i] != '&') continue;
        e = i + 1;
        while (e < len && e < i + 12 && s[e] != ';' && s[e] != '&' && !is_ws((unsigned char)s[e])) e++;
        if (e >= len || s[e] != ';') continue;
        if (s[i + 1] == '#') {
            unsigned long c = 0;
            unsigned int k = i + 2;
            int hex = k < e && (s[k] == 'x' || s[k] == 'X');
            if (hex) k++;
            if (k == e) continue;
            for (; k < e; k++) {
                int d = s[k] >= '0' && s[k] <= '9' ? s[k] - '0' : hex && lc((unsigned char)s[k]) >= 'a' && lc((unsigned char)s[k]) <= 'f' ? lc((unsigned char)s[k]) - 'a' + 10 : -1;
                if (d < 0) break;
                c = c * (hex ? 16 : 10) + (unsigned long)d;
                if (c > 0x10FFFF) break;
            }
            if (k != e || c == 0 || c > 0x10FFFF) continue;
            bwb_add(out, s + from, i - from);
            put_utf8(out, c);
            from = i = e;
            from++;
            continue;
        }
        {
            int k;
            for (k = 0; names[k]; k++) if (e - i - 1 == strlen(names[k]) && memcmp(s + i + 1, names[k], e - i - 1) == 0) break;
            if (!names[k]) continue;
            bwb_add(out, s + from, i - from);
            put_utf8(out, codes[k]);
            i = e;
            from = e + 1;
        }
    }
    bwb_add(out, s + from, len - from);
}

void bwd_text(BwNode* n, BwBuf* out)
{
    BwNode* k;
    if (n->type == BWN_TEXT) {
        if (n->parent && (n->parent->flags & BWF_RAW)) bwb_str(out, n->text);      /* raw text: as it is */
        else bwd_unescape(n->text, (unsigned int)strlen(n->text), out);
        return;
    }
    if (n->type == BWN_COMMENT || n->type == BWN_DOCTYPE) return;
    for (k = n->first; k; k = k->next) bwd_text(k, out);
}

static void ser(BwNode* n, BwBuf* out)
{
    BwNode* k;
    BwAttr* a;
    switch (n->type) {
    case BWN_TEXT: bwb_str(out, n->text); return;
    case BWN_COMMENT: bwb_str(out, "<!--"); bwb_str(out, n->text); bwb_str(out, "-->"); return;
    case BWN_DOCTYPE: bwb_str(out, n->text); return;
    case BWN_ELEMENT:
        bwb_ch(out, '<');
        bwb_str(out, n->name);
        for (a = n->attrs; a; a = a->next) {
            bwb_ch(out, ' ');
            bwb_str(out, a->name);
            if (a->value) { bwb_str(out, "=\""); bwb_str(out, a->value); bwb_ch(out, '"'); }
        }
        bwb_ch(out, '>');
        if (n->flags & BWF_VOID) return;
        for (k = n->first; k; k = k->next) ser(k, out);
        bwb_str(out, "</");
        bwb_str(out, n->name);
        bwb_ch(out, '>');
        return;
    default:
        for (k = n->first; k; k = k->next) ser(k, out);
    }
}

void bwd_serialize(BwNode* n, int outer, BwBuf* out)
{
    BwNode* k;
    if (outer) { ser(n, out); return; }
    for (k = n->first; k; k = k->next) ser(k, out);
}

/* ------------------------------------------------------------------ finding */

BwNode* bwd_next(const BwNode* root, const BwNode* n, int intoChildren)
{
    if (intoChildren && n->first) return n->first;
    while (n && n != root) {
        if (n->next) return n->next;
        n = n->parent;
    }
    return 0;
}

BwNode* bwd_child_element(BwNode* parent, const char* name)
{
    BwNode* k;
    if (!parent) return 0;
    for (k = parent->first; k; k = k->next) if (k->type == BWN_ELEMENT && eq(k->name, name)) return k;
    return 0;
}

BwNode* bwd_find_id(BwNode* root, const char* id)
{
    BwNode* n = root;
    while ((n = bwd_next(root, n, 1)) != 0) {
        if (n->type == BWN_ELEMENT) { const char* v = bwd_attr(n, "id"); if (v && strcmp(v, id) == 0) return n; }
    }
    return 0;
}

int bwd_has_class(const BwNode* n, const char* cls, unsigned int len)
{
    const char* v = bwd_attr(n, "class");
    if (!v || !len) return 0;
    while (*v) {
        const char* s;
        while (*v && is_ws((unsigned char)*v)) v++;
        s = v;
        while (*v && !is_ws((unsigned char)*v)) v++;
        if ((unsigned int)(v - s) == len && memcmp(s, cls, len) == 0) return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ selectors
 * Compound selectors of: tag or *, #id, .class, [attr], [attr=v] (also ^= $= *= ~= |=), and
 * :first-child :last-child :only-child :empty :checked :disabled :enabled :root :link :not(...),
 * joined by ' ' '>' '+' '~', in a list with ','. Anything else: the selector cannot be read. */

#define SEL_PARTS 16
#define SEL_COMPOUNDS 12
#define SEL_LIST 12

typedef struct { char kind; char op; const char* s; unsigned int n; const char* v; unsigned int vn; } SPart;
typedef struct { SPart part[SEL_PARTS]; int np; char comb; } SCompound;
typedef struct { SCompound c[SEL_COMPOUNDS]; int nc; } SComplex;
typedef struct { SComplex x[SEL_LIST]; int nx; } SList;

static int ident_char(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c >= 0x80 || c == '\\'; }

/* the pseudo-classes read (with their argument when they need one) */
static int pseudo_known(const SPart* P)
{
    static const char* const plain[] = {
        "first-child", "last-child", "only-child", "first-of-type", "last-of-type", "only-of-type", "empty",
        "checked", "disabled", "enabled", "root", "link", "any-link", "scope", "defined", "required", "optional",
        "read-only", "read-write", "placeholder-shown", "indeterminate", "default", "valid", "invalid",
        "hover", "focus", "active", "visited", "focus-within", "focus-visible", "target", "fullscreen", "modal",
        "autofill", "playing", "paused", "user-invalid", "user-valid", "in-range", "out-of-range" };
    static const char* const args[] = { "not", "is", "where", "has", "matches", "nth-child", "nth-last-child",
        "nth-of-type", "nth-last-of-type", "lang", "dir" };
    unsigned int k;
    for (k = 0; k < sizeof plain / sizeof plain[0]; k++) if (!P->v && eq_ci_n(P->s, P->n, plain[k])) return 1;
    for (k = 0; k < sizeof args / sizeof args[0]; k++) if (P->v && eq_ci_n(P->s, P->n, args[k])) return 1;
    return 0;
}

static int sel_parse(const char* s, SList* L)
{
    unsigned int i = 0, n = (unsigned int)strlen(s);
    SComplex* X;
    SCompound* C;
    L->nx = 0;
    for (;;) {
        if (L->nx >= SEL_LIST) return -1;
        X = &L->x[L->nx++];
        X->nc = 0;
        while (i < n && is_ws((unsigned char)s[i])) i++;
        for (;;) {
            char comb = 0;
            /* the combinator before this compound */
            if (X->nc) {
                int sawWs = 0;
                while (i < n && is_ws((unsigned char)s[i])) { i++; sawWs = 1; }
                if (i >= n || s[i] == ',') break;
                if (s[i] == '>' || s[i] == '+' || s[i] == '~') { comb = s[i++]; while (i < n && is_ws((unsigned char)s[i])) i++; }
                else if (sawWs) comb = ' ';
                else return -1;
            }
            if (X->nc >= SEL_COMPOUNDS) return -1;
            C = &X->c[X->nc++];
            C->np = 0;
            C->comb = comb;
            while (i < n && !is_ws((unsigned char)s[i]) && s[i] != ',' && s[i] != '>' && s[i] != '+' && s[i] != '~') {
                SPart* P;
                if (C->np >= SEL_PARTS) return -1;
                P = &C->part[C->np++];
                P->op = 0; P->v = 0; P->vn = 0;
                if (s[i] == '*') { P->kind = '*'; P->s = s + i; P->n = 1; i++; continue; }
                if (s[i] == '#' || s[i] == '.') {
                    P->kind = s[i] == '#' ? 'i' : 'c';
                    i++;
                    P->s = s + i;
                    while (i < n && ident_char((unsigned char)s[i])) i++;
                    P->n = (unsigned int)(s + i - P->s);
                    if (!P->n) return -1;
                    continue;
                }
                if (s[i] == '[') {
                    P->kind = 'a';
                    i++;
                    while (i < n && is_ws((unsigned char)s[i])) i++;
                    P->s = s + i;
                    while (i < n && ident_char((unsigned char)s[i])) i++;
                    P->n = (unsigned int)(s + i - P->s);
                    while (i < n && is_ws((unsigned char)s[i])) i++;
                    if (i < n && s[i] != ']') {
                        if (s[i] == '=') { P->op = '='; i++; }
                        else if (i + 1 < n && s[i + 1] == '=' && (s[i] == '^' || s[i] == '$' || s[i] == '*' || s[i] == '~' || s[i] == '|')) { P->op = s[i]; i += 2; }
                        else return -1;
                        while (i < n && is_ws((unsigned char)s[i])) i++;
                        if (i < n && (s[i] == '"' || s[i] == '\'')) {
                            char q = s[i++];
                            P->v = s + i;
                            while (i < n && s[i] != q) i++;
                            P->vn = (unsigned int)(s + i - P->v);
                            if (i < n) i++;
                        } else {
                            P->v = s + i;
                            while (i < n && s[i] != ']' && !is_ws((unsigned char)s[i])) i++;
                            P->vn = (unsigned int)(s + i - P->v);
                        }
                        while (i < n && is_ws((unsigned char)s[i])) i++;
                        if (i + 1 < n && (s[i] == 'i' || s[i] == 'I') ) { i++; while (i < n && is_ws((unsigned char)s[i])) i++; }
                    }
                    if (i >= n || s[i] != ']' || !P->n) return -1;
                    i++;
                    continue;
                }
                if (s[i] == ':') {
                    i++;
                    if (i < n && s[i] == ':') return -1;        /* pseudo-elements match nothing */
                    P->kind = ':';
                    P->s = s + i;
                    while (i < n && ident_char((unsigned char)s[i])) i++;
                    P->n = (unsigned int)(s + i - P->s);
                    if (i < n && s[i] == '(') {
                        int depth = 1;
                        P->v = s + ++i;
                        while (i < n && depth) { if (s[i] == '(') depth++; else if (s[i] == ')') depth--; if (depth) i++; }
                        if (i >= n) return -1;
                        P->vn = (unsigned int)(s + i - P->v);
                        i++;
                    }
                    if (!pseudo_known(P))
                        return -1;
                    continue;
                }
                if (ident_char((unsigned char)s[i])) {
                    P->kind = 't';
                    P->s = s + i;
                    while (i < n && ident_char((unsigned char)s[i])) i++;
                    P->n = (unsigned int)(s + i - P->s);
                    continue;
                }
                return -1;
            }
            if (!C->np) return -1;
        }
        if (!X->nc) return -1;
        while (i < n && is_ws((unsigned char)s[i])) i++;
        if (i >= n) return 0;
        if (s[i] != ',') return -1;
        i++;
    }
}

static BwNode* parent_el(BwNode* n) { n = n->parent; return n && n->type == BWN_ELEMENT ? n : 0; }
static BwNode* prev_el(BwNode* n) { for (n = n->prev; n; n = n->prev) if (n->type == BWN_ELEMENT) return n; return 0; }
static BwNode* next_el(BwNode* n) { for (n = n->next; n; n = n->next) if (n->type == BWN_ELEMENT) return n; return 0; }

static int match_compound(BwNode* n, const SCompound* C);
static int match_list(BwNode* n, const SList* L);

/* "an+b" (odd, even, 3, -n+2, 2n...) */
static int nth_parse(const char* v, unsigned int vn, int* a, int* b)
{
    unsigned int i = 0;
    int sign = 1, num = 0, any = 0;
    char buf[32];
    unsigned int k = 0;
    for (; i < vn && k < sizeof buf - 1; i++) if (!is_ws((unsigned char)v[i])) buf[k++] = (char)lc((unsigned char)v[i]);
    buf[k] = 0;
    if (!strcmp(buf, "odd")) { *a = 2; *b = 1; return 0; }
    if (!strcmp(buf, "even")) { *a = 2; *b = 0; return 0; }
    i = 0;
    if (buf[i] == '+' || buf[i] == '-') { sign = buf[i] == '-' ? -1 : 1; i++; }
    while (buf[i] >= '0' && buf[i] <= '9') { num = num * 10 + (buf[i++] - '0'); any = 1; }
    if (buf[i] == 'n') {
        *a = sign * (any ? num : 1);
        i++;
        *b = 0;
        if (buf[i] == '+' || buf[i] == '-') {
            int s2 = buf[i] == '-' ? -1 : 1, m = 0;
            i++;
            if (!(buf[i] >= '0' && buf[i] <= '9')) return -1;
            while (buf[i] >= '0' && buf[i] <= '9') m = m * 10 + (buf[i++] - '0');
            *b = s2 * m;
        }
    } else {
        if (!any) return -1;
        *a = 0;
        *b = sign * num;
    }
    return buf[i] ? -1 : 0;
}

static int nth_ok(int i, int a, int b)
{
    if (a == 0) return i == b;
    return (i - b) / a >= 0 && (i - b) % a == 0;
}

/* the element's 1-based position among its element siblings (of its type), from the end */
static int sib_index(BwNode* n, int oftype, int fromEnd)
{
    BwNode* k;
    int i = 1;
    for (k = fromEnd ? n->next : n->prev; k; k = fromEnd ? k->next : k->prev)
        if (k->type == BWN_ELEMENT && (!oftype || eq(k->name, n->name))) i++;
    return i;
}

/* :is() / :where() / :not() / :has() arguments, parsed as a selector list */
static int match_arg(BwNode* n, const SPart* P, int has)
{
    char buf[256];
    SList* L;
    int r = 0, child = 0;
    unsigned int off = 0;
    BwNode* d;
    if (P->vn >= sizeof buf) return 0;
    while (off < P->vn && is_ws((unsigned char)P->v[off])) off++;
    if (has && off < P->vn && P->v[off] == '>') { child = 1; off++; }
    memcpy(buf, P->v + off, P->vn - off);
    buf[P->vn - off] = 0;
    L = (SList*)malloc(sizeof(SList));
    if (!L) return 0;
    if (sel_parse(buf, L) == 0) {
        if (!has) r = match_list(n, L);
        else if (child) { for (d = n->first; d && !r; d = d->next) if (d->type == BWN_ELEMENT && match_list(d, L)) r = 1; }
        else { for (d = n->first; d && !r; d = bwd_next(n, d, 1)) if (d->type == BWN_ELEMENT && match_list(d, L)) r = 1; }
    } else if (!has && eq_ci_n(P->s, P->n, "not")) {
        r = 0;
    }
    free(L);
    return r;
}

static int match_part(BwNode* n, const SPart* P)
{
    const char* v;
    switch (P->kind) {
    case '*': return 1;
    case 't': return eq_ci_n(P->s, P->n, n->name);
    case 'i': v = bwd_attr(n, "id"); return v && strlen(v) == P->n && memcmp(v, P->s, P->n) == 0;
    case 'c': return bwd_has_class(n, P->s, P->n);
    case 'a': {
        char nm[48];
        unsigned int k, vl;
        if (P->n >= sizeof nm) return 0;
        for (k = 0; k < P->n; k++) nm[k] = (char)lc((unsigned char)P->s[k]);
        nm[P->n] = 0;
        v = bwd_attr(n, nm);
        if (!v) return 0;
        if (!P->op) return 1;
        vl = (unsigned int)strlen(v);
        switch (P->op) {
        case '=': return vl == P->vn && memcmp(v, P->v, vl) == 0;
        case '^': return P->vn && vl >= P->vn && memcmp(v, P->v, P->vn) == 0;
        case '$': return P->vn && vl >= P->vn && memcmp(v + vl - P->vn, P->v, P->vn) == 0;
        case '*': {
            unsigned int i;
            if (!P->vn) return 0;
            for (i = 0; i + P->vn <= vl; i++) if (memcmp(v + i, P->v, P->vn) == 0) return 1;
            return 0;
        }
        case '~': {
            const char* s = v;
            while (*s) {
                const char* b;
                while (*s && is_ws((unsigned char)*s)) s++;
                b = s;
                while (*s && !is_ws((unsigned char)*s)) s++;
                if ((unsigned int)(s - b) == P->vn && memcmp(b, P->v, P->vn) == 0) return 1;
            }
            return 0;
        }
        case '|': return (vl == P->vn && memcmp(v, P->v, vl) == 0) || (vl > P->vn && memcmp(v, P->v, P->vn) == 0 && v[P->vn] == '-');
        }
        return 0;
    }
    case ':':
        if (eq_ci_n(P->s, P->n, "first-child")) return !prev_el(n);
        if (eq_ci_n(P->s, P->n, "last-child")) return !next_el(n);
        if (eq_ci_n(P->s, P->n, "only-child")) return !prev_el(n) && !next_el(n);
        if (eq_ci_n(P->s, P->n, "empty")) return n->first == 0;
        if (eq_ci_n(P->s, P->n, "checked")) return bwd_attr(n, "checked") != 0 || bwd_attr(n, "selected") != 0;
        if (eq_ci_n(P->s, P->n, "disabled")) return bwd_attr(n, "disabled") != 0;
        if (eq_ci_n(P->s, P->n, "enabled")) return bwd_attr(n, "disabled") == 0;
        if (eq_ci_n(P->s, P->n, "root")) return n->parent && n->parent->type == BWN_DOCUMENT;
        if (eq_ci_n(P->s, P->n, "link") || eq_ci_n(P->s, P->n, "any-link")) return (eq(n->name, "a") || eq(n->name, "area")) && bwd_attr(n, "href") != 0;
        if (eq_ci_n(P->s, P->n, "first-of-type")) return sib_index(n, 1, 0) == 1;
        if (eq_ci_n(P->s, P->n, "last-of-type")) return sib_index(n, 1, 1) == 1;
        if (eq_ci_n(P->s, P->n, "only-of-type")) return sib_index(n, 1, 0) == 1 && sib_index(n, 1, 1) == 1;
        if (eq_ci_n(P->s, P->n, "scope")) return n->parent && n->parent->type == BWN_DOCUMENT;
        if (eq_ci_n(P->s, P->n, "defined")) return 1;
        if (eq_ci_n(P->s, P->n, "required")) return bwd_attr(n, "required") != 0;
        if (eq_ci_n(P->s, P->n, "optional")) return bwd_attr(n, "required") == 0 && (eq(n->name, "input") || eq(n->name, "select") || eq(n->name, "textarea"));
        if (eq_ci_n(P->s, P->n, "read-only")) return bwd_attr(n, "readonly") != 0 || !(eq(n->name, "input") || eq(n->name, "textarea"));
        if (eq_ci_n(P->s, P->n, "read-write")) return bwd_attr(n, "readonly") == 0 && (eq(n->name, "input") || eq(n->name, "textarea"));
        if (eq_ci_n(P->s, P->n, "placeholder-shown")) { v = bwd_attr(n, "value"); return bwd_attr(n, "placeholder") != 0 && (!v || !*v); }
        if (eq_ci_n(P->s, P->n, "valid")) return 1;
        if (eq_ci_n(P->s, P->n, "not")) return !match_arg(n, P, 0);
        if (eq_ci_n(P->s, P->n, "is") || eq_ci_n(P->s, P->n, "where") || eq_ci_n(P->s, P->n, "matches")) return match_arg(n, P, 0);
        if (eq_ci_n(P->s, P->n, "has")) return match_arg(n, P, 1);
        if (eq_ci_n(P->s, P->n, "lang")) {
            BwNode* k;
            for (k = n; k && k->type == BWN_ELEMENT; k = k->parent)
                if ((v = bwd_attr(k, "lang")) != 0) return strlen(v) >= P->vn && !strncmp(v, P->v, P->vn) && (v[P->vn] == 0 || v[P->vn] == '-');
            return 0;
        }
        if (eq_ci_n(P->s, P->n, "dir")) return eq_ci_n(P->v, P->vn, "ltr");
        if (P->v && (eq_ci_n(P->s, P->n, "nth-child") || eq_ci_n(P->s, P->n, "nth-last-child") ||
                     eq_ci_n(P->s, P->n, "nth-of-type") || eq_ci_n(P->s, P->n, "nth-last-of-type"))) {
            int a, b, oftype = P->n > 9 && (P->s[P->n - 7] == 'o' || P->s[P->n - 7] == 'O');
            int last = P->s[4] == 'l' || P->s[4] == 'L';
            if (nth_parse(P->v, P->vn, &a, &b)) return 0;
            return nth_ok(sib_index(n, oftype, last), a, b);
        }
        return 0;  /* :hover :focus :visited ... */
    }
    return 0;
}

static int match_compound(BwNode* n, const SCompound* C)
{
    int k;
    if (n->type != BWN_ELEMENT) return 0;
    for (k = 0; k < C->np; k++) if (!match_part(n, &C->part[k])) return 0;
    return 1;
}

static int match_complex(BwNode* n, const SComplex* X, int k)
{
    BwNode* a;
    if (!match_compound(n, &X->c[k])) return 0;
    if (k == 0) return 1;
    switch (X->c[k].comb) {
    case '>': a = parent_el(n); return a && match_complex(a, X, k - 1);
    case '+': a = prev_el(n); return a && match_complex(a, X, k - 1);
    case '~': for (a = prev_el(n); a; a = prev_el(a)) if (match_complex(a, X, k - 1)) return 1; return 0;
    default: for (a = parent_el(n); a; a = parent_el(a)) if (match_complex(a, X, k - 1)) return 1; return 0;
    }
}

static int match_list(BwNode* n, const SList* L)
{
    int k;
    for (k = 0; k < L->nx; k++) if (match_complex(n, &L->x[k], L->x[k].nc - 1)) return 1;
    return 0;
}

int bwd_matches(BwNode* n, const char* sel)
{
    SList* L = (SList*)malloc(sizeof(SList));
    int r;
    if (!L) return -1;
    r = sel_parse(sel, L) ? -1 : match_list(n, L);
    free(L);
    return r;
}

int bwd_select(BwNode* root, const char* sel, int (*each)(BwNode* n, void* ctx), void* ctx)
{
    SList* L = (SList*)malloc(sizeof(SList));
    BwNode* n = root;
    if (!L) return -1;
    if (sel_parse(sel, L)) { free(L); return -1; }
    while ((n = bwd_next(root, n, 1)) != 0)
        if (n->type == BWN_ELEMENT && match_list(n, L) && each(n, ctx)) break;
    free(L);
    return 0;
}
