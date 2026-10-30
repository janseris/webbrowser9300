/* Browser 9300 - HTML tokenizer + tree builder + a little CSS, producing the doc's op stream.
 *
 * Tolerant of bad HTML the way browsers are where it matters for reading: implied </p>, </li>,
 * </td>..., stray end tags ignored, raw text in script/style/textarea/title. No DOM is kept: an
 * open-element stack carries the computed style, and text / blocks / images / controls go out
 * as ops in document order. C89, no static data. */

#include "bwcore.h"
#include "bwentity.h"

/* ------------------------------------------------------------------ tags */
enum {
    T_UNKNOWN = 0,
    T_A, T_ABBR, T_ADDRESS, T_AREA, T_ARTICLE, T_ASIDE, T_AUDIO, T_B, T_BASE, T_BIG, T_BLOCKQUOTE,
    T_BODY, T_BR, T_BUTTON, T_CANVAS, T_CAPTION, T_CENTER, T_CITE, T_CODE, T_COL, T_COLGROUP,
    T_DATALIST, T_DD, T_DEL, T_DETAILS, T_DFN, T_DIALOG, T_DIR, T_DIV, T_DL, T_DT, T_EM, T_EMBED,
    T_FIELDSET, T_FIGCAPTION, T_FIGURE, T_FONT, T_FOOTER, T_FORM, T_FRAME, T_FRAMESET, T_H1, T_H2,
    T_H3, T_H4, T_H5, T_H6, T_HEAD, T_HEADER, T_HGROUP, T_HR, T_HTML, T_I, T_IFRAME, T_IMG, T_INPUT,
    T_INS, T_KBD, T_LABEL, T_LEGEND, T_LI, T_LINK, T_LISTING, T_MAIN, T_MAP, T_MARK, T_MATH, T_MENU,
    T_META, T_NAV, T_NOBR, T_NOEMBED, T_NOFRAMES, T_NOSCRIPT, T_OBJECT, T_OL, T_OPTGROUP, T_OPTION,
    T_P, T_PARAM, T_PICTURE, T_PLAINTEXT, T_PRE, T_Q, T_S, T_SAMP, T_SCRIPT, T_SECTION, T_SELECT,
    T_SMALL, T_SOURCE, T_SPAN, T_STRIKE, T_STRONG, T_STYLE, T_SUB, T_SUMMARY, T_SUP, T_SVG, T_TABLE,
    T_TBODY, T_TD, T_TEMPLATE, T_TEXTAREA, T_TFOOT, T_TH, T_THEAD, T_TITLE, T_TR, T_TRACK, T_TT, T_U,
    T_UL, T_VAR, T_VIDEO, T_WBR, T_XMP,
    T_COUNT
};

/* the names, in the enum's order (index = tag id) */
static const char* const kTagNames[T_COUNT] = {
    "",
    "a", "abbr", "address", "area", "article", "aside", "audio", "b", "base", "big", "blockquote",
    "body", "br", "button", "canvas", "caption", "center", "cite", "code", "col", "colgroup",
    "datalist", "dd", "del", "details", "dfn", "dialog", "dir", "div", "dl", "dt", "em", "embed",
    "fieldset", "figcaption", "figure", "font", "footer", "form", "frame", "frameset", "h1", "h2",
    "h3", "h4", "h5", "h6", "head", "header", "hgroup", "hr", "html", "i", "iframe", "img", "input",
    "ins", "kbd", "label", "legend", "li", "link", "listing", "main", "map", "mark", "math", "menu",
    "meta", "nav", "nobr", "noembed", "noframes", "noscript", "object", "ol", "optgroup", "option",
    "p", "param", "picture", "plaintext", "pre", "q", "s", "samp", "script", "section", "select",
    "small", "source", "span", "strike", "strong", "style", "sub", "summary", "sup", "svg", "table",
    "tbody", "td", "template", "textarea", "tfoot", "th", "thead", "title", "tr", "track", "tt", "u",
    "ul", "var", "video", "wbr", "xmp"
};

enum { D_INLINE = 0, D_BLOCK, D_LISTITEM, D_NONE, D_CELL, D_ROW };

#define MAX_DEPTH 160
#define MAX_ATTRS 40
#define DEVICE_WIDTH 640

typedef struct {
    unsigned char tag;
    unsigned char display;
    unsigned char font;
    unsigned char flags;          /* BW_OPF_* */
    unsigned char align;
    unsigned char hidden;
    unsigned char blockOpen;      /* a BLOCK_START went out for it */
    unsigned char listKind;       /* this element is a list: 1 ul, 2 ol */
    int color;
    int bg;
    int target;
    int listCounter;              /* ol: the next number */
    int listDepth;                /* how many lists are open around (for the bullet) */
    int marginBottom;
} Elem;

/* ---- CSS */
#define CSS_DISPLAY   0x001
#define CSS_COLOR     0x002
#define CSS_BG        0x004
#define CSS_BOLD      0x008
#define CSS_ITALIC    0x010
#define CSS_ALIGN     0x020
#define CSS_UNDERLINE 0x040
#define CSS_SIZE      0x080
#define CSS_HIDDEN    0x100
#define CSS_PRE       0x200
#define CSS_MONO      0x400
#define CSS_STRIKE    0x800

typedef struct {
    unsigned int mask;
    unsigned char display, bold, italic, align, underline, size, hidden, pre, mono, strike;
    int color, bg;
} CssProps;

typedef struct {
    int tag;                       /* 0 any */
    char* id;                      /* NULL any */
    char* classes;                 /* NUL-separated, ends with an empty one; NULL none */
    int spec;                      /* 0 tag, 1 class, 2 id */
    unsigned int bloom;            /* bits of the tag / id / classes it needs (a quick reject) */
    CssProps props;
} CssRule;

typedef struct {
    const char* name; unsigned int nameLen;
    unsigned int valOff;           /* into P.attrBuf (UTF-8, NUL-terminated) */
    int hasValue;
} Attr;

typedef struct {
    BwDoc* doc;
    const char* s;
    unsigned int n;
    unsigned int pos;
    int cs;
    Elem stack[MAX_DEPTH];
    int depth;
    int lastSpace;
    /* attributes of the tag being handled */
    Attr attrs[MAX_ATTRS];
    int attrCount;
    IgBuf attrBuf;
    char tagName[16];
    /* CSS */
    CssRule* rules;
    unsigned int ruleCount, ruleCap;
    /* forms */
    int curForm;
    int curSelect;                 /* control being filled with options, -1 */
    int capture;                   /* 0 none, 1 button label, 2 option label */
    int captureCtl;
    IgBuf cap16;                   /* captured UTF-16 text */
    unsigned short* optValue;      /* the <option value=...> waiting for its label */
    unsigned int optValueLen;
    int optSelected;
    int seenBody;
} P;

/* ------------------------------------------------------------------ small helpers */
static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int is_ws(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
static int is_alnum(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

static int lookup_tag(const char* name, unsigned int n)
{
    /* the table is sorted by name: binary search */
    int lo = 1, hi = T_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const char* t = kTagNames[mid];
        unsigned int i = 0;
        int c = 0;
        while (i < n && t[i] && t[i] == name[i]) i++;
        if (i == n && !t[i]) return mid;
        if (i == n) c = -1;
        else if (!t[i]) c = 1;
        else c = (unsigned char)name[i] - (unsigned char)t[i];
        if (c < 0) hi = mid - 1; else lo = mid + 1;
    }
    return T_UNKNOWN;
}

/* ---- growable arrays of the doc */
static int grow(void** arr, unsigned int* cap, unsigned int count, unsigned int size)
{
    void* na;
    unsigned int nc;
    if (count < *cap) return 1;
    nc = *cap ? *cap * 2 : 16;
    na = ig_malloc(nc * size);
    if (!na) return 0;
    if (*arr) { ig_memcpy(na, *arr, count * size); ig_free(*arr); }
    *arr = na;
    *cap = nc;
    return 1;
}

static int pool_put(BwDoc* d, unsigned short c)
{
    if (d->poolLen >= d->poolCap) {
        unsigned int nc = d->poolCap ? d->poolCap * 2 : 4096;
        unsigned short* np = (unsigned short*)ig_malloc(nc * 2);
        if (!np) { d->failed = 1; return 0; }
        if (d->pool) { ig_memcpy(np, d->pool, d->poolLen * 2); ig_free(d->pool); }
        d->pool = np;
        d->poolCap = nc;
    }
    d->pool[d->poolLen++] = c;
    return 1;
}

static BwOp* add_op(P* p, int type)
{
    BwDoc* d = p->doc;
    BwOp* op;
    if (!grow((void**)&d->ops, &d->opCap, d->opCount, sizeof(BwOp))) { d->failed = 1; return 0; }
    op = &d->ops[d->opCount++];
    ig_memset(op, 0, sizeof(BwOp));
    op->type = (unsigned char)type;
    op->target = -1;
    op->ref = -1;
    op->color = 0;
    return op;
}

static int add_target(P* p, int kind, int index)
{
    BwDoc* d = p->doc;
    if (!grow((void**)&d->targets, &d->targetCap, d->targetCount, sizeof(BwTarget))) { d->failed = 1; return -1; }
    d->targets[d->targetCount].kind = kind;
    d->targets[d->targetCount].index = index;
    return (int)d->targetCount++;
}

static unsigned short* dup16(const unsigned short* s, unsigned int n)
{
    unsigned short* r = (unsigned short*)ig_malloc((n + 1) * 2);
    if (!r) return 0;
    if (n) ig_memcpy(r, s, n * 2);
    r[n] = 0;
    return r;
}

/* UTF-8 C string -> new UTF-16 */
static unsigned short* utf8_to_16(const char* s, unsigned int* outLen)
{
    unsigned int n = ig_strlen(s);
    unsigned int m = ig_utf8_to_utf16(s, n, 0, 0);
    unsigned short* r = (unsigned short*)ig_malloc((m + 1) * 2);
    if (!r) { *outLen = 0; return 0; }
    ig_utf8_to_utf16(s, n, r, m);
    r[m] = 0;
    *outLen = m;
    return r;
}

/* ------------------------------------------------------------------ characters */

/* An entity at s[*i] ('&'); returns the character and moves *i past it, or 0 (not an entity). */
static unsigned int read_entity(const char* s, unsigned int n, unsigned int* i, int inAttr)
{
    unsigned int j = *i + 1;
    if (j >= n) return 0;
    if (s[j] == '#') {
        unsigned int v = 0, digits = 0;
        int hex = 0;
        j++;
        if (j < n && (s[j] == 'x' || s[j] == 'X')) { hex = 1; j++; }
        while (j < n && digits < 8) {
            int c = (unsigned char)s[j], d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (hex && c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (hex && c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else break;
            v = v * (hex ? 16 : 10) + (unsigned int)d;
            digits++;
            j++;
        }
        if (!digits) return 0;
        while (j < n && ((s[j] >= '0' && s[j] <= '9') || (hex && is_alnum((unsigned char)s[j])))) j++;   /* too long */
        if (j < n && s[j] == ';') j++;
        *i = j;
        if (v >= 0x80 && v < 0xA0) return bw_charset_char(BW_CS_1252, (unsigned char)v);
        if (v == 0 || v > 0xFFFF || (v >= 0xD800 && v < 0xE000)) return 0xFFFD;
        return v;
    } else {
        unsigned int st = j, len;
        int lo = 0, hi = BW_ENTITY_COUNT - 1;
        while (j < n && j - st < 12 && is_alnum((unsigned char)s[j])) j++;
        len = j - st;
        if (!len) return 0;
        /* binary search for the longest match of exactly len */
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            const char* t = kEntities[mid].name;
            unsigned int k = 0;
            int c;
            while (k < len && t[k] && t[k] == s[st + k]) k++;
            if (k == len && !t[k]) {
                int hasSemi = j < n && s[j] == ';';
                if (!hasSemi) {
                    /* without ';' only the old ones, and not in "a?x=1&copy=2" */
                    unsigned short code = kEntities[mid].code;
                    if (!(code == '&' || code == '<' || code == '>' || code == '"' || code == 0xA0 || code == 0xA9))
                        return 0;
                    if (inAttr && j < n && (s[j] == '=' || is_alnum((unsigned char)s[j]))) return 0;
                }
                *i = hasSemi ? j + 1 : j;
                return kEntities[mid].code;
            }
            if (k == len) c = -1;
            else if (!t[k]) c = 1;
            else c = (unsigned char)s[st + k] - (unsigned char)t[k];
            if (c < 0) hi = mid - 1; else lo = mid + 1;
        }
        return 0;
    }
}

/* the next character of s at *i in charset cs (entities decoded if asked) */
static unsigned int read_char(int cs, const char* s, unsigned int n, unsigned int* i, int entities, int inAttr)
{
    unsigned char c = (unsigned char)s[*i];
    if (c == '&' && entities) {
        unsigned int e = read_entity(s, n, i, inAttr);
        if (e) return e;
        (*i)++;
        return '&';
    }
    if (c < 0x80) { (*i)++; return c ? c : 0xFFFD; }
    if (cs != BW_CS_UTF8) { (*i)++; return bw_charset_char(cs, c); }
    {
        unsigned int need, cp, k;
        if ((c & 0xE0) == 0xC0) { need = 1; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { need = 2; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { need = 3; cp = c & 0x07; }
        else { (*i)++; return 0xFFFD; }
        if (*i + need >= n) { (*i)++; return 0xFFFD; }
        for (k = 1; k <= need; k++) {
            unsigned char cc = (unsigned char)s[*i + k];
            if ((cc & 0xC0) != 0x80) { (*i)++; return 0xFFFD; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        *i += need + 1;
        if (cp > 0xFFFF || (cp >= 0xD800 && cp < 0xE000)) return 0xFFFD;
        return cp;
    }
}

/* ------------------------------------------------------------------ text output */

static Elem* top(P* p) { return &p->stack[p->depth - 1]; }

static int capturing(P* p) { return p->capture != 0; }

static void put_text_char(P* p, unsigned int c, const Elem* e)
{
    BwDoc* d = p->doc;
    BwOp* last;
    if (capturing(p)) {
        unsigned short u = (unsigned short)c;
        if (is_ws((int)c)) { if (p->lastSpace) return; u = ' '; p->lastSpace = 1; }
        else p->lastSpace = 0;
        ig_buf_append(&p->cap16, &u, 2);
        return;
    }
    if (e->hidden) return;
    if (!(e->flags & BW_OPF_PRE)) {
        if (is_ws((int)c)) {
            if (p->lastSpace) return;
            c = ' ';
            p->lastSpace = 1;
        } else p->lastSpace = 0;
    } else {
        if (c == '\r') return;
        p->lastSpace = 0;
    }
    last = d->opCount ? &d->ops[d->opCount - 1] : 0;
    if (!(last && last->type == BW_OP_TEXT && last->font == e->font && last->color == e->color &&
          last->flags == e->flags && last->target == e->target && last->off + last->len == d->poolLen)) {
        if (c == ' ' && !(e->flags & BW_OPF_PRE) && last && last->type != BW_OP_TEXT &&
            last->type != BW_OP_IMAGE && last->type != BW_OP_CONTROL) {
            /* a space right after a block boundary: nothing to separate */
            return;
        }
        last = add_op(p, BW_OP_TEXT);
        if (!last) return;
        last->font = e->font;
        last->color = e->color;
        last->flags = e->flags;
        last->target = e->target;
        last->off = d->poolLen;
        last->len = 0;
    }
    if (c == '\t' && (e->flags & BW_OPF_PRE)) {
        int k;
        for (k = 0; k < 4; k++) if (pool_put(d, ' ')) last->len++;
        return;
    }
    if (pool_put(d, (unsigned short)c)) last->len++;
}

/* text bytes s[0..n) (raw page bytes) */
static void put_text(P* p, const char* s, unsigned int n)
{
    unsigned int i = 0;
    Elem* e = top(p);
    while (i < n) {
        unsigned int c = read_char(p->cs, s, n, &i, 1, 0);
        put_text_char(p, c, e);
    }
}

/* ------------------------------------------------------------------ colors, CSS values */

typedef struct { const char* name; int rgb; } NamedColor;
static const NamedColor kColors[] = {
    { "aqua", 0x00FFFF }, { "black", 0x000000 }, { "blue", 0x0000FF }, { "brown", 0xA52A2A },
    { "crimson", 0xDC143C }, { "cyan", 0x00FFFF }, { "darkblue", 0x00008B }, { "darkgray", 0xA9A9A9 },
    { "darkgreen", 0x006400 }, { "darkgrey", 0xA9A9A9 }, { "darkred", 0x8B0000 }, { "fuchsia", 0xFF00FF },
    { "gold", 0xFFD700 }, { "gray", 0x808080 }, { "green", 0x008000 }, { "grey", 0x808080 },
    { "lightgray", 0xD3D3D3 }, { "lightgrey", 0xD3D3D3 }, { "lime", 0x00FF00 }, { "magenta", 0xFF00FF },
    { "maroon", 0x800000 }, { "navy", 0x000080 }, { "olive", 0x808000 }, { "orange", 0xFFA500 },
    { "pink", 0xFFC0CB }, { "purple", 0x800080 }, { "red", 0xFF0000 }, { "silver", 0xC0C0C0 },
    { "teal", 0x008080 }, { "white", 0xFFFFFF }, { "whitesmoke", 0xF5F5F5 }, { "yellow", 0xFFFF00 }
};

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    c = lower(c);
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int read_num(const char* s, unsigned int n, unsigned int* i)
{
    int v = 0, any = 0;
    while (*i < n && s[*i] >= '0' && s[*i] <= '9') { v = v * 10 + (s[*i] - '0'); (*i)++; any = 1; if (v > 100000) v = 100000; }
    if (*i < n && s[*i] == '.') { (*i)++; while (*i < n && s[*i] >= '0' && s[*i] <= '9') (*i)++; }
    return any ? v : -1;
}

/* -1 unknown, -2 transparent / none, else 0xRRGGBB */
static int parse_color(const char* s, unsigned int n)
{
    unsigned int i;
    while (n && is_ws((unsigned char)*s)) { s++; n--; }
    while (n && is_ws((unsigned char)s[n - 1])) n--;
    if (!n) return -1;
    if (s[0] == '#' || (n == 6 && hexval((unsigned char)s[0]) >= 0)) {
        int v[6], k, m;
        const char* h = s[0] == '#' ? s + 1 : s;
        m = (int)(s[0] == '#' ? n - 1 : n);
        if (m != 3 && m != 6 && m != 4 && m != 8) return -1;
        for (k = 0; k < m && k < 6; k++) { v[k] = hexval((unsigned char)h[k]); if (v[k] < 0) return -1; }
        if (m == 3 || m == 4) return (v[0] * 17 << 16) | (v[1] * 17 << 8) | (v[2] * 17);
        return (v[0] << 20) | (v[1] << 16) | (v[2] << 12) | (v[3] << 8) | (v[4] << 4) | v[5];
    }
    if (n > 4 && ig_strieq_n(s, "rgb", 3)) {
        int c[3], k;
        i = 3;
        if (i < n && lower((unsigned char)s[i]) == 'a') i++;
        if (i >= n || s[i] != '(') return -1;
        i++;
        for (k = 0; k < 3; k++) {
            int v;
            while (i < n && (is_ws((unsigned char)s[i]) || s[i] == ',')) i++;
            v = read_num(s, n, &i);
            if (v < 0) return -1;
            if (i < n && s[i] == '%') { v = v * 255 / 100; i++; }
            if (v > 255) v = 255;
            c[k] = v;
        }
        /* rgba(...,0) is transparent */
        while (i < n && (is_ws((unsigned char)s[i]) || s[i] == ',' || s[i] == '/')) i++;
        if (i < n && s[i] == '0' && (i + 1 >= n || s[i + 1] == ')' || is_ws((unsigned char)s[i + 1]))) return -2;
        return (c[0] << 16) | (c[1] << 8) | c[2];
    }
    if ((n == 11 && ig_strieq_n(s, "transparent", 11)) || (n == 4 && ig_strieq_n(s, "none", 4))) return -2;
    {
        int lo = 0, hi = (int)(sizeof(kColors) / sizeof(kColors[0])) - 1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            const char* t = kColors[mid].name;
            unsigned int k = 0;
            int c;
            while (k < n && t[k] && t[k] == lower((unsigned char)s[k])) k++;
            if (k == n && !t[k]) return kColors[mid].rgb;
            if (k == n) c = -1;
            else if (!t[k]) c = 1;
            else c = lower((unsigned char)s[k]) - (unsigned char)t[k];
            if (c < 0) hi = mid - 1; else lo = mid + 1;
        }
    }
    return -1;
}

static int word_is(const char* s, unsigned int n, const char* lit)
{
    unsigned int ln = ig_strlen(lit);
    return n == ln && ig_strieq_n(s, lit, n);
}

static int contains_ci(const char* s, unsigned int n, const char* lit)
{
    unsigned int ln = ig_strlen(lit), i;
    for (i = 0; i + ln <= n; i++) if (ig_strieq_n(s + i, lit, ln)) return 1;
    return 0;
}

/* a CSS font-size value -> size class (-1 unknown) */
static int css_font_size(const char* v, unsigned int n)
{
    unsigned int i = 0;
    int num;
    if (contains_ci(v, n, "xx-small") || contains_ci(v, n, "x-small") || word_is(v, n, "small") || word_is(v, n, "smaller")) return 0;
    if (word_is(v, n, "medium")) return 1;
    if (word_is(v, n, "large") || word_is(v, n, "x-large") || word_is(v, n, "larger")) return 2;
    if (contains_ci(v, n, "xx-large")) return 3;
    while (i < n && is_ws((unsigned char)v[i])) i++;
    {
        unsigned int st = i;
        int whole = read_num(v, n, &i);
        int frac = 0;
        unsigned int k;
        if (whole < 0) {
            if (i < n && v[i] == '.') whole = 0; else return -1;
        }
        /* the fraction, in hundredths */
        for (k = st; k < i; k++) if (v[k] == '.') {
            unsigned int m = k + 1;
            if (m < i) frac += (v[m] - '0') * 10;
            if (m + 1 < i) frac += v[m + 1] - '0';
            break;
        }
        num = whole * 100 + frac;
    }
    if (i + 2 <= n && ig_strieq_n(v + i, "px", 2)) {
        if (num <= 1250) return 0;
        if (num <= 1700) return 1;
        if (num <= 2400) return 2;
        return 3;
    }
    if (i + 2 <= n && ig_strieq_n(v + i, "pt", 2)) {
        if (num <= 950) return 0;
        if (num <= 1300) return 1;
        if (num <= 1800) return 2;
        return 3;
    }
    if ((i + 2 <= n && (ig_strieq_n(v + i, "em", 2))) || (i + 3 <= n && ig_strieq_n(v + i, "rem", 3))) {
        if (num < 90) return 0;
        if (num <= 115) return 1;
        if (num <= 160) return 2;
        return 3;
    }
    if (i < n && v[i] == '%') {
        if (num < 9000) return 0;
        if (num <= 11500) return 1;
        if (num <= 16000) return 2;
        return 3;
    }
    return -1;
}

/* Parses "prop: value; prop: value" into props. */
static void parse_decls(const char* s, unsigned int n, CssProps* pr)
{
    unsigned int i = 0;
    int w1 = 0, h1 = 0, ovh = 0;
    while (i < n) {
        unsigned int ps, pe, vs, ve;
        while (i < n && (is_ws((unsigned char)s[i]) || s[i] == ';')) i++;
        ps = i;
        while (i < n && s[i] != ':' && s[i] != ';') i++;
        pe = i;
        while (pe > ps && is_ws((unsigned char)s[pe - 1])) pe--;
        if (i >= n || s[i] != ':') { continue; }
        i++;
        while (i < n && is_ws((unsigned char)s[i])) i++;
        vs = i;
        {
            int paren = 0;
            while (i < n && (s[i] != ';' || paren)) {
                if (s[i] == '(') paren++;
                else if (s[i] == ')' && paren) paren--;
                i++;
            }
        }
        ve = i;
        while (ve > vs && is_ws((unsigned char)s[ve - 1])) ve--;
        /* "!important" changes nothing here */
        {
            unsigned int k;
            for (k = vs; k < ve; k++) if (s[k] == '!') { ve = k; while (ve > vs && is_ws((unsigned char)s[ve - 1])) ve--; break; }
        }
        {
            const char* p = s + ps;
            unsigned int pl = pe - ps;
            const char* v = s + vs;
            unsigned int vl = ve - vs;
            if (word_is(p, pl, "display")) {
                pr->mask |= CSS_DISPLAY;
                if (word_is(v, vl, "none")) pr->display = D_NONE;
                else if (word_is(v, vl, "inline") || word_is(v, vl, "inline-block") || word_is(v, vl, "inline-flex") ||
                         word_is(v, vl, "contents") || word_is(v, vl, "inline-table")) pr->display = D_INLINE;
                else if (word_is(v, vl, "list-item")) pr->display = D_LISTITEM;
                else if (word_is(v, vl, "table-cell")) pr->display = D_CELL;
                else if (word_is(v, vl, "table-row")) pr->display = D_ROW;
                else pr->display = D_BLOCK;
            } else if (word_is(p, pl, "color")) {
                int c = parse_color(v, vl);
                if (c >= 0) { pr->mask |= CSS_COLOR; pr->color = c; }
            } else if (word_is(p, pl, "background-color") || word_is(p, pl, "background")) {
                int c = -1;
                if (word_is(p, pl, "background")) {
                    /* the first word that is a color */
                    unsigned int k = 0;
                    while (k < vl) {
                        unsigned int st;
                        int paren = 0;
                        while (k < vl && is_ws((unsigned char)v[k])) k++;
                        st = k;
                        while (k < vl && (!is_ws((unsigned char)v[k]) || paren)) {
                            if (v[k] == '(') paren++; else if (v[k] == ')' && paren) paren--;
                            k++;
                        }
                        if (k > st) {
                            c = parse_color(v + st, k - st);
                            if (c != -1) break;
                        }
                    }
                } else c = parse_color(v, vl);
                if (c != -1) { pr->mask |= CSS_BG; pr->bg = c; }
            } else if (word_is(p, pl, "font-weight")) {
                pr->mask |= CSS_BOLD;
                pr->bold = (unsigned char)(word_is(v, vl, "bold") || word_is(v, vl, "bolder") ||
                           (vl == 3 && v[0] >= '6' && v[0] <= '9'));
            } else if (word_is(p, pl, "font-style")) {
                pr->mask |= CSS_ITALIC;
                pr->italic = (unsigned char)(word_is(v, vl, "italic") || word_is(v, vl, "oblique"));
            } else if (word_is(p, pl, "font")) {
                int sz;
                unsigned int k = 0;
                pr->mask |= CSS_BOLD | CSS_ITALIC;
                pr->bold = (unsigned char)(contains_ci(v, vl, "bold") || contains_ci(v, vl, "700") || contains_ci(v, vl, "800") || contains_ci(v, vl, "900"));
                pr->italic = (unsigned char)contains_ci(v, vl, "italic");
                /* the size: the word with a unit */
                while (k < vl) {
                    unsigned int st;
                    while (k < vl && is_ws((unsigned char)v[k])) k++;
                    st = k;
                    while (k < vl && !is_ws((unsigned char)v[k]) && v[k] != '/') k++;
                    if (k > st && ((v[st] >= '0' && v[st] <= '9') || v[st] == '.')) {
                        sz = css_font_size(v + st, k - st);
                        if (sz >= 0) { pr->mask |= CSS_SIZE; pr->size = (unsigned char)sz; break; }
                    }
                    while (k < vl && !is_ws((unsigned char)v[k])) k++;
                }
            } else if (word_is(p, pl, "font-size")) {
                int sz = css_font_size(v, vl);
                if (sz >= 0) { pr->mask |= CSS_SIZE; pr->size = (unsigned char)sz; }
            } else if (word_is(p, pl, "font-family")) {
                if (contains_ci(v, vl, "mono") || contains_ci(v, vl, "courier")) { pr->mask |= CSS_MONO; pr->mono = 1; }
            } else if (word_is(p, pl, "text-align")) {
                pr->mask |= CSS_ALIGN;
                pr->align = (unsigned char)(word_is(v, vl, "center") ? BW_ALIGN_CENTER :
                            (word_is(v, vl, "right") || word_is(v, vl, "end")) ? BW_ALIGN_RIGHT : BW_ALIGN_LEFT);
            } else if (word_is(p, pl, "text-decoration") || word_is(p, pl, "text-decoration-line")) {
                pr->mask |= CSS_UNDERLINE | CSS_STRIKE;
                pr->underline = (unsigned char)contains_ci(v, vl, "underline");
                pr->strike = (unsigned char)contains_ci(v, vl, "line-through");
            } else if (word_is(p, pl, "visibility")) {
                if (word_is(v, vl, "hidden") || word_is(v, vl, "collapse")) { pr->mask |= CSS_HIDDEN; pr->hidden = 1; }
            } else if (word_is(p, pl, "white-space")) {
                pr->mask |= CSS_PRE;
                pr->pre = (unsigned char)(word_is(v, vl, "pre") || word_is(v, vl, "pre-wrap"));
            } else if (word_is(p, pl, "left") || word_is(p, pl, "text-indent") || word_is(p, pl, "top") ||
                       word_is(p, pl, "margin-left")) {
                /* off-screen text (skip links, screen-reader labels) */
                if (contains_ci(v, vl, "-999") || contains_ci(v, vl, "-1000")) { pr->mask |= CSS_HIDDEN; pr->hidden = 1; }
            } else if (word_is(p, pl, "clip")) {
                if (contains_ci(v, vl, "rect(0") || contains_ci(v, vl, "rect(1px")) { pr->mask |= CSS_HIDDEN; pr->hidden = 1; }
            } else if (word_is(p, pl, "clip-path")) {
                if (contains_ci(v, vl, "inset(50%")) { pr->mask |= CSS_HIDDEN; pr->hidden = 1; }
            } else if (word_is(p, pl, "width")) {
                if (word_is(v, vl, "1px") || word_is(v, vl, "0") || word_is(v, vl, "0px")) w1 = 1;
            } else if (word_is(p, pl, "height")) {
                if (word_is(v, vl, "1px") || word_is(v, vl, "0") || word_is(v, vl, "0px")) h1 = 1;
            } else if (word_is(p, pl, "overflow")) {
                if (word_is(v, vl, "hidden")) ovh = 1;
            } else if (word_is(p, pl, "opacity")) {
                if (word_is(v, vl, "0")) { pr->mask |= CSS_HIDDEN; pr->hidden = 1; }
            }
        }
    }
    if (w1 && h1 && ovh) { pr->mask |= CSS_HIDDEN; pr->hidden = 1; }
}

/* ------------------------------------------------------------------ CSS rules */

static void skip_css_ws(const char* s, unsigned int n, unsigned int* i)
{
    for (;;) {
        while (*i < n && is_ws((unsigned char)s[*i])) (*i)++;
        if (*i + 1 < n && s[*i] == '/' && s[*i + 1] == '*') {
            *i += 2;
            while (*i + 1 < n && !(s[*i] == '*' && s[*i + 1] == '/')) (*i)++;
            *i = *i + 2 > n ? n : *i + 2;
            continue;
        }
        if (*i + 3 < n && s[*i] == '<' && s[*i + 1] == '!' && s[*i + 2] == '-' && s[*i + 3] == '-') { *i += 4; continue; }
        if (*i + 2 < n && s[*i] == '-' && s[*i + 1] == '-' && s[*i + 2] == '>') { *i += 3; continue; }
        break;
    }
}

/* skips a {...} block with nesting; *i at '{' */
static void skip_block(const char* s, unsigned int n, unsigned int* i)
{
    int depth = 0;
    while (*i < n) {
        char c = s[*i];
        if (c == '{') depth++;
        else if (c == '}') { depth--; if (depth <= 0) { (*i)++; return; } }
        else if (c == '"' || c == '\'') {
            char q = c;
            (*i)++;
            while (*i < n && s[*i] != q) (*i)++;
        }
        (*i)++;
    }
}

/* does a media query list apply to a 640 px screen? */
static int media_matches(const char* s, unsigned int n)
{
    unsigned int i = 0;
    while (i < n) {
        unsigned int st = i, e;
        int ok = 1;
        while (i < n && s[i] != ',') i++;
        e = i;
        if (i < n) i++;
        {
            const char* q = s + st;
            unsigned int ql = e - st, k;
            if (contains_ci(q, ql, "print") && !contains_ci(q, ql, "screen")) ok = 0;
            if (contains_ci(q, ql, "speech")) ok = 0;
            for (k = 0; k + 9 < ql && ok; k++) {
                int isMin = ig_strieq_n(q + k, "min-width", 9);
                int isMax = ig_strieq_n(q + k, "max-width", 9);
                if (isMin || isMax) {
                    unsigned int m = k + 9;
                    int v;
                    while (m < ql && (q[m] == ':' || is_ws((unsigned char)q[m]))) m++;
                    v = read_num(q, ql, &m);
                    if (v >= 0) {
                        if (m + 2 <= ql && ig_strieq_n(q + m, "em", 2)) v *= 16;
                        if (isMin && DEVICE_WIDTH < v) ok = 0;
                        if (isMax && DEVICE_WIDTH > v) ok = 0;
                    }
                }
                if (ig_strieq_n(q + k, "orientation", 11 < ql - k ? 11 : ql - k) && contains_ci(q, ql, "portrait")) ok = 0;
            }
        }
        if (ok) return 1;
    }
    return 0;
}

static unsigned int bloom_bit(const char* s, unsigned int n)
{
    unsigned int h = 2166136261u, i;
    for (i = 0; i < n; i++) { h ^= (unsigned char)s[i]; h *= 16777619u; }
    return 1u << (h & 31);
}

static int add_rule(P* p, int tag, const char* id, unsigned int idLen, const char* cls, unsigned int clsLen,
                    const CssProps* pr)
{
    CssRule* r;
    if (!grow((void**)&p->rules, &p->ruleCap, p->ruleCount, sizeof(CssRule))) return 0;
    r = &p->rules[p->ruleCount];
    ig_memset(r, 0, sizeof(CssRule));
    r->tag = tag;
    if (idLen) { r->id = ig_strndup(id, idLen); if (!r->id) return 0; }
    if (clsLen) {
        /* "a.b" -> "a\0b\0\0" */
        unsigned int k;
        r->classes = (char*)ig_malloc(clsLen + 2);
        if (!r->classes) { ig_free(r->id); return 0; }
        for (k = 0; k < clsLen; k++) r->classes[k] = cls[k] == '.' ? 0 : cls[k];
        r->classes[clsLen] = 0;
        r->classes[clsLen + 1] = 0;
    }
    r->spec = idLen ? 2 : clsLen ? 1 : 0;
    r->bloom = (tag ? 1u << (tag & 31) : 0) | (idLen ? bloom_bit(id, idLen) : 0);
    if (clsLen) {
        unsigned int k = 0;
        while (k < clsLen) {
            unsigned int st = k;
            while (k < clsLen && cls[k] != '.') k++;
            if (k > st) r->bloom |= bloom_bit(cls + st, k - st);
            k++;
        }
    }
    r->props = *pr;
    p->ruleCount++;
    return 1;
}

/* one selector (no combinators) -> a rule; anything we cannot match is dropped */
static void add_selector(P* p, const char* s, unsigned int n, const CssProps* pr)
{
    unsigned int i = 0, ts, te, is = 0, ie = 0, cs = 0, ce = 0;
    int tag = 0;
    while (n && is_ws((unsigned char)s[n - 1])) n--;
    while (i < n && is_ws((unsigned char)s[i])) i++;
    /* a:link / a:visited are just a */
    if (n > 5 && ig_strieq_n(s + n - 5, ":link", 5)) n -= 5;
    else if (n > 8 && ig_strieq_n(s + n - 8, ":visited", 8)) n -= 8;
    ts = i;
    while (i < n && (is_alnum((unsigned char)s[i]) || s[i] == '-' || s[i] == '_' || s[i] == '*')) i++;
    te = i;
    if (te > ts) {
        if (!(te - ts == 1 && s[ts] == '*')) {
            char name[16];
            unsigned int k;
            if (te - ts >= sizeof(name)) return;
            for (k = ts; k < te; k++) name[k - ts] = (char)lower((unsigned char)s[k]);
            tag = lookup_tag(name, te - ts);
            if (!tag) return;
        }
    }
    while (i < n) {
        if (s[i] == '#' && !is) {
            i++;
            is = i;
            while (i < n && (is_alnum((unsigned char)s[i]) || s[i] == '-' || s[i] == '_')) i++;
            ie = i;
        } else if (s[i] == '.') {
            if (!cs) cs = i + 1;
            i++;
            while (i < n && (is_alnum((unsigned char)s[i]) || s[i] == '-' || s[i] == '_')) i++;
            ce = i;
        } else return;       /* combinator, pseudo-class, attribute selector: not for us */
    }
    if (!tag && !is && !cs && te == ts) return;
    add_rule(p, tag, s + is, ie - is, cs ? s + cs : 0, cs ? ce - cs : 0, pr);
    p->doc->cssRules++;
}

static void parse_css(P* p, const char* s, unsigned int n, int depth)
{
    unsigned int i = 0;
    while (i < n) {
        unsigned int ss, se;
        skip_css_ws(s, n, &i);
        if (i >= n) break;
        if (s[i] == '}') { i++; continue; }
        if (s[i] == '@') {
            unsigned int st = i;
            while (i < n && s[i] != '{' && s[i] != ';') i++;
            if (i >= n) break;
            if (s[i] == ';') { i++; continue; }        /* @import, @charset */
            if (depth < 3 && ((i - st > 6 && ig_strieq_n(s + st, "@media", 6) && media_matches(s + st + 6, i - st - 6)) ||
                              (i - st > 9 && ig_strieq_n(s + st, "@supports", 9)))) {
                unsigned int bs = i + 1, be;
                skip_block(s, n, &i);
                be = i > bs ? i - 1 : bs;
                parse_css(p, s + bs, be - bs, depth + 1);
            } else skip_block(s, n, &i);
            continue;
        }
        ss = i;
        while (i < n && s[i] != '{' && s[i] != '}') i++;
        se = i;
        if (i >= n || s[i] == '}') { i++; continue; }
        {
            unsigned int ds = i + 1, de;
            CssProps pr;
            i = ds;
            while (i < n && s[i] != '}') {
                if (s[i] == '"' || s[i] == '\'') { char q = s[i++]; while (i < n && s[i] != q) i++; }
                if (i < n) i++;
            }
            de = i;
            if (i < n) i++;
            ig_memset(&pr, 0, sizeof(pr));
            parse_decls(s + ds, de - ds, &pr);
            if (!pr.mask) continue;
            /* each selector of the group */
            {
                unsigned int k = ss;
                while (k < se) {
                    unsigned int st = k;
                    while (k < se && s[k] != ',') k++;
                    add_selector(p, s + st, k - st, &pr);
                    k++;
                }
            }
        }
    }
}

static void free_rules(P* p)
{
    unsigned int i;
    for (i = 0; i < p->ruleCount; i++) { ig_free(p->rules[i].id); ig_free(p->rules[i].classes); }
    ig_free(p->rules);
    p->rules = 0;
    p->ruleCount = p->ruleCap = 0;
}

/* ------------------------------------------------------------------ attributes */

static const char* attr(P* p, const char* name)
{
    int i;
    unsigned int n = ig_strlen(name);
    for (i = 0; i < p->attrCount; i++)
        if (p->attrs[i].nameLen == n && ig_strieq_n(p->attrs[i].name, name, n))
            return p->attrBuf.data ? p->attrBuf.data + p->attrs[i].valOff : "";
    return 0;
}

static int has_attr(P* p, const char* name) { return attr(p, name) != 0; }

static int attr_int(P* p, const char* name, int def)
{
    const char* v = attr(p, name);
    unsigned int i = 0;
    int r;
    if (!v) return def;
    while (v[i] == ' ') i++;
    r = read_num(v, ig_strlen(v), &i);
    if (r < 0) return def;
    if (v[i] == '%') return def;
    return r;
}

/* class list contains the class? */
static int has_class(const char* list, const char* cls)
{
    unsigned int cl = ig_strlen(cls), i = 0;
    if (!list) return 0;
    while (list[i]) {
        unsigned int st;
        while (list[i] && is_ws((unsigned char)list[i])) i++;
        st = i;
        while (list[i] && !is_ws((unsigned char)list[i])) i++;
        if (i - st == cl && ig_memcmp(list + st, cls, cl) == 0) return 1;
    }
    return 0;
}

/* Reads the attributes of a start tag at s[*i] up to '>' (values decoded to UTF-8). */
static int read_attrs(P* p, unsigned int* ip, int* selfClose)
{
    const char* s = p->s;
    unsigned int n = p->n, i = *ip;
    p->attrCount = 0;
    p->attrBuf.len = 0;
    p->attrBuf.failed = 0;
    *selfClose = 0;
    for (;;) {
        unsigned int ns, ne;
        while (i < n && (is_ws((unsigned char)s[i]) || s[i] == '/')) {
            if (s[i] == '/' && i + 1 < n && s[i + 1] == '>') *selfClose = 1;
            i++;
        }
        if (i >= n) break;
        if (s[i] == '>') { i++; break; }
        ns = i;
        while (i < n && !is_ws((unsigned char)s[i]) && s[i] != '=' && s[i] != '>' && !(s[i] == '/' && i + 1 < n && s[i + 1] == '>')) i++;
        ne = i;
        if (ne == ns) { i++; continue; }
        while (i < n && is_ws((unsigned char)s[i])) i++;
        if (p->attrCount < MAX_ATTRS) {
            Attr* a = &p->attrs[p->attrCount++];
            a->name = s + ns;
            a->nameLen = ne - ns;
            a->valOff = p->attrBuf.len;
            a->hasValue = 0;
        }
        if (i < n && s[i] == '=') {
            unsigned int vs, ve;
            char q = 0;
            i++;
            while (i < n && is_ws((unsigned char)s[i])) i++;
            if (i < n && (s[i] == '"' || s[i] == '\'')) q = s[i++];
            vs = i;
            if (q) { while (i < n && s[i] != q) i++; ve = i; if (i < n) i++; }
            else { while (i < n && !is_ws((unsigned char)s[i]) && s[i] != '>') i++; ve = i; }
            if (p->attrCount && p->attrs[p->attrCount - 1].name == s + ns) {
                /* decode: charset + entities -> UTF-8 */
                unsigned int k = vs;
                p->attrs[p->attrCount - 1].hasValue = 1;
                while (k < ve) {
                    unsigned int c = read_char(p->cs, s, ve, &k, 1, 1);
                    char u[3];
                    if (c < 0x80) ig_buf_putc(&p->attrBuf, (char)(c == '\n' || c == '\r' || c == '\t' ? ' ' : c));
                    else if (c < 0x800) {
                        u[0] = (char)(0xC0 | (c >> 6)); u[1] = (char)(0x80 | (c & 0x3F));
                        ig_buf_append(&p->attrBuf, u, 2);
                    } else {
                        u[0] = (char)(0xE0 | (c >> 12)); u[1] = (char)(0x80 | ((c >> 6) & 0x3F)); u[2] = (char)(0x80 | (c & 0x3F));
                        ig_buf_append(&p->attrBuf, u, 3);
                    }
                }
            }
        }
        if (p->attrCount && p->attrs[p->attrCount - 1].name == s + ns) ig_buf_putc(&p->attrBuf, 0);
    }
    *ip = i;
    return !p->attrBuf.failed;
}

/* ------------------------------------------------------------------ styles */

static int default_display(int tag)
{
    switch (tag) {
    case T_ADDRESS: case T_ARTICLE: case T_ASIDE: case T_BLOCKQUOTE: case T_BODY: case T_CAPTION:
    case T_CENTER: case T_DD: case T_DETAILS: case T_DIALOG: case T_DIR: case T_DIV: case T_DL: case T_DT:
    case T_FIELDSET: case T_FIGCAPTION: case T_FIGURE: case T_FOOTER: case T_FORM: case T_FRAMESET:
    case T_H1: case T_H2: case T_H3: case T_H4: case T_H5: case T_H6: case T_HEADER: case T_HGROUP:
    case T_HTML: case T_LEGEND: case T_LISTING: case T_MAIN: case T_MENU: case T_NAV: case T_NOFRAMES:
    case T_OL: case T_P: case T_PLAINTEXT: case T_PRE: case T_SECTION: case T_SUMMARY: case T_TABLE:
    case T_UL: case T_XMP: case T_NOSCRIPT: case T_TBODY: case T_THEAD: case T_TFOOT:
        return D_BLOCK;
    case T_TR: return D_ROW;
    case T_TD: case T_TH: return D_CELL;
    case T_LI: return D_LISTITEM;
    case T_HEAD: case T_SCRIPT: case T_STYLE: case T_TEMPLATE: case T_TITLE: case T_META: case T_LINK:
    case T_BASE: case T_NOEMBED: case T_PARAM: case T_SOURCE: case T_TRACK: case T_SVG: case T_MATH:
    case T_CANVAS: case T_MAP: case T_AREA: case T_DATALIST: case T_COL: case T_COLGROUP: case T_OBJECT:
    case T_EMBED: case T_AUDIO: case T_VIDEO:
        return D_NONE;
    default:
        return D_INLINE;
    }
}

static int margin_top(int tag, int depth)
{
    switch (tag) {
    case T_P: case T_BLOCKQUOTE: case T_DL: case T_PRE: case T_FIELDSET: case T_FIGURE: case T_XMP: return 6;
    case T_H1: return 10;
    case T_H2: return 8;
    case T_H3: case T_H4: case T_H5: case T_H6: return 6;
    case T_UL: case T_OL: case T_MENU: case T_DIR: return depth ? 0 : 6;
    case T_TABLE: return 4;
    default: return 0;
    }
}

static int indent_of(int tag)
{
    switch (tag) {
    case T_UL: case T_OL: case T_MENU: case T_DIR: return 20;
    case T_BLOCKQUOTE: return 20;
    case T_DD: return 20;
    case T_FIGURE: return 12;
    default: return 0;
    }
}

static void apply_props(Elem* e, const CssProps* pr)
{
    if (pr->mask & CSS_DISPLAY) e->display = pr->display;
    if (pr->mask & CSS_COLOR) e->color = pr->color;
    if (pr->mask & CSS_BG) e->bg = pr->bg;
    if (pr->mask & CSS_BOLD) e->font = (unsigned char)(pr->bold ? (e->font | BW_FONT_BOLD) : (e->font & ~BW_FONT_BOLD));
    if (pr->mask & CSS_ITALIC) e->font = (unsigned char)(pr->italic ? (e->font | BW_FONT_ITALIC) : (e->font & ~BW_FONT_ITALIC));
    if (pr->mask & CSS_MONO) e->font = (unsigned char)(e->font | BW_FONT_MONO);
    if (pr->mask & CSS_SIZE) e->font = BW_FONT_MAKE(pr->size, e->font & 7);
    if (pr->mask & CSS_ALIGN) e->align = pr->align;
    if (pr->mask & CSS_UNDERLINE) e->flags = (unsigned char)(pr->underline ? (e->flags | BW_OPF_UNDERLINE) : (e->flags & ~BW_OPF_UNDERLINE));
    if (pr->mask & CSS_STRIKE) e->flags = (unsigned char)(pr->strike ? (e->flags | BW_OPF_STRIKE) : (e->flags & ~BW_OPF_STRIKE));
    if (pr->mask & CSS_PRE) e->flags = (unsigned char)(pr->pre ? (e->flags | BW_OPF_PRE) : (e->flags & ~BW_OPF_PRE));
    if (pr->mask & CSS_HIDDEN) e->hidden = 1;
}

/* computes the style of a new element from its parent, the tag, the attributes and the CSS */
static void compute_style(P* p, Elem* e, const Elem* parent, int tag)
{
    unsigned char size;
    const char* v;
    *e = *parent;
    e->tag = (unsigned char)tag;
    e->display = (unsigned char)default_display(tag);
    e->bg = -1;
    e->blockOpen = 0;
    e->listKind = 0;
    e->marginBottom = 0;
    size = (unsigned char)BW_FONT_SIZE(e->font);
    switch (tag) {
    case T_B: case T_STRONG: case T_TH: case T_DT: case T_LEGEND: case T_SUMMARY:
        e->font |= BW_FONT_BOLD; break;
    case T_I: case T_EM: case T_CITE: case T_VAR: case T_DFN: case T_ADDRESS:
        e->font |= BW_FONT_ITALIC; break;
    case T_CODE: case T_TT: case T_KBD: case T_SAMP:
        e->font |= BW_FONT_MONO; break;
    case T_PRE: case T_XMP: case T_LISTING: case T_PLAINTEXT:
        e->font |= BW_FONT_MONO; e->flags |= BW_OPF_PRE; break;
    case T_U: case T_INS:
        e->flags |= BW_OPF_UNDERLINE; break;
    case T_S: case T_STRIKE: case T_DEL:
        e->flags |= BW_OPF_STRIKE; break;
    case T_SMALL: case T_SUB: case T_SUP:
        e->font = BW_FONT_MAKE(size > 0 ? size - 1 : 0, e->font & 7); break;
    case T_BIG:
        e->font = BW_FONT_MAKE(size < 3 ? size + 1 : 3, e->font & 7); break;
    case T_H1: e->font = BW_FONT_MAKE(3, (e->font & 7) | BW_FONT_BOLD); break;
    case T_H2: e->font = BW_FONT_MAKE(2, (e->font & 7) | BW_FONT_BOLD); break;
    case T_H3: e->font = BW_FONT_MAKE(2, (e->font & 7) | BW_FONT_BOLD); break;
    case T_H4: e->font = BW_FONT_MAKE(1, (e->font & 7) | BW_FONT_BOLD); break;
    case T_H5: case T_H6: e->font = BW_FONT_MAKE(1, (e->font & 7) | BW_FONT_BOLD); break;
    case T_CENTER: case T_CAPTION: e->align = BW_ALIGN_CENTER; break;
    case T_MARK: e->bg = 0xFFFF00; break;
    case T_NOBR: break;
    default: break;
    }
    if (tag == T_UL || tag == T_OL || tag == T_MENU || tag == T_DIR) {
        e->listKind = (unsigned char)(tag == T_OL ? 2 : 1);
        e->listCounter = attr_int(p, "start", 1);
        e->listDepth = parent->listDepth + 1;
    }
    /* legacy presentation attributes */
    v = attr(p, "align");
    if (v && (e->display == D_BLOCK || e->display == D_ROW || tag == T_TD || tag == T_TH)) {
        unsigned int vl = ig_strlen(v);
        if (word_is(v, vl, "center") || word_is(v, vl, "middle")) e->align = BW_ALIGN_CENTER;
        else if (word_is(v, vl, "right")) e->align = BW_ALIGN_RIGHT;
        else if (word_is(v, vl, "left") || word_is(v, vl, "justify")) e->align = BW_ALIGN_LEFT;
    }
    if (tag == T_FONT) {
        v = attr(p, "color");
        if (v) { int c = parse_color(v, ig_strlen(v)); if (c >= 0) e->color = c; }
        v = attr(p, "size");
        if (v) {
            int sz = -1;
            unsigned int k = 0;
            while (v[k] == ' ') k++;
            if (v[k] == '+') sz = size + 1;
            else if (v[k] == '-') sz = size - 1;
            else if (v[k] >= '1' && v[k] <= '7') {
                int n = v[k] - '0';
                sz = n <= 2 ? 0 : n == 3 ? 1 : n <= 5 ? 2 : 3;
            }
            if (sz < 0) sz = 0;
            if (sz > 3) sz = 3;
            e->font = BW_FONT_MAKE(sz, e->font & 7);
        }
    }
    v = attr(p, "bgcolor");
    if (v && tag != T_BODY && (e->display == D_BLOCK)) {
        int c = parse_color(v, ig_strlen(v));
        if (c >= 0) e->bg = c;
    }
    if (has_attr(p, "hidden")) e->hidden = 1;
    if (attr(p, "aria-hidden") && ig_streq(attr(p, "aria-hidden"), "true") && (tag == T_SVG || tag == T_SPAN || tag == T_I)) e->hidden = 1;
    /* the stylesheet: tag rules, then class rules, then id rules, each in source order */
    if (p->ruleCount) {
        const char* id = attr(p, "id");
        const char* cls = attr(p, "class");
        int spec;
        unsigned int have = 1u << (tag & 31);
        if (id) have |= bloom_bit(id, ig_strlen(id));
        if (cls) {
            unsigned int k = 0;
            while (cls[k]) {
                unsigned int st;
                while (cls[k] && is_ws((unsigned char)cls[k])) k++;
                st = k;
                while (cls[k] && !is_ws((unsigned char)cls[k])) k++;
                if (k > st) have |= bloom_bit(cls + st, k - st);
            }
        }
        for (spec = 0; spec <= 2; spec++) {
            unsigned int r;
            for (r = 0; r < p->ruleCount; r++) {
                const CssRule* rule = &p->rules[r];
                if (rule->bloom & ~have) continue;
                if (rule->spec != spec) continue;
                if (rule->tag && rule->tag != tag) continue;
                if (rule->id && (!id || !ig_streq(rule->id, id))) continue;
                if (rule->classes) {
                    const char* c = rule->classes;
                    int ok = 1;
                    if (!cls) continue;
                    while (*c) {
                        if (!has_class(cls, c)) { ok = 0; break; }
                        c += ig_strlen(c) + 1;
                    }
                    if (!ok) continue;
                }
                apply_props(e, &rule->props);
            }
        }
    }
    v = attr(p, "style");
    if (v) {
        CssProps pr;
        ig_memset(&pr, 0, sizeof(pr));
        parse_decls(v, ig_strlen(v), &pr);
        apply_props(e, &pr);
    }
    if (e->display == D_NONE) e->hidden = 1;
}

/* ------------------------------------------------------------------ the element stack */

static void emit_block_start(P* p, Elem* e, int margin)
{
    BwOp* op;
    if (e->hidden || capturing(p)) return;
    op = add_op(p, BW_OP_BLOCK_START);
    if (!op) return;
    op->a = margin;
    op->b = indent_of(e->tag);
    op->align = e->align;
    op->color = e->bg >= 0 ? e->bg : -1;
    op->font = e->font;
    e->blockOpen = 1;
    p->lastSpace = 1;
}

static void finish_capture(P* p);

static void pop(P* p)
{
    Elem* e;
    if (p->depth <= 1) return;
    e = top(p);
    if (e->tag == T_BUTTON && p->capture == 1) finish_capture(p);
    if (e->tag == T_OPTION && p->capture == 2) finish_capture(p);
    if (e->tag == T_SELECT && p->curSelect >= 0) {
        if (p->capture == 2) finish_capture(p);
        {
            BwControl* c = &p->doc->controls[p->curSelect];
            if (c->target >= 0) {
                BwOp* op = add_op(p, BW_OP_CONTROL);
                if (op) { op->ref = p->curSelect; op->target = c->target; op->font = e->font; }
                p->lastSpace = 0;
            }
        }
        p->curSelect = -1;
    }
    if (e->tag == T_FORM) p->curForm = -1;
    if (e->blockOpen) {
        BwOp* op = add_op(p, BW_OP_BLOCK_END);
        if (op) op->a = e->marginBottom;
        p->lastSpace = 1;
    }
    p->depth--;
}

/* pops up to and including the nearest element with that tag, if one is open below a stop tag */
static int close_tag(P* p, int tag, const int* stops)
{
    int i;
    for (i = p->depth - 1; i >= 1; i--) {
        int t = p->stack[i].tag;
        if (t == tag) {
            while (p->depth > i) pop(p);
            return 1;
        }
        if (stops) {
            const int* s;
            for (s = stops; *s; s++) if (*s == t) return 0;
        }
    }
    return 0;
}

static int in_stack(P* p, int tag)
{
    int i;
    for (i = p->depth - 1; i >= 1; i--) if (p->stack[i].tag == tag) return 1;
    return 0;
}

static int is_void(int tag)
{
    switch (tag) {
    case T_AREA: case T_BASE: case T_BR: case T_COL: case T_EMBED: case T_HR: case T_IMG: case T_INPUT:
    case T_LINK: case T_META: case T_PARAM: case T_SOURCE: case T_TRACK: case T_WBR: case T_FRAME:
        return 1;
    default: return 0;
    }
}

/* tags whose start closes an open <p> */
static int closes_p(int tag)
{
    switch (tag) {
    case T_ADDRESS: case T_ARTICLE: case T_ASIDE: case T_BLOCKQUOTE: case T_CENTER: case T_DETAILS:
    case T_DIALOG: case T_DIR: case T_DIV: case T_DL: case T_FIELDSET: case T_FIGCAPTION: case T_FIGURE:
    case T_FOOTER: case T_FORM: case T_H1: case T_H2: case T_H3: case T_H4: case T_H5: case T_H6:
    case T_HEADER: case T_HGROUP: case T_HR: case T_LI: case T_LISTING: case T_MAIN: case T_MENU:
    case T_NAV: case T_OL: case T_P: case T_PRE: case T_SECTION: case T_SUMMARY: case T_TABLE: case T_UL:
    case T_XMP: case T_DD: case T_DT: case T_PLAINTEXT:
        return 1;
    default: return 0;
    }
}

/* ------------------------------------------------------------------ captures (button / option) */

static void start_capture(P* p, int kind, int ctl)
{
    p->capture = kind;
    p->captureCtl = ctl;
    p->cap16.len = 0;
    p->cap16.failed = 0;
    p->lastSpace = 1;
}

static void finish_capture(P* p)
{
    BwDoc* d = p->doc;
    unsigned short* s = (unsigned short*)p->cap16.data;
    unsigned int n = p->cap16.len / 2;
    int kind = p->capture;
    p->capture = 0;
    while (n && s[n - 1] == ' ') n--;
    if (kind == 1 && p->captureCtl >= 0) {
        BwControl* c = &d->controls[p->captureCtl];
        Elem* e = top(p);
        ig_free(c->label);
        c->label = n ? dup16(s, n) : 0;
        c->labelLen = c->label ? n : 0;
        if (c->target >= 0) {
            BwOp* op = add_op(p, BW_OP_CONTROL);
            if (op) { op->ref = p->captureCtl; op->target = c->target; op->font = e->font; }
        }
        p->lastSpace = 0;
    } else if (kind == 2 && p->curSelect >= 0) {
        BwControl* c = &d->controls[p->curSelect];
        BwOption* no = (BwOption*)ig_malloc((c->optionCount + 1) * sizeof(BwOption));
        if (no) {
            BwOption* o;
            if (c->options) { ig_memcpy(no, c->options, c->optionCount * sizeof(BwOption)); ig_free(c->options); }
            c->options = no;
            o = &c->options[c->optionCount];
            o->label = dup16(s ? s : (unsigned short*)"\0\0", n);
            o->labelLen = o->label ? n : 0;
            if (p->optValue) { o->value = p->optValue; o->valueLen = p->optValueLen; p->optValue = 0; }
            else { o->value = dup16(s ? s : (unsigned short*)"\0\0", n); o->valueLen = o->value ? n : 0; }
            if (p->optSelected) c->selected = (int)c->optionCount;
            c->optionCount++;
        } else d->failed = 1;
        ig_free(p->optValue);
        p->optValue = 0;
    }
}

/* ------------------------------------------------------------------ elements */

static BwControl* new_control(P* p, int type, int* index)
{
    BwDoc* d = p->doc;
    BwControl* c;
    if (!grow((void**)&d->controls, &d->controlCap, d->controlCount, sizeof(BwControl))) { d->failed = 1; return 0; }
    c = &d->controls[d->controlCount];
    ig_memset(c, 0, sizeof(BwControl));
    c->type = type;
    c->form = p->curForm;
    c->target = -1;
    c->selected = 0;
    {
        const char* nm = attr(p, "name");
        if (nm && nm[0]) c->name = ig_strdup(nm);
    }
    *index = (int)d->controlCount++;
    return c;
}

static void set_value_from_attr(BwControl* c, const char* v)
{
    if (!v) return;
    c->value = utf8_to_16(v, &c->valueLen);
}

static void add_anchor(P* p, const char* name)
{
    BwDoc* d = p->doc;
    BwOp* op;
    if (!name || !name[0] || top(p)->hidden || capturing(p)) return;
    if (!grow((void**)&d->anchors, &d->anchorCap, d->anchorCount, sizeof(BwAnchor))) { d->failed = 1; return; }
    d->anchors[d->anchorCount].name = ig_strdup(name);
    if (!d->anchors[d->anchorCount].name) return;
    op = add_op(p, BW_OP_ANCHOR);
    if (op) op->ref = (int)d->anchorCount;
    d->anchorCount++;
}

/* the text of a short link/label put straight into the stream: "[Frame]" */
static void put_label(P* p, const char* utf8)
{
    unsigned int i = 0, n = ig_strlen(utf8);
    Elem* e = top(p);
    while (i < n) {
        unsigned int c = read_char(BW_CS_UTF8, utf8, n, &i, 0, 0);
        put_text_char(p, c, e);
    }
}

static int new_link(P* p, const char* href)
{
    BwDoc* d = p->doc;
    char* url;
    int t;
    if (!href) return -1;
    {
        unsigned int k = 0;
        while (href[k] == ' ') k++;
        if (ig_strieq_n(href + k, "javascript:", 11)) return -1;
    }
    url = bw_url_resolve(d->baseUrl, href, ig_strlen(href));
    if (!url) return -1;
    if (!grow((void**)&d->links, &d->linkCap, d->linkCount, sizeof(BwLink))) { ig_free(url); d->failed = 1; return -1; }
    d->links[d->linkCount].href = url;
    t = add_target(p, BW_TGT_LINK, (int)d->linkCount);
    d->linkCount++;
    return t;
}

static void handle_image(P* p, int tag)
{
    BwDoc* d = p->doc;
    const char* src = attr(p, "src");
    const char* ds = attr(p, "data-src");
    const char* alt = attr(p, "alt");
    Elem* e = top(p);
    BwImage* im;
    BwOp* op;
    int w = attr_int(p, "width", 0), h = attr_int(p, "height", 0);
    (void)tag;
    if (e->hidden || capturing(p)) return;
    if (!ds) ds = attr(p, "data-lazy-src");
    if (!ds) ds = attr(p, "data-original");
    if (ds && ds[0] && (!src || !src[0] || ig_strieq_n(src, "data:", 5))) src = ds;
    if ((w > 0 && w <= 2) || (h > 0 && h <= 2)) return;     /* tracking pixels, spacers */
    if (!src || !src[0]) {
        if (alt && alt[0]) { put_label(p, alt); }
        return;
    }
    if (!grow((void**)&d->images, &d->imageCap, d->imageCount, sizeof(BwImage))) { d->failed = 1; return; }
    im = &d->images[d->imageCount];
    ig_memset(im, 0, sizeof(BwImage));
    if (ig_strieq_n(src, "data:", 5)) im->src = 0;
    else im->src = bw_url_resolve(d->baseUrl, src, ig_strlen(src));
    im->attrW = w;
    im->attrH = h;
    im->target = e->target;
    im->altOff = d->poolLen;
    if (alt) {
        unsigned int i = 0, n = ig_strlen(alt);
        while (i < n) {
            unsigned int c = read_char(BW_CS_UTF8, alt, n, &i, 0, 0);
            if (!pool_put(d, (unsigned short)c)) break;
        }
    }
    im->altLen = d->poolLen - im->altOff;
    if (!im->src) im->failed = 1;
    op = add_op(p, BW_OP_IMAGE);
    if (op) { op->ref = (int)d->imageCount; op->target = e->target; op->font = e->font; op->color = e->color; }
    d->imageCount++;
    p->lastSpace = 0;
}

static void handle_input(P* p)
{
    const char* type = attr(p, "type");
    unsigned int tl = type ? ig_strlen(type) : 0;
    int t = BW_CTL_TEXT, idx;
    BwControl* c;
    Elem* e = top(p);
    if (type) {
        if (word_is(type, tl, "password")) t = BW_CTL_PASSWORD;
        else if (word_is(type, tl, "checkbox")) t = BW_CTL_CHECKBOX;
        else if (word_is(type, tl, "radio")) t = BW_CTL_RADIO;
        else if (word_is(type, tl, "submit")) t = BW_CTL_SUBMIT;
        else if (word_is(type, tl, "reset")) t = BW_CTL_RESET;
        else if (word_is(type, tl, "button")) t = BW_CTL_BUTTON;
        else if (word_is(type, tl, "hidden")) t = BW_CTL_HIDDEN;
        else if (word_is(type, tl, "file")) t = BW_CTL_FILE;
        else if (word_is(type, tl, "image")) t = BW_CTL_IMAGE;
    }
    c = new_control(p, t, &idx);
    if (!c) return;
    set_value_from_attr(c, attr(p, "value"));
    c->checked = c->initialChecked = has_attr(p, "checked");
    c->size = attr_int(p, "size", 20);
    c->maxLength = attr_int(p, "maxlength", 0);
    if (t == BW_CTL_CHECKBOX && !c->value) { unsigned int n; c->value = utf8_to_16("on", &n); c->valueLen = n; }
    if (t == BW_CTL_RADIO && !c->value) { unsigned int n; c->value = utf8_to_16("on", &n); c->valueLen = n; }
    if (t == BW_CTL_IMAGE) {
        const char* alt = attr(p, "alt");
        c->label = utf8_to_16(alt && alt[0] ? alt : "Submit", &c->labelLen);
    }
    if (t == BW_CTL_TEXT) {
        const char* ph = attr(p, "placeholder");
        if (!ph) ph = attr(p, "aria-label");
        if (!ph) ph = attr(p, "title");
        if (ph && ph[0]) c->label = utf8_to_16(ph, &c->labelLen);
    }
    if (t == BW_CTL_HIDDEN || e->hidden || capturing(p)) return;
    c->target = add_target(p, BW_TGT_CONTROL, idx);
    {
        BwOp* op = add_op(p, BW_OP_CONTROL);
        if (op) { op->ref = idx; op->target = c->target; op->font = e->font; }
    }
    p->lastSpace = 0;
}

static void handle_meta(P* p)
{
    const char* he = attr(p, "http-equiv");
    const char* content = attr(p, "content");
    if (he && content && ig_strieq_n(he, "refresh", 7) && !p->doc->refreshUrl) {
        unsigned int i = 0, n = ig_strlen(content);
        int delay = read_num(content, n, &i);
        while (i < n && (content[i] == ';' || content[i] == ',' || content[i] == ' ')) i++;
        if (i + 4 <= n && ig_strieq_n(content + i, "url", 3)) {
            i += 3;
            while (i < n && (content[i] == ' ' || content[i] == '=')) i++;
            if (i < n && (content[i] == '\'' || content[i] == '"')) {
                unsigned int e = n;
                i++;
                if (e > i && (content[e - 1] == '\'' || content[e - 1] == '"')) e--;
                p->doc->refreshUrl = bw_url_resolve(p->doc->baseUrl, content + i, e - i);
            } else p->doc->refreshUrl = bw_url_resolve(p->doc->baseUrl, content + i, n - i);
            p->doc->refreshDelay = delay < 0 ? 0 : delay;
        }
    }
}

/* raw text elements: the end of their content */
static unsigned int find_end_tag(P* p, unsigned int from, const char* name)
{
    unsigned int nl = ig_strlen(name), i;
    for (i = from; i + nl + 2 <= p->n; i++)
        if (p->s[i] == '<' && p->s[i + 1] == '/' && ig_strieq_n(p->s + i + 2, name, nl)) {
            char c = i + 2 + nl < p->n ? p->s[i + 2 + nl] : '>';
            if (c == '>' || is_ws((unsigned char)c) || c == '/') return i;
        }
    return p->n;
}

static unsigned int skip_past_tag(P* p, unsigned int i)
{
    while (i < p->n && p->s[i] != '>') i++;
    return i < p->n ? i + 1 : p->n;
}

static void pop_head(P* p)
{
    if (in_stack(p, T_HEAD)) close_tag(p, T_HEAD, 0);
}

static const int kStopTable[] = { T_TABLE, T_TD, T_TH, T_BUTTON, 0 };
static const int kStopList[] = { T_UL, T_OL, T_MENU, T_DIR, T_TABLE, 0 };
static const int kStopDl[] = { T_DL, T_TABLE, 0 };
static const int kStopRow[] = { T_TABLE, 0 };
static const int kStopCell[] = { T_TR, T_TABLE, 0 };

static void start_tag(P* p, int tag, int selfClose)
{
    Elem* parent;
    Elem* e;
    BwDoc* d = p->doc;

    /* things outside the head start the body */
    if (tag != T_TITLE && tag != T_META && tag != T_LINK && tag != T_STYLE && tag != T_SCRIPT &&
        tag != T_BASE && tag != T_HEAD && tag != T_HTML && tag != T_NOSCRIPT && tag != T_TEMPLATE) pop_head(p);

    /* implied end tags */
    if (closes_p(tag)) close_tag(p, T_P, kStopTable);
    if (tag == T_LI) close_tag(p, T_LI, kStopList);
    if (tag == T_DT || tag == T_DD) { if (!close_tag(p, T_DT, kStopDl)) close_tag(p, T_DD, kStopDl); }
    if (tag == T_TR || tag == T_TBODY || tag == T_THEAD || tag == T_TFOOT) {
        close_tag(p, T_TD, kStopRow); close_tag(p, T_TH, kStopRow);
        close_tag(p, T_TR, kStopRow);
        if (tag != T_TR) { close_tag(p, T_TBODY, kStopRow); close_tag(p, T_THEAD, kStopRow); close_tag(p, T_TFOOT, kStopRow); }
    }
    if (tag == T_TD || tag == T_TH) { close_tag(p, T_TD, kStopCell); close_tag(p, T_TH, kStopCell); }
    if (tag == T_OPTION || tag == T_OPTGROUP) { if (p->capture == 2) close_tag(p, T_OPTION, 0); }
    if (tag >= T_H1 && tag <= T_H6) {
        int t = top(p)->tag;
        if (t >= T_H1 && t <= T_H6) pop(p);
    }
    if (tag == T_A && in_stack(p, T_A)) close_tag(p, T_A, 0);
    if (tag == T_BUTTON && p->capture == 1) close_tag(p, T_BUTTON, 0);
    if (tag == T_FORM && p->curForm >= 0) return;          /* nested forms are not allowed */

    parent = top(p);

    /* elements that are not pushed */
    switch (tag) {
    case T_BR:
        if (!parent->hidden && !capturing(p)) { add_op(p, BW_OP_BR); p->lastSpace = 1; }
        return;
    case T_WBR: return;
    case T_HR:
        if (!parent->hidden && !capturing(p)) { add_op(p, BW_OP_HR); p->lastSpace = 1; }
        return;
    case T_IMG:
        {
            Elem tmp;
            compute_style(p, &tmp, parent, tag);
            if (tmp.hidden) return;
        }
        handle_image(p, tag);
        return;
    case T_INPUT:
        {
            Elem tmp;
            compute_style(p, &tmp, parent, tag);
            if (tmp.hidden && !(attr(p, "type") && ig_strieq_n(attr(p, "type"), "hidden", 6))) {
                /* still part of the form */
                int idx;
                BwControl* c = new_control(p, BW_CTL_HIDDEN, &idx);
                if (c) set_value_from_attr(c, attr(p, "value"));
                if (c && attr(p, "type") && (ig_strieq_n(attr(p, "type"), "checkbox", 8) || ig_strieq_n(attr(p, "type"), "radio", 5)) &&
                    !has_attr(p, "checked")) {
                    /* an unchecked hidden checkbox is not sent */
                    ig_free(c->name); c->name = 0;
                }
                return;
            }
        }
        handle_input(p);
        return;
    case T_META: handle_meta(p); return;
    case T_BASE:
        {
            const char* h = attr(p, "href");
            if (h && h[0]) {
                char* u = bw_url_resolve(d->baseUrl, h, ig_strlen(h));
                if (u) { ig_free(d->baseUrl); d->baseUrl = u; }
            }
        }
        return;
    case T_LINK: case T_PARAM: case T_SOURCE: case T_TRACK: case T_AREA: case T_COL: case T_EMBED:
        return;
    case T_FRAME:
        {
            const char* src = attr(p, "src");
            int t = new_link(p, src);
            if (t >= 0 && !parent->hidden) {
                const char* nm = attr(p, "name");
                Elem* e2;
                if (p->depth < MAX_DEPTH) {
                    e2 = &p->stack[p->depth++];
                    compute_style(p, e2, parent, T_DIV);
                    e2->display = D_BLOCK;
                    emit_block_start(p, e2, 2);
                    e2->target = t;
                    e2->color = d->linkColor;
                    e2->flags |= BW_OPF_UNDERLINE;
                    put_label(p, "[Frame: ");
                    put_label(p, nm && nm[0] ? nm : src);
                    put_label(p, "]");
                    pop(p);
                }
            }
        }
        return;
    default: break;
    }

    if (p->depth >= MAX_DEPTH) return;
    e = &p->stack[p->depth];
    compute_style(p, e, parent, tag);
    p->depth++;

    switch (tag) {
    case T_BODY:
        {
            const char* v;
            p->seenBody = 1;
            v = attr(p, "bgcolor");
            if (v) { int c = parse_color(v, ig_strlen(v)); if (c >= 0) d->bgColor = c; }
            v = attr(p, "text");
            if (v) { int c = parse_color(v, ig_strlen(v)); if (c >= 0) { d->textColor = c; e->color = c; } }
            v = attr(p, "link");
            if (v) { int c = parse_color(v, ig_strlen(v)); if (c >= 0) d->linkColor = c; }
            if (e->bg >= 0) { d->bgColor = e->bg; e->bg = -1; }
            if (e->color != parent->color) d->textColor = e->color;
        }
        break;
    case T_HTML:
        if (e->bg >= 0) { d->bgColor = e->bg; e->bg = -1; }
        break;
    case T_A:
        {
            const char* href = attr(p, "href");
            const char* nm = attr(p, "name");
            if (nm) add_anchor(p, nm);
            if (href && !e->hidden) {
                int t = new_link(p, href);
                if (t >= 0) {
                    e->target = t;
                    /* a:link color from CSS wins; otherwise the page's link color */
                    {
                        int linkRuleColor = 0;
                        unsigned int r;
                        for (r = 0; r < p->ruleCount; r++)
                            if (p->rules[r].tag == T_A && !p->rules[r].classes && !p->rules[r].id && (p->rules[r].props.mask & CSS_COLOR)) linkRuleColor = 1;
                        if (!linkRuleColor && !(attr(p, "style") && contains_ci(attr(p, "style"), ig_strlen(attr(p, "style")), "color"))) {
                            /* keep a color the parent (font tag) set only if the class rules set it */
                            int classColor = 0;
                            const char* cls = attr(p, "class");
                            if (cls) {
                                for (r = 0; r < p->ruleCount; r++)
                                    if (p->rules[r].classes && (p->rules[r].props.mask & CSS_COLOR) && (!p->rules[r].tag || p->rules[r].tag == T_A)) {
                                        const char* c = p->rules[r].classes;
                                        int ok = 1;
                                        while (*c) { if (!has_class(cls, c)) { ok = 0; break; } c += ig_strlen(c) + 1; }
                                        if (ok) classColor = 1;
                                    }
                            }
                            if (!classColor) e->color = d->linkColor;
                        }
                    }
                    if (!(e->flags & BW_OPF_UNDERLINE)) {
                        /* underline unless a rule says text-decoration: none */
                        int noUnderline = 0;
                        unsigned int r;
                        for (r = 0; r < p->ruleCount; r++)
                            if ((!p->rules[r].tag || p->rules[r].tag == T_A) && (p->rules[r].props.mask & CSS_UNDERLINE) &&
                                !p->rules[r].props.underline && !p->rules[r].id && !p->rules[r].classes) noUnderline = 1;
                        if (!noUnderline) e->flags |= BW_OPF_UNDERLINE;
                    }
                }
            }
        }
        break;
    case T_FORM:
        {
            const char* action = attr(p, "action");
            const char* method = attr(p, "method");
            if (grow((void**)&d->forms, &d->formCap, d->formCount, sizeof(BwForm))) {
                BwForm* f = &d->forms[d->formCount];
                f->action = bw_url_resolve(d->baseUrl, action ? action : "", action ? ig_strlen(action) : 0);
                f->post = method && ig_strieq_n(method, "post", 4);
                p->curForm = (int)d->formCount++;
            }
        }
        break;
    case T_BUTTON:
        {
            const char* type = attr(p, "type");
            int ct = BW_CTL_SUBMIT, idx;
            BwControl* c;
            if (type && ig_strieq_n(type, "button", 6)) ct = BW_CTL_BUTTON;
            else if (type && ig_strieq_n(type, "reset", 5)) ct = BW_CTL_RESET;
            c = new_control(p, ct, &idx);
            if (c) {
                set_value_from_attr(c, attr(p, "value"));
                if (!e->hidden && !capturing(p)) c->target = add_target(p, BW_TGT_CONTROL, idx);
                start_capture(p, 1, idx);
            }
        }
        break;
    case T_SELECT:
        {
            int idx;
            BwControl* c = new_control(p, BW_CTL_SELECT, &idx);
            if (c) {
                p->curSelect = idx;
                if (!e->hidden && !capturing(p)) c->target = add_target(p, BW_TGT_CONTROL, idx);
            }
        }
        break;
    case T_OPTION:
        if (p->curSelect >= 0) {
            const char* v = attr(p, "value");
            ig_free(p->optValue);
            p->optValue = 0;
            if (v) p->optValue = utf8_to_16(v, &p->optValueLen);
            p->optSelected = has_attr(p, "selected");
            start_capture(p, 2, p->curSelect);
        }
        break;
    case T_IFRAME:
        {
            const char* src = attr(p, "src");
            int w = attr_int(p, "width", 100), h = attr_int(p, "height", 100);
            if (src && src[0] && w > 2 && h > 2 && !parent->hidden && !capturing(p)) {
                int t = new_link(p, src);
                if (t >= 0) {
                    const char* title = attr(p, "title");
                    Elem saved = *e;
                    e->hidden = 0;
                    e->target = t;
                    e->color = d->linkColor;
                    e->flags |= BW_OPF_UNDERLINE;
                    put_label(p, "[Frame");
                    if (title && title[0]) { put_label(p, ": "); put_label(p, title); }
                    put_label(p, "]");
                    *e = saved;
                }
            }
            e->hidden = 1;   /* the content is the fallback for old browsers */
        }
        break;
    case T_VIDEO: case T_AUDIO:
        {
            const char* src = attr(p, "src");
            if (src && src[0] && !parent->hidden && !capturing(p)) {
                int t = new_link(p, src);
                if (t >= 0) {
                    Elem saved = *e;
                    e->hidden = 0; e->target = t; e->color = d->linkColor; e->flags |= BW_OPF_UNDERLINE;
                    put_label(p, tag == T_VIDEO ? "[Video]" : "[Audio]");
                    *e = saved;
                }
            }
        }
        break;
    default: break;
    }

    /* blocks */
    if (e->display == D_BLOCK || e->display == D_LISTITEM || e->display == D_ROW) {
        int depthLists = parent->listDepth;
        int m = margin_top(tag, depthLists);
        emit_block_start(p, e, m);
        e->marginBottom = m;
        if (e->display == D_LISTITEM && e->blockOpen) {
            /* the marker */
            BwOp* op = add_op(p, BW_OP_MARKER);
            if (op) {
                int k;
                Elem* list = 0;
                for (k = p->depth - 2; k >= 1; k--) if (p->stack[k].listKind) { list = &p->stack[k]; break; }
                op->font = e->font & ~BW_FONT_ITALIC;
                op->color = e->color;
                op->off = d->poolLen;
                if (list && list->listKind == 2) {
                    char num[12];
                    int v = attr_int(p, "value", list->listCounter), j;
                    list->listCounter = v + 1;
                    ig_itoa(v, num);
                    for (j = 0; num[j]; j++) pool_put(d, (unsigned short)num[j]);
                    pool_put(d, '.');
                } else {
                    int lvl = list ? list->listDepth : 1;
                    pool_put(d, (unsigned short)(lvl <= 1 ? 0x2022 : lvl == 2 ? 0x25E6 : 0x00B7));
                }
                op->len = d->poolLen - op->off;
            }
        }
    } else if (e->display == D_CELL && !e->hidden && !capturing(p)) {
        add_op(p, BW_OP_CELL);
        p->lastSpace = 1;
    }
    if (tag == T_DT && e->blockOpen) e->font |= BW_FONT_BOLD;

    {
        const char* id = attr(p, "id");
        if (id && tag != T_A) add_anchor(p, id);
    }

    if (selfClose && tag != T_SCRIPT && tag != T_STYLE && tag != T_TEXTAREA && tag != T_TITLE) pop(p);
}

static void end_tag(P* p, int tag)
{
    switch (tag) {
    case T_BR:
        if (!top(p)->hidden && !capturing(p)) { add_op(p, BW_OP_BR); p->lastSpace = 1; }
        return;
    case T_P:
        if (!close_tag(p, T_P, kStopTable)) {
            /* "</p>" alone acts as <p></p> */
        }
        return;
    case T_LI: close_tag(p, T_LI, kStopList); return;
    case T_TD: case T_TH: close_tag(p, tag, kStopCell); return;
    case T_TR: close_tag(p, T_TR, kStopRow); return;
    case T_BODY: case T_HTML: return;        /* content after </body> still counts */
    case T_HEAD: close_tag(p, T_HEAD, 0); return;
    case T_FORM:
        if (!close_tag(p, T_FORM, 0)) p->curForm = -1;
        return;
    default:
        {
            static const int stops[] = { T_TABLE, 0 };
            if (tag == T_TABLE) close_tag(p, T_TABLE, 0);
            else if (!close_tag(p, tag, (tag == T_DIV || tag == T_SPAN) ? 0 : stops)) {}
        }
        return;
    }
}

/* ------------------------------------------------------------------ the main loop */

static void raw_text_element(P* p, int tag, unsigned int* ip)
{
    const char* name = kTagNames[tag];
    unsigned int st = *ip;
    unsigned int e = find_end_tag(p, st, name);
    if (tag == T_STYLE) {
        if (!top(p)->hidden || in_stack(p, T_HEAD) || 1) {
            const char* media = attr(p, "media");
            if (!media || media_matches(media, ig_strlen(media))) parse_css(p, p->s + st, e - st, 0);
        }
    } else if (tag == T_TITLE) {
        if (!p->doc->title) {
            IgBuf b;
            unsigned int i = st;
            int sp = 1;
            ig_buf_init(&b);
            while (i < e) {
                unsigned int c = read_char(p->cs, p->s, e, &i, 1, 0);
                unsigned short u;
                if (is_ws((int)c)) { if (sp) continue; c = ' '; sp = 1; } else sp = 0;
                u = (unsigned short)c;
                ig_buf_append(&b, &u, 2);
            }
            while (b.len >= 2 && ((unsigned short*)b.data)[b.len / 2 - 1] == ' ') b.len -= 2;
            if (b.len && !b.failed) {
                p->doc->title = dup16((unsigned short*)b.data, b.len / 2);
                p->doc->titleLen = p->doc->title ? b.len / 2 : 0;
            }
            ig_buf_free(&b);
        }
    } else if (tag == T_TEXTAREA) {
        int idx;
        BwControl* c = new_control(p, BW_CTL_TEXTAREA, &idx);
        Elem* t = top(p);
        if (c) {
            IgBuf b;
            unsigned int i = st;
            ig_buf_init(&b);
            if (i < e && p->s[i] == '\r') i++;
            if (i < e && p->s[i] == '\n') i++;
            while (i < e) {
                unsigned int ch = read_char(p->cs, p->s, e, &i, 1, 0);
                unsigned short u = (unsigned short)ch;
                if (ch == '\r') continue;
                ig_buf_append(&b, &u, 2);
            }
            if (b.len && !b.failed) { c->value = dup16((unsigned short*)b.data, b.len / 2); c->valueLen = c->value ? b.len / 2 : 0; }
            ig_buf_free(&b);
            c->size = attr_int(p, "rows", 3);
            c->cols = attr_int(p, "cols", 40);
            if (!t->hidden && !capturing(p)) {
                BwOp* op;
                c->target = add_target(p, BW_TGT_CONTROL, idx);
                op = add_op(p, BW_OP_CONTROL);
                if (op) { op->ref = idx; op->target = c->target; op->font = t->font; }
                p->lastSpace = 0;
            }
        }
    } else if (tag == T_PLAINTEXT) {
        put_text(p, p->s + st, p->n - st);
        *ip = p->n;
        return;
    } else if (tag == T_XMP) {
        /* shown, but as text: no tags inside */
        put_text(p, p->s + st, e - st);
    }
    *ip = e < p->n ? skip_past_tag(p, e) : p->n;
    if (tag == T_TEXTAREA || tag == T_XMP) {
        /* those were pushed (not void): pop */
        if (top(p)->tag == tag) pop(p);
    } else if (top(p)->tag == tag) pop(p);
}

static void run(P* p)
{
    const char* s = p->s;
    unsigned int n = p->n, i = 0;
    while (i < n) {
        unsigned int ts = i;
        if (s[i] != '<') {
            while (i < n && s[i] != '<') i++;
            /* non-space text outside the head ends it */
            {
                unsigned int k;
                for (k = ts; k < i; k++) if (!is_ws((unsigned char)s[k])) { pop_head(p); break; }
            }
            put_text(p, s + ts, i - ts);
            continue;
        }
        /* a tag, comment, doctype, or a lone '<' */
        if (i + 3 < n && s[i + 1] == '!' && s[i + 2] == '-' && s[i + 3] == '-') {
            unsigned int k = i + 4;
            while (k + 2 < n && !(s[k] == '-' && s[k + 1] == '-' && s[k + 2] == '>')) k++;
            i = k + 2 < n ? k + 3 : n;
            continue;
        }
        if (i + 1 < n && (s[i + 1] == '!' || s[i + 1] == '?')) {
            /* <![CDATA[ ... ]]> is text in foreign content; elsewhere a bogus comment */
            if (i + 8 < n && ig_memcmp(s + i, "<![CDATA[", 9) == 0) {
                unsigned int k = i + 9;
                while (k + 2 < n && !(s[k] == ']' && s[k + 1] == ']' && s[k + 2] == '>')) k++;
                i = k + 2 < n ? k + 3 : n;
                continue;
            }
            i = skip_past_tag(p, i + 1);
            continue;
        }
        {
            int isEnd = 0, tag, selfClose;
            unsigned int ns, ne, k;
            k = i + 1;
            if (k < n && s[k] == '/') { isEnd = 1; k++; }
            ns = k;
            while (k < n && (is_alnum((unsigned char)s[k]) || s[k] == '-' || s[k] == ':' || s[k] == '_')) k++;
            ne = k;
            if (ne == ns || !((s[ns] >= 'a' && s[ns] <= 'z') || (s[ns] >= 'A' && s[ns] <= 'Z'))) {
                if (isEnd) { i = skip_past_tag(p, i + 1); continue; }   /* "</ >" */
                /* a '<' that starts no tag is text */
                put_text(p, s + i, 1);
                i++;
                continue;
            }
            {
                unsigned int m;
                unsigned int len = ne - ns;
                if (len >= sizeof(p->tagName)) len = sizeof(p->tagName) - 1;
                for (m = 0; m < len; m++) p->tagName[m] = (char)lower((unsigned char)s[ns + m]);
                p->tagName[len] = 0;
                tag = (ne - ns) < sizeof(p->tagName) ? lookup_tag(p->tagName, len) : T_UNKNOWN;
            }
            i = ne;
            read_attrs(p, &i, &selfClose);
            if (isEnd) { if (tag) end_tag(p, tag); continue; }
            if (!tag) continue;               /* custom elements: their content still shows */
            if (is_void(tag)) selfClose = 1;
            if (tag == T_SCRIPT || tag == T_TEMPLATE || tag == T_NOEMBED) {
                i = find_end_tag(p, i, kTagNames[tag]);
                i = i < n ? skip_past_tag(p, i) : n;
                continue;
            }
            if (tag == T_SVG || tag == T_MATH) {
                if (!selfClose) {
                    i = find_end_tag(p, i, kTagNames[tag]);
                    i = i < n ? skip_past_tag(p, i) : n;
                }
                continue;
            }
            if (tag == T_STYLE || tag == T_TITLE || tag == T_TEXTAREA || tag == T_XMP || tag == T_PLAINTEXT) {
                if (tag == T_TEXTAREA || tag == T_XMP || tag == T_PLAINTEXT) {
                    start_tag(p, tag, 0);
                } else {
                    /* style / title: not pushed, handled here */
                    if (tag == T_STYLE) {
                        /* (attr() still sees this tag's attributes) */
                    }
                }
                if (tag == T_TEXTAREA && top(p)->tag != T_TEXTAREA) {
                    /* not pushed (depth): just skip the content */
                    i = find_end_tag(p, i, "textarea");
                    i = i < n ? skip_past_tag(p, i) : n;
                    continue;
                }
                raw_text_element(p, tag, &i);
                continue;
            }
            start_tag(p, tag, selfClose);
        }
    }
    while (p->depth > 1) pop(p);
    if (p->capture) finish_capture(p);
}

/* ------------------------------------------------------------------ public */

static BwDoc* new_doc(const char* url, int cs)
{
    BwDoc* d = (BwDoc*)ig_malloc(sizeof(BwDoc));
    if (!d) return 0;
    ig_memset(d, 0, sizeof(BwDoc));
    d->baseUrl = ig_strdup(url ? url : "");
    d->charset = cs;
    d->bgColor = 0xFFFFFF;
    d->textColor = 0x000000;
    d->linkColor = 0x0000CC;
    if (!d->baseUrl) { ig_free(d); return 0; }
    return d;
}

static void init_parser(P* p, BwDoc* d, const char* data, unsigned int len, int cs)
{
    Elem* root;
    ig_memset(p, 0, sizeof(P));
    p->doc = d;
    p->s = data;
    p->n = len;
    p->cs = cs;
    p->curForm = -1;
    p->curSelect = -1;
    p->captureCtl = -1;
    ig_buf_init(&p->attrBuf);
    ig_buf_init(&p->cap16);
    root = &p->stack[0];
    ig_memset(root, 0, sizeof(Elem));
    root->font = BW_FONT_MAKE(1, 0);
    root->color = d->textColor;
    root->bg = -1;
    root->target = -1;
    root->display = D_BLOCK;
    p->depth = 1;
    p->lastSpace = 1;
}

static void done_parser(P* p)
{
    ig_buf_free(&p->attrBuf);
    ig_buf_free(&p->cap16);
    ig_free(p->optValue);
    free_rules(p);
}

BwDoc* bw_doc_parse(const char* data, unsigned int len, const char* url, const char* contentType, int cs)
{
    BwDoc* d;
    P* p;
    if (cs < 0) cs = bw_charset_sniff(contentType, data, len);
    if (cs < 0) {
        /* no charset named: UTF-8 if the bytes are valid UTF-8, else windows-1250 (Czech pages) */
        unsigned int i = 0, bad = 0, multi = 0;
        while (i < len && i < 65536) {
            unsigned char c = (unsigned char)data[i];
            if (c < 0x80) { i++; continue; }
            if ((c & 0xE0) == 0xC0 && i + 1 < len && ((unsigned char)data[i + 1] & 0xC0) == 0x80) { i += 2; multi++; }
            else if ((c & 0xF0) == 0xE0 && i + 2 < len && ((unsigned char)data[i + 1] & 0xC0) == 0x80 && ((unsigned char)data[i + 2] & 0xC0) == 0x80) { i += 3; multi++; }
            else if ((c & 0xF8) == 0xF0 && i + 3 < len) { i += 4; multi++; }
            else { bad++; i++; }
        }
        cs = (bad == 0 || multi > bad * 4) ? BW_CS_UTF8 : BW_CS_1250;
    }
    if (len >= 3 && cs == BW_CS_UTF8 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF) {
        data += 3; len -= 3;
    }
    d = new_doc(url, cs);
    if (!d) return 0;
    p = (P*)ig_malloc(sizeof(P));
    if (!p) { bw_doc_free(d); return 0; }
    init_parser(p, d, data, len, cs);
    run(p);
    done_parser(p);
    ig_free(p);
    return d;
}

BwDoc* bw_doc_plain(const char* data, unsigned int len, const char* url, int cs)
{
    BwDoc* d;
    P* p;
    if (cs < 0) cs = bw_charset_sniff(0, data, 0);
    if (cs < 0) cs = BW_CS_UTF8;
    d = new_doc(url, cs);
    if (!d) return 0;
    p = (P*)ig_malloc(sizeof(P));
    if (!p) { bw_doc_free(d); return 0; }
    init_parser(p, d, data, len, cs);
    p->stack[0].font = BW_FONT_MAKE(1, BW_FONT_MONO);
    p->stack[0].flags = BW_OPF_PRE;
    {
        unsigned int i = 0;
        while (i < len) {
            unsigned int c = read_char(cs, data, len, &i, 0, 0);
            put_text_char(p, c, &p->stack[0]);
        }
    }
    done_parser(p);
    ig_free(p);
    return d;
}

void bw_doc_free(BwDoc* d)
{
    unsigned int i, k;
    if (!d) return;
    ig_free(d->ops);
    ig_free(d->pool);
    for (i = 0; i < d->linkCount; i++) ig_free(d->links[i].href);
    ig_free(d->links);
    for (i = 0; i < d->imageCount; i++) ig_free(d->images[i].src);
    ig_free(d->images);
    for (i = 0; i < d->controlCount; i++) {
        BwControl* c = &d->controls[i];
        ig_free(c->name); ig_free(c->value); ig_free(c->label);
        for (k = 0; k < c->optionCount; k++) { ig_free(c->options[k].label); ig_free(c->options[k].value); }
        ig_free(c->options);
    }
    ig_free(d->controls);
    for (i = 0; i < d->formCount; i++) ig_free(d->forms[i].action);
    ig_free(d->forms);
    for (i = 0; i < d->anchorCount; i++) ig_free(d->anchors[i].name);
    ig_free(d->anchors);
    ig_free(d->targets);
    ig_free(d->title);
    ig_free(d->baseUrl);
    ig_free(d->refreshUrl);
    ig_free(d);
}

int bw_doc_find_anchor(const BwDoc* d, const char* name)
{
    unsigned int i;
    if (!name) return -1;
    for (i = 0; i < d->anchorCount; i++) if (ig_streq(d->anchors[i].name, name)) return (int)i;
    return -1;
}
