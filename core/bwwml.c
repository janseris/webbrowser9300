/* Browser 9300: WAP pages. A WML deck (text/vnd.wap.wml) becomes an HTML page the browser's
 * parser and layout show as any other: every card in turn (with its id as an anchor, so a link
 * to "#card" scrolls to it), <do> and <anchor> as links or a form's button, a <go> with
 * <postfield>s as a form, <select> menus whose options navigate (onpick) as links, $(var)
 * substituted from <setvar>, a first card's timer or onenterforward as a refresh. The text keeps
 * its bytes (the charset is the deck's), entities are the same in both. */
#include "igcore.h"
#include "bwcore.h"

/* ---- a growable output */
typedef struct { char* p; unsigned int n, cap; int oom; } WBuf;

static void wb_add(WBuf* b, const char* s, unsigned int n)
{
    if (b->oom || !n) return;
    if (b->n + n + 1 > b->cap) {
        unsigned int cap = b->cap ? b->cap * 2 : 4096;
        char* q;
        while (cap < b->n + n + 1) cap *= 2;
        q = (char*)ig_malloc(cap);
        if (!q) { b->oom = 1; return; }
        if (b->p) { ig_memcpy(q, b->p, b->n); ig_free(b->p); }
        b->p = q; b->cap = cap;
    }
    ig_memcpy(b->p + b->n, s, n);
    b->n += n;
    b->p[b->n] = 0;
}
static void wb_str(WBuf* b, const char* s) { wb_add(b, s, ig_strlen(s)); }

/* ---- the deck as a tree */
#define W_MAXATTR 12
typedef struct WNode {
    const char* name; unsigned int nameLen;     /* NULL: text */
    const char* text; unsigned int textLen;     /* the raw text (entities as they are) */
    const char* an[W_MAXATTR]; unsigned int anl[W_MAXATTR];
    const char* av[W_MAXATTR]; unsigned int avl[W_MAXATTR];
    int nattr;
    struct WNode *parent, *first, *last, *next;
} WNode;

typedef struct {
    WNode* pool; int used, cap;
    const char* vn[32]; unsigned int vnl[32];   /* the variables (<setvar>) */
    const char* vv[32]; unsigned int vvl[32];
    int nvar;
    const char* rn[16]; unsigned int rnl[16];   /* inputs renamed to their postfield's name */
    const char* rp[16]; unsigned int rpl[16];
    int nren;
} WDeck;

static int lc(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int eqn(const char* a, unsigned int n, const char* lit)
{
    unsigned int i;
    for (i = 0; i < n; i++) if (!lit[i] || lc((unsigned char)a[i]) != lit[i]) return 0;
    return lit[n] == 0;
}
static int is_ws(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

static WNode* new_node(WDeck* d, WNode* parent)
{
    WNode* n;
    if (d->used >= d->cap) return 0;
    n = &d->pool[d->used++];
    ig_memset(n, 0, sizeof *n);
    n->parent = parent;
    if (parent) {
        if (parent->last) parent->last->next = n; else parent->first = n;
        parent->last = n;
    }
    return n;
}

static int find_end(const char* s, unsigned int i, unsigned int n, const char* lit)
{
    unsigned int l = ig_strlen(lit);
    for (; i + l <= n; i++) if (!ig_memcmp(s + i, lit, l)) return (int)i;
    return -1;
}

/* XML, tolerantly: elements, attributes, text, comments, CDATA; <?...?> and <!DOCTYPE> skipped */
static WNode* parse(WDeck* d, const char* s, unsigned int n)
{
    WNode* root = new_node(d, 0);
    WNode* cur = root;
    unsigned int i = 0;
    if (!root) return 0;
    while (i < n) {
        if (s[i] == '<') {
            int e;
            if (i + 4 <= n && !ig_memcmp(s + i, "<!--", 4)) { e = find_end(s, i + 4, n, "-->"); i = e < 0 ? n : (unsigned int)e + 3; continue; }
            if (i + 9 <= n && !ig_memcmp(s + i, "<![CDATA[", 9)) {
                WNode* t;
                e = find_end(s, i + 9, n, "]]>");
                t = new_node(d, cur);
                if (t) { t->text = s + i + 9; t->textLen = (e < 0 ? n : (unsigned int)e) - (i + 9); }
                i = e < 0 ? n : (unsigned int)e + 3;
                continue;
            }
            if (i + 1 < n && (s[i + 1] == '?' || s[i + 1] == '!')) {
                while (i < n && s[i] != '>') i++;
                i++;
                continue;
            }
            if (i + 1 < n && s[i + 1] == '/') {     /* an end tag: up to the element it closes */
                unsigned int a = i + 2, b = a;
                WNode* k;
                while (b < n && s[b] != '>' && !is_ws((unsigned char)s[b])) b++;
                for (k = cur; k && k != root; k = k->parent)
                    if (k->nameLen == b - a && !ig_memcmp(k->name, s + a, b - a)) { cur = k->parent; break; }
                while (b < n && s[b] != '>') b++;
                i = b + 1;
                continue;
            }
            {   /* a start tag */
                WNode* el = new_node(d, cur);
                unsigned int a = i + 1;
                int empty = 0;
                if (!el) return root;
                el->name = s + a;
                while (a < n && !is_ws((unsigned char)s[a]) && s[a] != '>' && s[a] != '/') a++;
                el->nameLen = (unsigned int)(s + a - el->name);
                for (;;) {
                    unsigned int ns, ne;
                    while (a < n && is_ws((unsigned char)s[a])) a++;
                    if (a >= n) break;
                    if (s[a] == '>') { a++; break; }
                    if (s[a] == '/') { empty = 1; a++; continue; }
                    ns = a;
                    while (a < n && !is_ws((unsigned char)s[a]) && s[a] != '=' && s[a] != '>' && s[a] != '/') a++;
                    ne = a;
                    while (a < n && is_ws((unsigned char)s[a])) a++;
                    if (a < n && s[a] == '=') {
                        unsigned int vs;
                        a++;
                        while (a < n && is_ws((unsigned char)s[a])) a++;
                        if (a < n && (s[a] == '"' || s[a] == '\'')) {
                            char q = s[a++];
                            vs = a;
                            while (a < n && s[a] != q) a++;
                            if (el->nattr < W_MAXATTR) { el->an[el->nattr] = s + ns; el->anl[el->nattr] = ne - ns; el->av[el->nattr] = s + vs; el->avl[el->nattr] = a - vs; el->nattr++; }
                            if (a < n) a++;
                        } else {
                            vs = a;
                            while (a < n && !is_ws((unsigned char)s[a]) && s[a] != '>') a++;
                            if (el->nattr < W_MAXATTR) { el->an[el->nattr] = s + ns; el->anl[el->nattr] = ne - ns; el->av[el->nattr] = s + vs; el->avl[el->nattr] = a - vs; el->nattr++; }
                        }
                    } else if (ne > ns && el->nattr < W_MAXATTR) {
                        el->an[el->nattr] = s + ns; el->anl[el->nattr] = ne - ns; el->av[el->nattr] = s + ns; el->avl[el->nattr] = 0; el->nattr++;
                    }
                }
                i = a;
                if (!empty && !eqn(el->name, el->nameLen, "br") && !eqn(el->name, el->nameLen, "img") &&
                    !eqn(el->name, el->nameLen, "input") && !eqn(el->name, el->nameLen, "setvar") &&
                    !eqn(el->name, el->nameLen, "postfield") && !eqn(el->name, el->nameLen, "timer") &&
                    !eqn(el->name, el->nameLen, "prev") && !eqn(el->name, el->nameLen, "noop") &&
                    !eqn(el->name, el->nameLen, "meta") && !eqn(el->name, el->nameLen, "access"))
                    cur = el;
            }
        } else {
            WNode* t;
            unsigned int a = i;
            while (i < n && s[i] != '<') i++;
            t = new_node(d, cur);
            if (t) { t->text = s + a; t->textLen = i - a; }
        }
    }
    return root;
}

static int is(const WNode* n, const char* name) { return n && n->name && eqn(n->name, n->nameLen, name); }

static int attr(const WNode* n, const char* name, const char** v, unsigned int* vl)
{
    int k;
    for (k = 0; k < n->nattr; k++)
        if (eqn(n->an[k], n->anl[k], name)) { *v = n->av[k]; *vl = n->avl[k]; return 1; }
    return 0;
}

static WNode* child(const WNode* n, const char* name)
{
    WNode* k;
    for (k = n->first; k; k = k->next) if (is(k, name)) return k;
    return 0;
}

/* the first descendant element of that name */
static WNode* find(const WNode* n, const char* name)
{
    WNode* k;
    for (k = n->first; k; k = k->next) {
        WNode* r;
        if (is(k, name)) return k;
        if ((r = find(k, name)) != 0) return r;
    }
    return 0;
}

/* ---- variables */
static void setvars(WDeck* d, const WNode* n)
{
    WNode* k;
    for (k = n->first; k; k = k->next) {
        if (is(k, "setvar") && d->nvar < 32) {
            const char *nm, *v;
            unsigned int nml, vl;
            if (attr(k, "name", &nm, &nml) && attr(k, "value", &v, &vl)) {
                d->vn[d->nvar] = nm; d->vnl[d->nvar] = nml; d->vv[d->nvar] = v; d->vvl[d->nvar] = vl; d->nvar++;
            }
        }
        if (k->name) setvars(d, k);
    }
}

/* text or an attribute's value with $(var), $(var:e), $var and $$ substituted */
static void subst(WDeck* d, WBuf* b, const char* s, unsigned int n)
{
    unsigned int i = 0, a = 0;
    while (i < n) {
        if (s[i] == '$') {
            unsigned int ns, ne;
            int k;
            wb_add(b, s + a, i - a);
            if (i + 1 < n && s[i + 1] == '$') { wb_add(b, "$", 1); i += 2; a = i; continue; }
            if (i + 1 < n && s[i + 1] == '(') {
                ns = i + 2;
                ne = ns;
                while (ne < n && s[ne] != ')' && s[ne] != ':') ne++;
                i = ne;
                while (i < n && s[i] != ')') i++;
                if (i < n) i++;
            } else {
                ns = i + 1;
                ne = ns;
                while (ne < n && ((s[ne] >= 'a' && s[ne] <= 'z') || (s[ne] >= 'A' && s[ne] <= 'Z') || (s[ne] >= '0' && s[ne] <= '9') || s[ne] == '_')) ne++;
                i = ne;
                if (ne == ns) { wb_add(b, "$", 1); a = i; continue; }
            }
            for (k = d->nvar - 1; k >= 0; k--)
                if (d->vnl[k] == ne - ns && !ig_memcmp(d->vn[k], s + ns, ne - ns)) { wb_add(b, d->vv[k], d->vvl[k]); break; }
            a = i;
            continue;
        }
        i++;
    }
    wb_add(b, s + a, n - a);
}

static void out_attr(WDeck* d, WBuf* b, const WNode* n, const char* name, const char* as)
{
    const char* v;
    unsigned int vl;
    if (!attr(n, name, &v, &vl)) return;
    wb_str(b, " ");
    wb_str(b, as ? as : name);
    wb_str(b, "=\"");
    subst(d, b, v, vl);
    wb_str(b, "\"");
}

/* ---- cards */
static void emit(WDeck* d, WBuf* b, const WNode* n, const WNode* form);

/* the text of a node's children (a <do>'s or an <option>'s label) */
static void emit_children(WDeck* d, WBuf* b, const WNode* n, const WNode* form)
{
    WNode* k;
    for (k = n->first; k; k = k->next) emit(d, b, k, form);
}

static int has_post(const WNode* go)
{
    const char* v;
    unsigned int vl;
    if (child(go, "postfield")) return 1;
    return attr(go, "method", &v, &vl) && eqn(v, vl, "post");
}

/* a <do> / <anchor> / onpick's target: a <go> (a link, or this card's form's button), <prev>,
 * <refresh> */
static void emit_action(WDeck* d, WBuf* b, const WNode* holder, const WNode* label, const WNode* form, int isDo)
{
    WNode* go = child(holder, "go");
    const char *v, *lab;
    unsigned int vl, labl;
    if (go && go == form) {
        wb_str(b, "<input type=\"submit\" value=\"");
        if (isDo && attr(holder, "label", &lab, &labl)) subst(d, b, lab, labl);
        else if (isDo) wb_str(b, "OK");
        else {
            WNode* k;
            for (k = holder->first; k; k = k->next) if (!k->name) subst(d, b, k->text, k->textLen);
        }
        wb_str(b, "\">");
        return;
    }
    if (go && attr(go, "href", &v, &vl)) {
        wb_str(b, "<a href=\"");
        subst(d, b, v, vl);
        wb_str(b, "\">");
    } else if (child(holder, "refresh")) {
        wb_str(b, "<a href=\"\">");
    } else {
        wb_str(b, "<span>");     /* <prev/>, <noop/>: the browser's Back does it */
    }
    if (isDo) {
        wb_str(b, "[");
        if (attr(holder, "label", &lab, &labl) && labl) subst(d, b, lab, labl);
        else if (attr(holder, "type", &lab, &labl) && labl) subst(d, b, lab, labl);
        else wb_str(b, "OK");
        wb_str(b, "]");
    } else if (label) emit_children(d, b, label, form);
    wb_str(b, go || child(holder, "refresh") ? "</a>" : "</span>");
}

static void emit(WDeck* d, WBuf* b, const WNode* n, const WNode* form)
{
    const char* v;
    unsigned int vl;
    if (!n->name) { subst(d, b, n->text, n->textLen); return; }
    if (is(n, "p")) {
        wb_str(b, "<p");
        out_attr(d, b, n, "align", 0);
        if (attr(n, "mode", &v, &vl) && eqn(v, vl, "nowrap")) wb_str(b, " style=\"white-space:nowrap\"");
        wb_str(b, ">");
        emit_children(d, b, n, form);
        wb_str(b, "</p>");
        return;
    }
    if (is(n, "br")) { wb_str(b, "<br>"); return; }
    if (is(n, "b") || is(n, "i") || is(n, "u") || is(n, "big") || is(n, "small") || is(n, "em") || is(n, "strong") ||
        is(n, "pre") || is(n, "table") || is(n, "tr") || is(n, "td") || is(n, "fieldset")) {
        wb_str(b, "<"); wb_add(b, n->name, n->nameLen);
        if (is(n, "table")) wb_str(b, " border=\"0\"");
        wb_str(b, ">");
        emit_children(d, b, n, form);
        wb_str(b, "</"); wb_add(b, n->name, n->nameLen); wb_str(b, ">");
        return;
    }
    if (is(n, "img")) {
        wb_str(b, "<img");
        out_attr(d, b, n, "src", 0); out_attr(d, b, n, "alt", 0); out_attr(d, b, n, "width", 0);
        out_attr(d, b, n, "height", 0); out_attr(d, b, n, "align", 0);
        wb_str(b, ">");
        return;
    }
    if (is(n, "a")) {
        wb_str(b, "<a");
        out_attr(d, b, n, "href", 0);
        wb_str(b, ">");
        emit_children(d, b, n, form);
        wb_str(b, "</a>");
        return;
    }
    if (is(n, "anchor")) { emit_action(d, b, n, n, form, 0); return; }
    if (is(n, "do")) {     /* (one that only goes back is the browser's Back: left out) */
        if (!child(n, "go") && !child(n, "refresh")) return;
        wb_str(b, " "); emit_action(d, b, n, 0, form, 1); wb_str(b, " ");
        return;
    }
    if (is(n, "input")) {
        int k;
        wb_str(b, "<input");
        if (attr(n, "type", &v, &vl) && eqn(v, vl, "password")) wb_str(b, " type=\"password\"");
        if (attr(n, "name", &v, &vl)) {
            wb_str(b, " name=\"");
            for (k = 0; k < d->nren; k++) if (d->rnl[k] == vl && !ig_memcmp(d->rn[k], v, vl)) { v = d->rp[k]; vl = d->rpl[k]; break; }
            wb_add(b, v, vl);
            wb_str(b, "\"");
            if (!attr(n, "value", &v, &vl)) {   /* its variable's value */
                const char* nm; unsigned int nml;
                attr(n, "name", &nm, &nml);
                for (k = d->nvar - 1; k >= 0; k--)
                    if (d->vnl[k] == nml && !ig_memcmp(d->vn[k], nm, nml)) { wb_str(b, " value=\""); wb_add(b, d->vv[k], d->vvl[k]); wb_str(b, "\""); break; }
            }
        }
        out_attr(d, b, n, "value", 0); out_attr(d, b, n, "size", 0); out_attr(d, b, n, "maxlength", 0);
        wb_str(b, ">");
        return;
    }
    if (is(n, "select")) {
        WNode* o;
        int nav = 0;
        for (o = n->first; o; o = o->next) if (is(o, "option") && attr(o, "onpick", &v, &vl)) nav = 1;
        if (nav) {      /* a menu: its options are links */
            wb_str(b, "<ul>");
            for (o = n->first; o; o = o->next) {
                if (!is(o, "option")) continue;
                wb_str(b, "<li>");
                if (attr(o, "onpick", &v, &vl)) { wb_str(b, "<a href=\""); subst(d, b, v, vl); wb_str(b, "\">"); emit_children(d, b, o, form); wb_str(b, "</a>"); }
                else if (find(o, "go")) emit_action(d, b, (WNode*)find(o, "go")->parent, o, form, 0);
                else emit_children(d, b, o, form);
                wb_str(b, "</li>");
            }
            wb_str(b, "</ul>");
            return;
        }
        wb_str(b, "<select");
        out_attr(d, b, n, "name", 0);
        if (attr(n, "multiple", &v, &vl) && eqn(v, vl, "true")) wb_str(b, " multiple");
        wb_str(b, ">");
        for (o = n->first; o; o = o->next) {
            if (is(o, "optgroup")) { wb_str(b, "<optgroup"); out_attr(d, b, o, "title", "label"); wb_str(b, ">"); emit_children(d, b, o, form); wb_str(b, "</optgroup>"); }
            else if (is(o, "option")) { wb_str(b, "<option"); out_attr(d, b, o, "value", 0); wb_str(b, ">"); emit_children(d, b, o, form); wb_str(b, "</option>"); }
        }
        wb_str(b, "</select>");
        return;
    }
    if (is(n, "option")) { wb_str(b, "<option"); out_attr(d, b, n, "value", 0); wb_str(b, ">"); emit_children(d, b, n, form); wb_str(b, "</option>"); return; }
    if (is(n, "optgroup")) { emit_children(d, b, n, form); return; }
    /* not shown: go postfield setvar timer onevent refresh prev noop access meta head template */
    if (is(n, "go") || is(n, "postfield") || is(n, "setvar") || is(n, "timer") || is(n, "onevent") ||
        is(n, "refresh") || is(n, "prev") || is(n, "noop") || is(n, "access") || is(n, "meta") ||
        is(n, "head") || is(n, "template"))
        return;
    emit_children(d, b, n, form);   /* anything else: its content */
}

/* the card's form: its first <go> with postfields or method="post" (inputs renamed to the
 * names their values are posted under) */
static WNode* find_post_go(const WNode* n)
{
    WNode* k;
    for (k = n->first; k; k = k->next) {
        WNode* r;
        if (is(k, "go") && has_post(k)) return k;
        if (k->name && (r = find_post_go(k)) != 0) return r;
    }
    return 0;
}

static const WNode* card_form(WDeck* d, const WNode* card)
{
    WNode* go = find_post_go(card);
    const WNode* k;
    d->nren = 0;
    if (!go) return 0;
    for (k = go->first; k; k = k->next) {
        const char *nm, *v;
        unsigned int nml, vl;
        if (is(k, "postfield") && attr(k, "name", &nm, &nml) && attr(k, "value", &v, &vl) && vl > 3 && v[0] == '$' && d->nren < 16) {
            unsigned int s = v[1] == '(' ? 2 : 1, e = s;
            while (e < vl && v[e] != ')' && v[e] != ':') e++;
            d->rn[d->nren] = v + s; d->rnl[d->nren] = e - s; d->rp[d->nren] = nm; d->rpl[d->nren] = nml; d->nren++;
        }
    }
    return go;
}

static void emit_card(WDeck* d, WBuf* b, const WNode* card, const WNode* tmpl, int first)
{
    const char* v;
    unsigned int vl;
    const WNode* form = card_form(d, card);
    WNode* k;
    if (!first) wb_str(b, "<hr>");
    if (attr(card, "id", &v, &vl)) { wb_str(b, "<a name=\""); wb_add(b, v, vl); wb_str(b, "\"></a>"); }
    if (attr(card, "title", &v, &vl) && vl) { wb_str(b, "<h3>"); subst(d, b, v, vl); wb_str(b, "</h3>"); }
    if (form) {
        wb_str(b, "<form");
        out_attr(d, b, form, "href", "action");
        if (has_post(form)) wb_str(b, " method=\"post\"");
        wb_str(b, ">");
        for (k = form->first; k; k = k->next) {  /* constant postfields */
            const char *nm;
            unsigned int nml;
            if (is(k, "postfield") && attr(k, "name", &nm, &nml) && attr(k, "value", &v, &vl) && !(vl > 3 && v[0] == '$')) {
                wb_str(b, "<input type=\"hidden\" name=\""); wb_add(b, nm, nml); wb_str(b, "\" value=\""); subst(d, b, v, vl); wb_str(b, "\">");
            }
        }
    }
    for (k = card->first; k; k = k->next) emit(d, b, k, form);
    if (tmpl)       /* the deck's <do>s, on every card */
        for (k = tmpl->first; k; k = k->next)
            if (is(k, "do") && (child(k, "go") || child(k, "refresh"))) { wb_str(b, "<p>"); emit(d, b, k, form); wb_str(b, "</p>"); }
    if (form) wb_str(b, "</form>");
}

int bw_is_wml(const char* contentType)
{
    unsigned int n;
    if (!contentType) return 0;
    while (*contentType == ' ') contentType++;
    for (n = 0; contentType[n] && contentType[n] != ';' && contentType[n] != ' '; n++) {}
    return eqn(contentType, n, "text/vnd.wap.wml");
}

char* bw_wml_to_html(const char* s, unsigned int n, unsigned int* outLen)
{
    WDeck d;
    WBuf b;
    WNode *root, *wml, *k, *first = 0, *tmpl;
    const char* v;
    unsigned int vl;
    int cards = 0;
    ig_memset(&d, 0, sizeof d);
    ig_memset(&b, 0, sizeof b);
    d.cap = 64 + (int)(n / 8);
    d.pool = (WNode*)ig_malloc((unsigned int)d.cap * sizeof(WNode));
    if (!d.pool) return 0;
    root = parse(&d, s, n);
    wml = root ? find(root, "wml") : 0;
    if (!wml) wml = root;
    if (!wml) { ig_free(d.pool); return 0; }
    setvars(&d, wml);
    tmpl = child(wml, "template");
    for (k = wml->first; k; k = k->next) if (is(k, "card")) { first = k; break; }
    wb_str(&b, "<html><head>");
    {   /* the deck's encoding (the bytes stay as they are) */
        int e = find_end(s, 0, n < 200 ? n : 200, "encoding=");
        if (e >= 0 && (unsigned int)e + 10 < n) {
            unsigned int a = (unsigned int)e + 10, z = a;
            while (z < n && s[z] != '"' && s[z] != '\'' && z - a < 40) z++;
            wb_str(&b, "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=");
            wb_add(&b, s + a, z - a);
            wb_str(&b, "\">");
        }
    }
    if (first) {
        WNode* ev;
        const char* to = 0;
        unsigned int tol = 0, secs = 0;
        if (attr(first, "title", &v, &vl)) { wb_str(&b, "<title>"); subst(&d, &b, v, vl); wb_str(&b, "</title>"); }
        /* the first card's timer (tenths of a second) or onenterforward: a refresh */
        if (attr(first, "onenterforward", &v, &vl) && vl) { to = v; tol = vl; secs = 0; }
        for (ev = first->first; ev && !to; ev = ev->next)
            if (is(ev, "onevent") && attr(ev, "type", &v, &vl) && eqn(v, vl, "onenterforward") && child(ev, "go") && attr(child(ev, "go"), "href", &to, &tol)) secs = 0;
        if (!to) {
            WNode* t = find(first, "timer");
            if (t && attr(t, "value", &v, &vl)) {
                unsigned int i, tenths = 0;
                for (i = 0; i < vl && v[i] >= '0' && v[i] <= '9'; i++) tenths = tenths * 10 + (unsigned int)(v[i] - '0');
                secs = (tenths + 9) / 10;
                if (attr(first, "ontimer", &v, &vl)) { to = v; tol = vl; }
                for (ev = first->first; ev && !to; ev = ev->next)
                    if (is(ev, "onevent") && attr(ev, "type", &v, &vl) && eqn(v, vl, "ontimer") && child(ev, "go")) attr(child(ev, "go"), "href", &to, &tol);
            }
        }
        if (to) {
            char num[12];
            unsigned int i = 0, x = secs;
            char tmp[12];
            int j = 0;
            do { tmp[j++] = (char)('0' + x % 10); x /= 10; } while (x && j < 11);
            while (j) num[i++] = tmp[--j];
            num[i] = 0;
            wb_str(&b, "<meta http-equiv=\"refresh\" content=\"");
            wb_str(&b, num);
            wb_str(&b, "; url=");
            subst(&d, &b, to, tol);
            wb_str(&b, "\">");
        }
    }
    wb_str(&b, "</head><body>");
    for (k = wml->first; k; k = k->next)
        if (is(k, "card")) emit_card(&d, &b, k, tmpl, cards++ == 0);
    if (!cards) emit_children(&d, &b, wml, 0);
    wb_str(&b, "</body></html>");
    ig_free(d.pool);
    if (b.oom) { ig_free(b.p); return 0; }
    *outLen = b.n;
    return b.p;
}
