/* Browser 9300: the DOM for a page's scripts (see bwjs.h).
 *
 * Each node of the tree (bwdom.c) gets a JavaScript wrapper only when a script reaches it: a
 * userdata object kept in the registry under its serial, so the same node is always the same
 * object. Everything structural is here in C (the tree, attributes, innerHTML, queries); the
 * conveniences built on it (reflected properties, classList, style, events) are in prelude.js.
 * NodeLists are plain arrays (not live). C89, no static data. */

#include <string.h>
#include <stdlib.h>
#include "bwjs.h"

#define DOM(J) ((BwJsDom*)js_getcontext(J))

void bwdj_note(BwJsDom* d, const char* a, const char* b)
{
    if (d->say.n > 8000) return;
    bwb_str(&d->say, a);
    if (b) { unsigned int n = (unsigned int)strlen(b); bwb_add(&d->say, b, n > 300 ? 300 : n); }
    bwb_add(&d->say, "\n", 1);
}

static void key_for(unsigned int serial, char* key)
{
    char t[12];
    int i = 0, j;
    do { t[i++] = (char)('0' + serial % 10); serial /= 10; } while (serial && i < 11);
    key[0] = 'n';
    for (j = 0; j < i; j++) key[1 + j] = t[i - 1 - j];
    key[1 + i] = 0;
}

void bwdj_push_node(js_State* J, BwNode* n)
{
    char key[16];
    const char* proto;
    if (!n) { js_pushnull(J); return; }
    key_for(n->serial, key);
    js_getregistry(J, key);
    if (js_isuserdata(J, -1, "node")) return;
    js_pop(J, 1);
    switch (n->type) {
    case BWN_ELEMENT: proto = "pEl"; break;
    case BWN_TEXT: proto = "pText"; break;
    case BWN_COMMENT: proto = "pComment"; break;
    case BWN_DOCUMENT: proto = "pDoc"; break;
    case BWN_FRAGMENT: proto = "pFrag"; break;
    default: proto = "pNode"; break;
    }
    js_getregistry(J, proto);
    js_newuserdata(J, "node", n, 0);
    js_copy(J, -1);
    js_setregistry(J, key);
}

static BwNode* node_at(js_State* J, int i)
{
    if (!js_isuserdata(J, i, "node")) js_typeerror(J, "not a node");
    return (BwNode*)js_touserdata(J, i, "node");
}
static BwNode* self(js_State* J) { return node_at(J, 0); }
static BwNode* opt_node(js_State* J, int i) { return js_isundefined(J, i) || js_isnull(J, i) ? 0 : node_at(J, i); }

static void push_buf(js_State* J, BwBuf* b)
{
    if (b->oom) { free(b->p); js_error(J, "out of memory"); }
    js_pushstring(J, b->p ? b->p : "");
    free(b->p);
}

static void lower_copy(const char* s, char* out, unsigned int max)
{
    unsigned int k;
    for (k = 0; s[k] && k < max - 1; k++) out[k] = (char)(s[k] >= 'A' && s[k] <= 'Z' ? s[k] + 32 : s[k]);
    out[k] = 0;
}

/* a JS string as raw HTML text (escaped), malloc'ed */
static char* escaped(const char* s, int attr)
{
    BwBuf b;
    memset(&b, 0, sizeof b);
    bwd_escape(s, (unsigned int)strlen(s), attr, &b);
    if (b.oom) { free(b.p); return 0; }
    if (!b.p) return (char*)calloc(1, 1);
    return b.p;
}

static BwNode* new_text(js_State* J, const char* s, int raw)
{
    BwJsDom* d = DOM(J);
    BwNode* t = bwd_node(d->tree, BWN_TEXT, 0, 0);
    char* e;
    if (!t) js_error(J, "out of memory");
    e = raw ? 0 : escaped(s, 0);
    if (raw) { if (bwd_set_text(t, s, (unsigned int)strlen(s))) js_error(J, "out of memory"); }
    else { if (!e) js_error(J, "out of memory"); free(t->text); t->text = e; }
    return t;
}

static void remove_children(BwNode* n) { while (n->first) bwd_remove(n->first); }

/* ------------------------------------------------------------------ Node */

static void Node_nodeType(js_State* J) { js_pushnumber(J, self(J)->type); }
static void Node_nodeName(js_State* J)
{
    BwNode* n = self(J);
    switch (n->type) {
    case BWN_ELEMENT: {
        char up[64];
        unsigned int k;
        for (k = 0; n->name[k] && k < sizeof up - 1; k++) up[k] = (char)(n->name[k] >= 'a' && n->name[k] <= 'z' ? n->name[k] - 32 : n->name[k]);
        up[k] = 0;
        js_pushstring(J, up);
        return;
    }
    case BWN_TEXT: js_pushliteral(J, "#text"); return;
    case BWN_COMMENT: js_pushliteral(J, "#comment"); return;
    case BWN_DOCUMENT: js_pushliteral(J, "#document"); return;
    case BWN_FRAGMENT: js_pushliteral(J, "#document-fragment"); return;
    default: js_pushliteral(J, "html"); return;
    }
}
static void Node_localName(js_State* J) { BwNode* n = self(J); if (n->type == BWN_ELEMENT) js_pushstring(J, n->name); else js_pushnull(J); }
static void Node_parentNode(js_State* J) { bwdj_push_node(J, self(J)->parent); }
static void Node_parentElement(js_State* J) { BwNode* p = self(J)->parent; bwdj_push_node(J, p && p->type == BWN_ELEMENT ? p : 0); }
static void Node_firstChild(js_State* J) { bwdj_push_node(J, self(J)->first); }
static void Node_lastChild(js_State* J) { bwdj_push_node(J, self(J)->last); }
static void Node_nextSibling(js_State* J) { bwdj_push_node(J, self(J)->next); }
static void Node_previousSibling(js_State* J) { bwdj_push_node(J, self(J)->prev); }
static void Node_ownerDocument(js_State* J) { BwNode* n = self(J); bwdj_push_node(J, n->type == BWN_DOCUMENT ? 0 : DOM(J)->tree->doc); }
static void Node_isConnected(js_State* J)
{
    BwNode* n = self(J);
    while (n->parent) n = n->parent;
    js_pushboolean(J, n->type == BWN_DOCUMENT);
}
static void Node_childNodes(js_State* J)
{
    BwNode* k;
    int i = 0;
    js_newarray(J);
    for (k = self(J)->first; k; k = k->next) { bwdj_push_node(J, k); js_setindex(J, -2, i++); }
}
static void Node_hasChildNodes(js_State* J) { js_pushboolean(J, self(J)->first != 0); }

static void Node_textContent(js_State* J)
{
    BwNode* n = self(J);
    BwBuf b;
    if (n->type == BWN_DOCUMENT) { js_pushnull(J); return; }
    if (n->type == BWN_COMMENT || n->type == BWN_DOCTYPE) { js_pushstring(J, n->text ? n->text : ""); return; }
    memset(&b, 0, sizeof b);
    bwd_text(n, &b);
    push_buf(J, &b);
}
static void Node_set_textContent(js_State* J)
{
    BwNode* n = self(J);
    const char* s = js_isnull(J, 1) || js_isundefined(J, 1) ? "" : js_tostring(J, 1);
    if (n->type == BWN_TEXT) {
        char* e = escaped(s, 0);
        if (!e) js_error(J, "out of memory");
        free(n->text); n->text = e;
    } else if (n->type == BWN_COMMENT) {
        if (bwd_set_text(n, s, (unsigned int)strlen(s))) js_error(J, "out of memory");
    } else if (n->type == BWN_ELEMENT || n->type == BWN_FRAGMENT) {
        remove_children(n);
        if (*s) bwd_insert_before(n, new_text(J, s, (n->flags & BWF_RAW) != 0), 0);
    }
    DOM(J)->dirty = 1;
    js_pushundefined(J);
}
static void Node_nodeValue(js_State* J)
{
    BwNode* n = self(J);
    if (n->type == BWN_TEXT) {
        BwBuf b;
        memset(&b, 0, sizeof b);
        bwd_text(n, &b);
        push_buf(J, &b);
    } else if (n->type == BWN_COMMENT) js_pushstring(J, n->text ? n->text : "");
    else js_pushnull(J);
}
static void Node_set_nodeValue(js_State* J)
{
    BwNode* n = self(J);
    if (n->type == BWN_TEXT || n->type == BWN_COMMENT) Node_set_textContent(J);
    else js_pushundefined(J);
}

static void insert(js_State* J, BwNode* parent, BwNode* child, BwNode* ref)
{
    if (bwd_insert_before(parent, child, ref)) js_error(J, "HierarchyRequestError: the node cannot be inserted there");
    DOM(J)->dirty = 1;
}
static void Node_appendChild(js_State* J) { BwNode* c = node_at(J, 1); insert(J, self(J), c, 0); js_copy(J, 1); }
static void Node_insertBefore(js_State* J)
{
    BwNode* p = self(J), *c = node_at(J, 1), *ref = opt_node(J, 2);
    if (ref && ref->parent != p) js_error(J, "NotFoundError: the reference is not a child of this node");
    insert(J, p, c, ref);
    js_copy(J, 1);
}
static void Node_removeChild(js_State* J)
{
    BwNode* p = self(J), *c = node_at(J, 1);
    if (c->parent != p) js_error(J, "NotFoundError: not a child of this node");
    bwd_remove(c);
    DOM(J)->dirty = 1;
    js_copy(J, 1);
}
static void Node_replaceChild(js_State* J)
{
    BwNode* p = self(J), *nw = node_at(J, 1), *old = node_at(J, 2);
    if (old->parent != p) js_error(J, "NotFoundError: not a child of this node");
    if (nw != old) { insert(J, p, nw, old); bwd_remove(old); }
    js_copy(J, 2);
}
static void Node_remove(js_State* J) { BwNode* n = self(J); if (n->parent) { bwd_remove(n); DOM(J)->dirty = 1; } js_pushundefined(J); }
static void Node_cloneNode(js_State* J)
{
    BwNode* c = bwd_clone(DOM(J)->tree, self(J), js_toboolean(J, 1));
    if (!c) js_error(J, "out of memory");
    bwdj_push_node(J, c);
}
static void Node_contains(js_State* J)
{
    BwNode* n = self(J), *o = opt_node(J, 1);
    for (; o; o = o->parent) if (o == n) { js_pushboolean(J, 1); return; }
    js_pushboolean(J, 0);
}
static void Node_noop(js_State* J) { js_pushundefined(J); }

/* ------------------------------------------------------------------ Element */

static void El_tagName(js_State* J) { Node_nodeName(J); }
static void El_children(js_State* J)
{
    BwNode* k;
    int i = 0;
    js_newarray(J);
    for (k = self(J)->first; k; k = k->next) if (k->type == BWN_ELEMENT) { bwdj_push_node(J, k); js_setindex(J, -2, i++); }
}
static void El_childElementCount(js_State* J)
{
    BwNode* k;
    int i = 0;
    for (k = self(J)->first; k; k = k->next) if (k->type == BWN_ELEMENT) i++;
    js_pushnumber(J, i);
}
static BwNode* el_step(BwNode* n, int fwd) { for (n = fwd ? n->next : n->prev; n; n = fwd ? n->next : n->prev) if (n->type == BWN_ELEMENT) return n; return 0; }
static void El_firstElementChild(js_State* J) { BwNode* k; for (k = self(J)->first; k && k->type != BWN_ELEMENT; k = k->next) {} bwdj_push_node(J, k); }
static void El_lastElementChild(js_State* J) { BwNode* k; for (k = self(J)->last; k && k->type != BWN_ELEMENT; k = k->prev) {} bwdj_push_node(J, k); }
static void El_nextElementSibling(js_State* J) { bwdj_push_node(J, el_step(self(J), 1)); }
static void El_previousElementSibling(js_State* J) { bwdj_push_node(J, el_step(self(J), 0)); }

static void El_innerHTML(js_State* J)
{
    BwBuf b;
    memset(&b, 0, sizeof b);
    bwd_serialize(self(J), 0, &b);
    push_buf(J, &b);
}
static void set_html(js_State* J, BwNode* n, const char* s)
{
    BwJsDom* d = DOM(J);
    if (n->flags & BWF_RAW) {
        remove_children(n);
        if (*s) bwd_insert_before(n, new_text(J, s, 1), 0);
    } else {
        BwNode* f = bwd_parse_fragment(d->tree, s, (unsigned int)strlen(s), 1);
        if (!f || d->tree->oom) js_error(J, "out of memory");
        remove_children(n);
        bwd_insert_before(n, f, 0);
    }
    d->dirty = 1;
}
static void El_set_innerHTML(js_State* J)
{
    set_html(J, self(J), js_isnull(J, 1) || js_isundefined(J, 1) ? "" : js_tostring(J, 1));
    js_pushundefined(J);
}
static void El_outerHTML(js_State* J)
{
    BwBuf b;
    memset(&b, 0, sizeof b);
    bwd_serialize(self(J), 1, &b);
    push_buf(J, &b);
}
static void El_set_outerHTML(js_State* J)
{
    BwNode* n = self(J);
    BwJsDom* d = DOM(J);
    const char* s = js_tostring(J, 1);
    BwNode* f;
    if (!n->parent) { js_pushundefined(J); return; }
    f = bwd_parse_fragment(d->tree, s, (unsigned int)strlen(s), 1);
    if (!f) js_error(J, "out of memory");
    bwd_insert_before(n->parent, f, n);
    bwd_remove(n);
    d->dirty = 1;
    js_pushundefined(J);
}

static void El_getAttribute(js_State* J)
{
    char nm[64];
    const char* v;
    BwBuf b;
    lower_copy(js_tostring(J, 1), nm, sizeof nm);
    v = bwd_attr(self(J), nm);
    if (!v) { js_pushnull(J); return; }
    memset(&b, 0, sizeof b);
    bwd_unescape(v, (unsigned int)strlen(v), &b);
    push_buf(J, &b);
}
static void El_setAttribute(js_State* J)
{
    char nm[64];
    char* v;
    BwNode* n = self(J);
    lower_copy(js_tostring(J, 1), nm, sizeof nm);
    if (!nm[0]) js_error(J, "InvalidCharacterError: an empty attribute name");
    v = escaped(js_tostring(J, 2), 1);
    if (!v || bwd_set_attr(n, nm, v)) { free(v); js_error(J, "out of memory"); }
    free(v);
    DOM(J)->dirty = 1;
    js_pushundefined(J);
}
static void El_removeAttribute(js_State* J)
{
    char nm[64];
    lower_copy(js_tostring(J, 1), nm, sizeof nm);
    bwd_remove_attr(self(J), nm);
    DOM(J)->dirty = 1;
    js_pushundefined(J);
}
static void El_hasAttribute(js_State* J)
{
    char nm[64];
    lower_copy(js_tostring(J, 1), nm, sizeof nm);
    js_pushboolean(J, bwd_attr(self(J), nm) != 0);
}
static void El_getAttributeNames(js_State* J)
{
    BwAttr* a;
    int i = 0;
    js_newarray(J);
    for (a = self(J)->attrs; a; a = a->next) { js_pushstring(J, a->name); js_setindex(J, -2, i++); }
}
static void El_attributes(js_State* J)
{
    BwAttr* a;
    int i = 0;
    js_newarray(J);
    for (a = self(J)->attrs; a; a = a->next) {
        BwBuf b;
        memset(&b, 0, sizeof b);
        if (a->value) bwd_unescape(a->value, (unsigned int)strlen(a->value), &b);
        js_newobject(J);
        js_pushstring(J, a->name); js_setproperty(J, -2, "name");
        js_pushstring(J, a->name); js_setproperty(J, -2, "nodeName");
        js_pushstring(J, b.p ? b.p : ""); js_setproperty(J, -2, "value");
        js_pushboolean(J, 1); js_setproperty(J, -2, "specified");
        free(b.p);
        js_copy(J, -1);
        js_setproperty(J, -3, a->name);         /* attributes["id"] too, as a NamedNodeMap */
        js_setindex(J, -2, i++);
    }
}

typedef struct { js_State* J; int i; int one; BwNode* found; } Collect;
static int collect_each(BwNode* n, void* ctx)
{
    Collect* c = (Collect*)ctx;
    if (c->one) { c->found = n; return 1; }
    bwdj_push_node(c->J, n);
    js_setindex(c->J, -2, c->i++);
    return 0;
}

static void get_by_tag(js_State* J)
{
    BwNode* root = self(J), *n = root;
    char nm[64];
    int i = 0, all;
    lower_copy(js_tostring(J, 1), nm, sizeof nm);
    all = nm[0] == '*' && !nm[1];
    js_newarray(J);
    while ((n = bwd_next(root, n, 1)) != 0)
        if (n->type == BWN_ELEMENT && (all || strcmp(n->name, nm) == 0)) { bwdj_push_node(J, n); js_setindex(J, -2, i++); }
}
static void get_by_class(js_State* J)
{
    BwNode* root = self(J), *n = root;
    const char* want = js_tostring(J, 1);
    int i = 0;
    js_newarray(J);
    while ((n = bwd_next(root, n, 1)) != 0) {
        const char* s = want;
        int ok = n->type == BWN_ELEMENT, any = 0;
        while (ok && *s) {
            const char* b;
            while (*s == ' ' || *s == '\t' || *s == '\n') s++;
            b = s;
            while (*s && *s != ' ' && *s != '\t' && *s != '\n') s++;
            if (s > b) { any = 1; if (!bwd_has_class(n, b, (unsigned int)(s - b))) ok = 0; }
        }
        if (ok && any) { bwdj_push_node(J, n); js_setindex(J, -2, i++); }
    }
}
static void query_one(js_State* J)
{
    Collect c;
    c.J = J; c.i = 0; c.one = 1; c.found = 0;
    if (bwd_select(self(J), js_tostring(J, 1), collect_each, &c) < 0) js_error(J, "SyntaxError: '%s' is not a selector this browser reads", js_tostring(J, 1));
    bwdj_push_node(J, c.found);
}
static void query_all(js_State* J)
{
    Collect c;
    const char* sel = js_tostring(J, 1);
    BwNode* root = self(J);
    c.J = J; c.i = 0; c.one = 0; c.found = 0;
    js_newarray(J);
    if (bwd_select(root, sel, collect_each, &c) < 0) js_error(J, "SyntaxError: '%s' is not a selector this browser reads", sel);
}
static void El_matches(js_State* J)
{
    int r = bwd_matches(self(J), js_tostring(J, 1));
    if (r < 0) js_error(J, "SyntaxError: '%s' is not a selector this browser reads", js_tostring(J, 1));
    js_pushboolean(J, r);
}
static void El_closest(js_State* J)
{
    BwNode* n;
    for (n = self(J); n && n->type == BWN_ELEMENT; n = n->parent) {
        int r = bwd_matches(n, js_tostring(J, 1));
        if (r < 0) js_error(J, "SyntaxError: '%s' is not a selector this browser reads", js_tostring(J, 1));
        if (r) { bwdj_push_node(J, n); return; }
    }
    js_pushnull(J);
}
static void El_insertAdjacentHTML(js_State* J)
{
    BwNode* n = self(J), *f;
    BwJsDom* d = DOM(J);
    char pos[16];
    const char* s = js_tostring(J, 2);
    lower_copy(js_tostring(J, 1), pos, sizeof pos);
    f = bwd_parse_fragment(d->tree, s, (unsigned int)strlen(s), 1);
    if (!f) js_error(J, "out of memory");
    if (!strcmp(pos, "beforebegin")) { if (n->parent) bwd_insert_before(n->parent, f, n); }
    else if (!strcmp(pos, "afterbegin")) bwd_insert_before(n, f, n->first);
    else if (!strcmp(pos, "beforeend")) bwd_insert_before(n, f, 0);
    else if (!strcmp(pos, "afterend")) { if (n->parent) bwd_insert_before(n->parent, f, n->next); }
    else js_error(J, "SyntaxError: insertAdjacentHTML: '%s'", pos);
    d->dirty = 1;
    js_pushundefined(J);
}

/* ------------------------------------------------------------------ Document */

static BwNode* doc_html(BwJsDom* d) { return bwd_child_element(d->tree->doc, "html"); }
static void Doc_documentElement(js_State* J) { bwdj_push_node(J, doc_html(DOM(J))); }
static void Doc_head(js_State* J) { bwdj_push_node(J, bwd_child_element(doc_html(DOM(J)), "head")); }
static void Doc_body(js_State* J) { bwdj_push_node(J, bwd_child_element(doc_html(DOM(J)), "body")); }
static BwNode* find_title(BwJsDom* d)
{
    BwNode* n = d->tree->doc;
    while ((n = bwd_next(d->tree->doc, n, 1)) != 0) if (n->type == BWN_ELEMENT && !strcmp(n->name, "title")) return n;
    return 0;
}
static void Doc_title(js_State* J)
{
    BwNode* t = find_title(DOM(J));
    BwBuf b;
    if (!t) { js_pushliteral(J, ""); return; }
    memset(&b, 0, sizeof b);
    bwd_text(t, &b);
    /* (raw text: its entities as the browser shows them) */
    {
        BwBuf u;
        memset(&u, 0, sizeof u);
        if (b.p) bwd_unescape(b.p, b.n, &u);
        free(b.p);
        push_buf(J, &u);
    }
}
static void Doc_set_title(js_State* J)
{
    BwJsDom* d = DOM(J);
    BwNode* t = find_title(d);
    const char* s = js_tostring(J, 1);
    char* e = escaped(s, 0);
    if (!e) js_error(J, "out of memory");
    if (!t) {
        BwNode* head = bwd_child_element(doc_html(d), "head");
        t = bwd_node(d->tree, BWN_ELEMENT, "title", 5);
        if (!t || !head) { free(e); js_error(J, "out of memory"); }
        bwd_insert_before(head, t, 0);
    }
    remove_children(t);
    {
        BwNode* x = bwd_node(d->tree, BWN_TEXT, 0, 0);
        if (!x) { free(e); js_error(J, "out of memory"); }
        free(x->text);
        x->text = e;
        bwd_insert_before(t, x, 0);
    }
    d->dirty = 1;
    js_pushundefined(J);
}
static void Doc_getElementById(js_State* J) { bwdj_push_node(J, bwd_find_id(self(J), js_tostring(J, 1))); }
static void Doc_createElement(js_State* J)
{
    const char* nm = js_tostring(J, js_isstring(J, 2) ? 2 : 1);   /* (createElementNS: the 2nd) */
    BwNode* n;
    unsigned int k, l = (unsigned int)strlen(nm);
    if (!l || l > 40) js_error(J, "InvalidCharacterError: '%s' is not an element name", nm);
    for (k = 0; k < l; k++) if (!((nm[k] >= 'a' && nm[k] <= 'z') || (nm[k] >= 'A' && nm[k] <= 'Z') || (nm[k] >= '0' && nm[k] <= '9') || nm[k] == '-' || nm[k] == ':' || nm[k] == '_'))
        js_error(J, "InvalidCharacterError: '%s' is not an element name", nm);
    n = bwd_node(DOM(J)->tree, BWN_ELEMENT, nm, l);
    if (!n) js_error(J, "out of memory");
    bwdj_push_node(J, n);
}
static void Doc_createElementNS(js_State* J) { Doc_createElement(J); }
static void Doc_createTextNode(js_State* J) { bwdj_push_node(J, new_text(J, js_tostring(J, 1), 0)); }
static void Doc_createComment(js_State* J)
{
    BwNode* n = bwd_node(DOM(J)->tree, BWN_COMMENT, 0, 0);
    const char* s = js_tostring(J, 1);
    if (!n || bwd_set_text(n, s, (unsigned int)strlen(s))) js_error(J, "out of memory");
    bwdj_push_node(J, n);
}
static void Doc_createDocumentFragment(js_State* J)
{
    BwNode* n = bwd_node(DOM(J)->tree, BWN_FRAGMENT, "#document-fragment", 18);
    if (!n) js_error(J, "out of memory");
    bwdj_push_node(J, n);
}

/* ------------------------------------------------------------------ setup */

static void method(js_State* J, const char* name, js_CFunction f, int n)
{
    js_newcfunction(J, f, name, n);
    js_defproperty(J, -2, name, JS_DONTENUM);
}
static void getter(js_State* J, const char* name, js_CFunction get, js_CFunction set)
{
    js_newcfunction(J, get, name, 0);
    if (set) js_newcfunction(J, set, name, 1); else js_pushundefined(J);
    js_defaccessor(J, -3, name, JS_DONTENUM);
}

static void query_methods(js_State* J)
{
    method(J, "getElementsByTagName", get_by_tag, 1);
    method(J, "getElementsByClassName", get_by_class, 1);
    method(J, "querySelector", query_one, 1);
    method(J, "querySelectorAll", query_all, 1);
}

void bwdj_setup(js_State* J, BwJsDom* d)
{
    (void)d;
    /* Node */
    js_newobject(J);
    getter(J, "nodeType", Node_nodeType, 0);
    getter(J, "nodeName", Node_nodeName, 0);
    getter(J, "parentNode", Node_parentNode, 0);
    getter(J, "parentElement", Node_parentElement, 0);
    getter(J, "firstChild", Node_firstChild, 0);
    getter(J, "lastChild", Node_lastChild, 0);
    getter(J, "nextSibling", Node_nextSibling, 0);
    getter(J, "previousSibling", Node_previousSibling, 0);
    getter(J, "childNodes", Node_childNodes, 0);
    getter(J, "ownerDocument", Node_ownerDocument, 0);
    getter(J, "isConnected", Node_isConnected, 0);
    getter(J, "textContent", Node_textContent, Node_set_textContent);
    getter(J, "nodeValue", Node_nodeValue, Node_set_nodeValue);
    getter(J, "data", Node_nodeValue, Node_set_nodeValue);
    method(J, "appendChild", Node_appendChild, 1);
    method(J, "insertBefore", Node_insertBefore, 2);
    method(J, "removeChild", Node_removeChild, 1);
    method(J, "replaceChild", Node_replaceChild, 2);
    method(J, "cloneNode", Node_cloneNode, 1);
    method(J, "contains", Node_contains, 1);
    method(J, "hasChildNodes", Node_hasChildNodes, 0);
    method(J, "remove", Node_remove, 0);
    method(J, "normalize", Node_noop, 0);
    js_copy(J, -1);
    js_setregistry(J, "pNode");
    js_setglobal(J, "__pNode");

    /* Element */
    js_getregistry(J, "pNode");
    js_newobjectx(J);
    getter(J, "tagName", El_tagName, 0);
    getter(J, "localName", Node_localName, 0);
    getter(J, "children", El_children, 0);
    getter(J, "childElementCount", El_childElementCount, 0);
    getter(J, "firstElementChild", El_firstElementChild, 0);
    getter(J, "lastElementChild", El_lastElementChild, 0);
    getter(J, "nextElementSibling", El_nextElementSibling, 0);
    getter(J, "previousElementSibling", El_previousElementSibling, 0);
    getter(J, "innerHTML", El_innerHTML, El_set_innerHTML);
    getter(J, "outerHTML", El_outerHTML, El_set_outerHTML);
    getter(J, "attributes", El_attributes, 0);
    method(J, "getAttribute", El_getAttribute, 1);
    method(J, "setAttribute", El_setAttribute, 2);
    method(J, "removeAttribute", El_removeAttribute, 1);
    method(J, "hasAttribute", El_hasAttribute, 1);
    method(J, "getAttributeNames", El_getAttributeNames, 0);
    method(J, "matches", El_matches, 1);
    method(J, "webkitMatchesSelector", El_matches, 1);
    method(J, "msMatchesSelector", El_matches, 1);
    method(J, "closest", El_closest, 1);
    method(J, "insertAdjacentHTML", El_insertAdjacentHTML, 2);
    query_methods(J);
    js_copy(J, -1);
    js_setregistry(J, "pEl");
    js_setglobal(J, "__pEl");

    /* Text, Comment */
    js_getregistry(J, "pNode");
    js_newobjectx(J);
    js_copy(J, -1);
    js_setregistry(J, "pText");
    js_setglobal(J, "__pText");
    js_getregistry(J, "pNode");
    js_newobjectx(J);
    js_copy(J, -1);
    js_setregistry(J, "pComment");
    js_setglobal(J, "__pComment");

    /* DocumentFragment */
    js_getregistry(J, "pNode");
    js_newobjectx(J);
    getter(J, "children", El_children, 0);
    getter(J, "firstElementChild", El_firstElementChild, 0);
    getter(J, "childElementCount", El_childElementCount, 0);
    method(J, "getElementById", Doc_getElementById, 1);
    query_methods(J);
    js_copy(J, -1);
    js_setregistry(J, "pFrag");
    js_setglobal(J, "__pFrag");

    /* Document */
    js_getregistry(J, "pNode");
    js_newobjectx(J);
    getter(J, "documentElement", Doc_documentElement, 0);
    getter(J, "head", Doc_head, 0);
    getter(J, "body", Doc_body, 0);
    getter(J, "title", Doc_title, Doc_set_title);
    getter(J, "children", El_children, 0);
    method(J, "getElementById", Doc_getElementById, 1);
    method(J, "createElement", Doc_createElement, 1);
    method(J, "createElementNS", Doc_createElementNS, 2);
    method(J, "createTextNode", Doc_createTextNode, 1);
    method(J, "createComment", Doc_createComment, 1);
    method(J, "createDocumentFragment", Doc_createDocumentFragment, 0);
    query_methods(J);
    js_copy(J, -1);
    js_setregistry(J, "pDoc");
    js_setglobal(J, "__pDoc");

    bwdj_push_node(J, d->tree->doc);
    js_setglobal(J, "__doc");
}
