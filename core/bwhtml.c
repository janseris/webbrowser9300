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

enum { D_INLINE = 0, D_BLOCK, D_LISTITEM, D_NONE, D_CELL, D_ROW, D_TABLE, D_ROWGROUP };

#define MAX_DEPTH 160
/* a character not to show at all (outside the 16-bit range: emoji) */
#define SKIP_CHAR 0x110000u
#define MAX_ATTRS 40
#define DEVICE_WIDTH 640

typedef struct {
    unsigned char tag;
    unsigned char display;
    unsigned char font;
    unsigned char flags;          /* BW_OPF_* */
    unsigned char align;
    unsigned char hidden;
    unsigned char blockOpen;      /* what went out for it: 1 BLOCK_START, 2 TABLE_START, 3 ROW_START, 4 CELL_START */
    unsigned char listKind;       /* this element is a list: 1 ul, 2 ol */
    int color;
    int bg;
    int target;
    int listCounter;              /* ol: the next number */
    int listDepth;                /* how many lists are open around (for the bullet) */
    int marginBottom;
    int px100;                    /* the font size in px * 100 (16 px = normal) */
    unsigned char listStyle;      /* inherited: 0 by the list's tag, else LS_* */
    unsigned char olType;         /* <ol type>: LS_* (0 decimal) */
    unsigned char transform;      /* inherited text-transform: 0 none, 1 upper, 2 lower, 3 capitalize */
    unsigned char isFloat;
    unsigned char isFlex, flexCol;   /* a flex / grid container (not inherited) */
    unsigned char flexItem;          /* a block in a flex row: flows inline after a gap */
    unsigned int afterOff;           /* ::after text in P.ancBuf (~0u none) */
    int labelIndex;                  /* a <label>: its doc label, -1 */
    int num;                         /* its number in the document (BwDoc.elemParent), -1 the root */
    int firstControl;                /* a <label>: the controls made inside it start here */
    int ibg;                      /* the background of an inline element around the text (-1) */
    int childCount;               /* element children opened so far (for :nth-child) */
    int childIndex;               /* this element's place among its siblings, from 1 */
    unsigned int opStart;         /* the op count when it opened (icon-only links) */
    unsigned int labelOff;        /* aria-label / title in P.ancBuf (~0u none) */
    /* the CSS box (not inherited), px after scaling for the small screen */
    short mt, mb, ml, mr, pt, pb, pl, pr;
    short width, maxw;            /* px, < 0: percent of the space there is, 0 none */
    unsigned char center;         /* margin-left and -right auto */
    unsigned char autoSides;      /* 1 right, 2 left margin is auto */
    short imgW, imgH;             /* CSS width / height in px (pictures) */
    unsigned char bw[4];          /* border widths: top right bottom left */
    int bc;                       /* border color */
    /* for descendant selectors: the id and class attribute in P.ancBuf (offsets, ~0u none) */
    unsigned int bufStart, idOff, clsOff;
    unsigned int bloom;
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
#define CSS_BOX       0x1000
#define CSS_LIST      0x2000
#define CSS_FLOAT     0x4000
#define CSS_TRANSFORM 0x8000
#define CSS_IMGSIZE   0x10000
#define CSS_FLEXDIR   0x20000

/* list-style-type */
enum { LS_AUTO = 0, LS_NONE, LS_DISC, LS_CIRCLE, LS_SQUARE, LS_DECIMAL, LS_LALPHA, LS_UALPHA, LS_LROMAN, LS_UROMAN };

/* a CSS length: v = value * 100; unit 0 px, 1 em, 2 %, 3 auto */
typedef struct { int v; unsigned char unit; } CssLen;

/* box fields set (boxMask) */
#define BX_M(side)   (1u << (side))          /* margins: 0 top, 1 right, 2 bottom, 3 left */
#define BX_P(side)   (1u << (4 + (side)))    /* paddings */
#define BX_B(side)   (1u << (8 + (side)))    /* border widths */
#define BX_BC        (1u << 12)
#define BX_W         (1u << 13)
#define BX_MAXW      (1u << 14)

typedef struct {
    unsigned int mask;
    unsigned char display, bold, italic, align, underline, sizeRel, hidden, pre, mono, strike;
    unsigned char listStyle, isFloat, transform;
    int sizeVal;                   /* px * 100, or (sizeRel) a factor * 10000 */
    int color, bg;
    unsigned int boxMask;
    CssLen m[4], pd[4], width, maxw, height;
    unsigned char bw[4];
    int bc;
    char* content;                 /* ::before / ::after text (UTF-8, allocated by the reader); NULL none */
    unsigned char contentAttr;     /* content is attr(name): the name */
    unsigned char flexRow;         /* with CSS_DISPLAY: display flex / grid */
    unsigned char flexCol;         /* with CSS_FLEXDIR: flex-direction column */
} CssProps;

/* an attribute condition: [name], [name=v], ~= ^= $= *= |= */
enum { AT_HAS = 0, AT_EQ, AT_WORD, AT_PREFIX, AT_SUFFIX, AT_SUB, AT_DASH };
typedef struct {
    char* name;
    char* val;
    unsigned char op;
    unsigned char negate;          /* inside :not() */
} CssAttrCond;

#define MAX_ATTR_CONDS 3

/* one compound selector: tag#id.class.class[attr]:pseudo */
typedef struct {
    int tag;                       /* 0 any */
    char* id;                      /* NULL any */
    char* classes;                 /* NUL-separated, ends with an empty one; NULL none */
    unsigned int bloom;            /* bits of the tag / id / classes it needs (a quick reject) */
    CssAttrCond attr[MAX_ATTR_CONDS];   /* only on the subject (ancestors keep no attributes) */
    unsigned char attrCount;
    unsigned char hasNth;          /* :first-child / :nth-child(An+B) */
    unsigned char hover;           /* :hover */
    short nthA, nthB;
    int notTag;                    /* :not(tag) */
    char* notClass;                /* :not(.x) */
    char* notId;                   /* :not(#x) */
} CssCompound;

typedef struct {
    /* the subject (the rightmost compound) */
    CssCompound subj;
    /* the compounds to its left, nearest first, and how each joins: 1 child (>), 0 descendant */
    CssCompound* anc;
    unsigned char* ancChild;
    unsigned int ancCount;
    int spec;                      /* ids * 10000 + classes * 100 + tags */
    CssProps props;
    CssProps* imp;                 /* the !important declarations, NULL none */
    unsigned char pseudo;          /* 1 ::before, 2 ::after (only its content counts) */
    char* content;
    unsigned char contentAttr;
    unsigned int bucket;           /* rule_bucket(), worked out once when it is first indexed */
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
    IgBuf ancBuf;                  /* the open elements' id / class values */
    unsigned int lastCssMask;      /* what the CSS set on the element just styled */
    /* custom properties (--x) from :root / html / body, for var() */
    char* varName[64];
    char* varVal[64];
    int varCount;
    int allowVars;                 /* the rule being read may define them */
    char* picSrc;                  /* <picture>: the srcset of the <source> chosen for its <img> */
    int curChildIndex;             /* the new element's place among its siblings */
    const CssRule* pseudoRule[2];  /* the ::before / ::after rule of the element just styled */
    int curElemNum;                /* the number of the element being styled */
    const int* hover;              /* the elements under the pointer (innermost first) */
    unsigned int hoverN;
    int full;                      /* BW_PARSE_FULLSIZE: desktop lengths as they are */
    int inlineCss;                 /* parsing a <style> block (its @import counts) */
    /* rules by the key of their subject: bucket b holds idx[start[b] .. start[b+1]) */
    unsigned int* idx;
    unsigned int* start;
    unsigned int indexedRules;     /* ruleCount when the index was built */
} P;

/* ------------------------------------------------------------------ small helpers */
static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
/* (a macro: GCC 2.9 -O2 doesn't inline, and a call a character was 16 % of a Wikipedia parse)
 * space, \t \n \f \r (9, 10, 12, 13; not \v); the argument has no side effects anywhere */
#define is_ws(c) ((c) == ' ' || ((unsigned int)((c) - 9) < 5u && (c) != 11))
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
    op->bg = -1;
    op->elem = p->depth > 0 ? p->stack[p->depth - 1].num : -1;
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
        if (v > 0xFFFF && v <= 0x10FFFF) return SKIP_CHAR;     /* emoji and the like: no font has them */
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

/* characters that only steer the text (soft hyphen, zero-width spaces, direction marks,
 * variation selectors, BOM): not drawn */
static int invisible(unsigned int c)
{
    if (c < 0xAD && c != SKIP_CHAR) return 0;      /* (nearly every character: one compare) */
    return c == SKIP_CHAR || c == 0xAD || (c >= 0x200B && c <= 0x200F) || (c >= 0x2060 && c <= 0x2064) ||
           c == 0xFEFF || c == 0xFE0E || c == 0xFE0F || (c >= 0x202A && c <= 0x202E) || c == 0x034F;
}

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
        if (cp > 0xFFFF) return SKIP_CHAR;
        if (cp >= 0xD800 && cp < 0xE000) return 0xFFFD;
        return cp;
    }
}

/* ------------------------------------------------------------------ text output */

static Elem* top(P* p) { return &p->stack[p->depth - 1]; }

static int capturing(P* p) { return p->capture != 0; }

/* upper / lower case for the scripts the phone's fonts have (Latin, Greek, Cyrillic) */
static unsigned int to_upper(unsigned int c)
{
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c < 0xE0) return c;
    if (c <= 0xFE && c != 0xF7) return c - 32;
    if (c == 0xFF) return 0x178;
    if (c >= 0x100 && c <= 0x137) return c & ~1u;
    if (c >= 0x139 && c <= 0x148) return (c & 1) ? c : c - 1;
    if (c >= 0x14A && c <= 0x177) return c & ~1u;
    if (c >= 0x179 && c <= 0x17E) return (c & 1) ? c : c - 1;
    if (c >= 0x3B1 && c <= 0x3C9 && c != 0x3C2) return c - 32;
    if (c >= 0x430 && c <= 0x44F) return c - 32;
    if (c >= 0x450 && c <= 0x45F) return c - 80;
    return c;
}

static unsigned int to_lower(unsigned int c)
{
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c < 0xC0) return c;
    if (c <= 0xDE && c != 0xD7) return c + 32;
    if (c == 0x178) return 0xFF;
    if (c >= 0x100 && c <= 0x137) return c | 1u;
    if (c >= 0x139 && c <= 0x148) return (c & 1) ? c + 1 : c;
    if (c >= 0x14A && c <= 0x177) return c | 1u;
    if (c >= 0x179 && c <= 0x17E) return (c & 1) ? c + 1 : c;
    if (c >= 0x391 && c <= 0x3A9) return c + 32;
    if (c >= 0x410 && c <= 0x42F) return c + 32;
    if (c >= 0x400 && c <= 0x40F) return c + 80;
    return c;
}

static void put_text_char(P* p, unsigned int c, const Elem* e)
{
    BwDoc* d = p->doc;
    BwOp* last;
    if (invisible(c)) return;
    if (e->transform && !is_ws((int)c)) {
        if (e->transform == 1) c = to_upper(c);
        else if (e->transform == 2) c = to_lower(c);
        else if (p->lastSpace) c = to_upper(c);
    }
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
    if (!(last && last->type == BW_OP_TEXT && last->font == e->font && last->color == e->color && last->bg == e->ibg &&
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
        last->bg = e->ibg;
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
    { "aliceblue", 0xF0F8FF },
    { "antiquewhite", 0xFAEBD7 },
    { "aqua", 0x00FFFF },
    { "aquamarine", 0x7FFFD4 },
    { "azure", 0xF0FFFF },
    { "beige", 0xF5F5DC },
    { "bisque", 0xFFE4C4 },
    { "black", 0x000000 },
    { "blanchedalmond", 0xFFEBCD },
    { "blue", 0x0000FF },
    { "blueviolet", 0x8A2BE2 },
    { "brown", 0xA52A2A },
    { "burlywood", 0xDEB887 },
    { "cadetblue", 0x5F9EA0 },
    { "chartreuse", 0x7FFF00 },
    { "chocolate", 0xD2691E },
    { "coral", 0xFF7F50 },
    { "cornflowerblue", 0x6495ED },
    { "cornsilk", 0xFFF8DC },
    { "crimson", 0xDC143C },
    { "cyan", 0x00FFFF },
    { "darkblue", 0x00008B },
    { "darkcyan", 0x008B8B },
    { "darkgoldenrod", 0xB8860B },
    { "darkgray", 0xA9A9A9 },
    { "darkgreen", 0x006400 },
    { "darkgrey", 0xA9A9A9 },
    { "darkkhaki", 0xBDB76B },
    { "darkmagenta", 0x8B008B },
    { "darkolivegreen", 0x556B2F },
    { "darkorange", 0xFF8C00 },
    { "darkorchid", 0x9932CC },
    { "darkred", 0x8B0000 },
    { "darksalmon", 0xE9967A },
    { "darkseagreen", 0x8FBC8F },
    { "darkslateblue", 0x483D8B },
    { "darkslategray", 0x2F4F4F },
    { "darkslategrey", 0x2F4F4F },
    { "darkturquoise", 0x00CED1 },
    { "darkviolet", 0x9400D3 },
    { "deeppink", 0xFF1493 },
    { "deepskyblue", 0x00BFFF },
    { "dimgray", 0x696969 },
    { "dimgrey", 0x696969 },
    { "dodgerblue", 0x1E90FF },
    { "firebrick", 0xB22222 },
    { "floralwhite", 0xFFFAF0 },
    { "forestgreen", 0x228B22 },
    { "fuchsia", 0xFF00FF },
    { "gainsboro", 0xDCDCDC },
    { "ghostwhite", 0xF8F8FF },
    { "gold", 0xFFD700 },
    { "goldenrod", 0xDAA520 },
    { "gray", 0x808080 },
    { "green", 0x008000 },
    { "greenyellow", 0xADFF2F },
    { "grey", 0x808080 },
    { "honeydew", 0xF0FFF0 },
    { "hotpink", 0xFF69B4 },
    { "indianred", 0xCD5C5C },
    { "indigo", 0x4B0082 },
    { "ivory", 0xFFFFF0 },
    { "khaki", 0xF0E68C },
    { "lavender", 0xE6E6FA },
    { "lavenderblush", 0xFFF0F5 },
    { "lawngreen", 0x7CFC00 },
    { "lemonchiffon", 0xFFFACD },
    { "lightblue", 0xADD8E6 },
    { "lightcoral", 0xF08080 },
    { "lightcyan", 0xE0FFFF },
    { "lightgoldenrodyellow", 0xFAFAD2 },
    { "lightgray", 0xD3D3D3 },
    { "lightgreen", 0x90EE90 },
    { "lightgrey", 0xD3D3D3 },
    { "lightpink", 0xFFB6C1 },
    { "lightsalmon", 0xFFA07A },
    { "lightseagreen", 0x20B2AA },
    { "lightskyblue", 0x87CEFA },
    { "lightslategray", 0x778899 },
    { "lightslategrey", 0x778899 },
    { "lightsteelblue", 0xB0C4DE },
    { "lightyellow", 0xFFFFE0 },
    { "lime", 0x00FF00 },
    { "limegreen", 0x32CD32 },
    { "linen", 0xFAF0E6 },
    { "magenta", 0xFF00FF },
    { "maroon", 0x800000 },
    { "mediumaquamarine", 0x66CDAA },
    { "mediumblue", 0x0000CD },
    { "mediumorchid", 0xBA55D3 },
    { "mediumpurple", 0x9370DB },
    { "mediumseagreen", 0x3CB371 },
    { "mediumslateblue", 0x7B68EE },
    { "mediumspringgreen", 0x00FA9A },
    { "mediumturquoise", 0x48D1CC },
    { "mediumvioletred", 0xC71585 },
    { "midnightblue", 0x191970 },
    { "mintcream", 0xF5FFFA },
    { "mistyrose", 0xFFE4E1 },
    { "moccasin", 0xFFE4B5 },
    { "navajowhite", 0xFFDEAD },
    { "navy", 0x000080 },
    { "oldlace", 0xFDF5E6 },
    { "olive", 0x808000 },
    { "olivedrab", 0x6B8E23 },
    { "orange", 0xFFA500 },
    { "orangered", 0xFF4500 },
    { "orchid", 0xDA70D6 },
    { "palegoldenrod", 0xEEE8AA },
    { "palegreen", 0x98FB98 },
    { "paleturquoise", 0xAFEEEE },
    { "palevioletred", 0xDB7093 },
    { "papayawhip", 0xFFEFD5 },
    { "peachpuff", 0xFFDAB9 },
    { "peru", 0xCD853F },
    { "pink", 0xFFC0CB },
    { "plum", 0xDDA0DD },
    { "powderblue", 0xB0E0E6 },
    { "purple", 0x800080 },
    { "rebeccapurple", 0x663399 },
    { "red", 0xFF0000 },
    { "rosybrown", 0xBC8F8F },
    { "royalblue", 0x4169E1 },
    { "saddlebrown", 0x8B4513 },
    { "salmon", 0xFA8072 },
    { "sandybrown", 0xF4A460 },
    { "seagreen", 0x2E8B57 },
    { "seashell", 0xFFF5EE },
    { "sienna", 0xA0522D },
    { "silver", 0xC0C0C0 },
    { "skyblue", 0x87CEEB },
    { "slateblue", 0x6A5ACD },
    { "slategray", 0x708090 },
    { "slategrey", 0x708090 },
    { "snow", 0xFFFAFA },
    { "springgreen", 0x00FF7F },
    { "steelblue", 0x4682B4 },
    { "tan", 0xD2B48C },
    { "teal", 0x008080 },
    { "thistle", 0xD8BFD8 },
    { "tomato", 0xFF6347 },
    { "turquoise", 0x40E0D0 },
    { "violet", 0xEE82EE },
    { "wheat", 0xF5DEB3 },
    { "white", 0xFFFFFF },
    { "whitesmoke", 0xF5F5F5 },
    { "yellow", 0xFFFF00 },
    { "yellowgreen", 0x9ACD32 }
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
        if (m == 4 && hexval((unsigned char)h[3]) == 0) return -2;                      /* alpha 0 */
        if (m == 8 && hexval((unsigned char)h[6]) == 0 && hexval((unsigned char)h[7]) == 0) return -2;
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
    if (n > 4 && ig_strieq_n(s, "hsl", 3)) {
        int hsl[3], k;
        long r, g, b, c, x, m2, hh, l, sat;
        i = 3;
        if (i < n && lower((unsigned char)s[i]) == 'a') i++;
        if (i >= n || s[i] != '(') return -1;
        i++;
        for (k = 0; k < 3; k++) {
            int v;
            while (i < n && (is_ws((unsigned char)s[i]) || s[i] == ',')) i++;
            v = read_num(s, n, &i);
            if (v < 0) return -1;
            if (i + 3 <= n && ig_strieq_n(s + i, "deg", 3)) i += 3;
            if (i < n && s[i] == '%') i++;
            hsl[k] = v;
        }
        while (i < n && (is_ws((unsigned char)s[i]) || s[i] == ',' || s[i] == '/')) i++;
        if (i < n && s[i] == '0' && (i + 1 >= n || s[i + 1] == ')' || is_ws((unsigned char)s[i + 1]))) return -2;
        hh = hsl[0] % 360; sat = hsl[1] > 100 ? 100 : hsl[1]; l = hsl[2] > 100 ? 100 : hsl[2];
        /* in thousandths */
        c = (1000 - (2 * l * 10 - 1000 < 0 ? -(2 * l * 10 - 1000) : 2 * l * 10 - 1000)) * sat / 100;
        x = c * (1000 - ((hh * 1000 / 60) % 2000 - 1000 < 0 ? -((hh * 1000 / 60) % 2000 - 1000) : (hh * 1000 / 60) % 2000 - 1000)) / 1000;
        m2 = l * 10 - c / 2;
        if (hh < 60) { r = c; g = x; b = 0; }
        else if (hh < 120) { r = x; g = c; b = 0; }
        else if (hh < 180) { r = 0; g = c; b = x; }
        else if (hh < 240) { r = 0; g = x; b = c; }
        else if (hh < 300) { r = x; g = 0; b = c; }
        else { r = c; g = 0; b = x; }
        r = (r + m2) * 255 / 1000; g = (g + m2) * 255 / 1000; b = (b + m2) * 255 / 1000;
        if (r < 0) r = 0;
        if (g < 0) g = 0;
        if (b < 0) b = 0;
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        return (int)((r << 16) | (g << 8) | b);
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

/* s[0..n) is lit, case-insensitively (no strlen of lit: it ends where s does) */
static int word_is_f(const char* s, unsigned int n, const char* lit)
{
    unsigned int i;
    for (i = 0; i < n; i++) {
        unsigned int a = (unsigned char)s[i], b = (unsigned char)lit[i];
        if (!b) return 0;
        if (a - 'A' < 26u) a += 32;
        if (b - 'A' < 26u) b += 32;
        if (a != b) return 0;
    }
    return lit[n] == 0;
}
/* with a string literal: its length (sizeof, at compile time) first, so the long chains of
 * property and value names cost a compare each, not a call (7 % of a Wikipedia parse) */
#define word_is(s, n, lit) ((n) == sizeof(lit) - 1 && word_is_f(s, n, lit))

/* (the whole comparison only where the first letter matches: every CSS value is searched for
 * "var(") */
static int contains_ci(const char* s, unsigned int n, const char* lit)
{
    unsigned int ln = ig_strlen(lit), i, f;
    if (!ln) return 1;
    f = (unsigned char)lit[0];
    if (f - 'A' < 26u) f += 32;
    for (i = 0; i + ln <= n; i++) {
        unsigned int c = (unsigned char)s[i];
        if (c - 'A' < 26u) c += 32;
        if (c == f && ig_strieq_n(s + i, lit, ln)) return 1;
    }
    return 0;
}

/* a CSS font-size value: px * 100 (*rel 0) or a factor of the parent's size * 10000 (*rel 1);
 * -1 unknown */
static int css_font_size(const char* v, unsigned int n, int* rel)
{
    unsigned int i = 0;
    int num;
    *rel = 0;
    if (word_is(v, n, "xx-small")) return 900;
    if (word_is(v, n, "x-small")) return 1000;
    if (word_is(v, n, "small")) return 1300;
    if (word_is(v, n, "medium")) return 1600;
    if (word_is(v, n, "large")) return 1800;
    if (word_is(v, n, "x-large")) return 2400;
    if (word_is(v, n, "xx-large")) return 3200;
    if (word_is(v, n, "smaller")) { *rel = 1; return 8300; }
    if (word_is(v, n, "larger")) { *rel = 1; return 12000; }
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
    if (i + 2 <= n && ig_strieq_n(v + i, "px", 2)) return num;
    if (i + 2 <= n && ig_strieq_n(v + i, "pt", 2)) return num * 4 / 3;
    if (i + 3 <= n && ig_strieq_n(v + i, "rem", 3)) return num * 16;
    if (i + 2 <= n && ig_strieq_n(v + i, "em", 2)) { *rel = 1; return num * 100; }
    if (i < n && v[i] == '%') { *rel = 1; return num; }
    if (i + 2 <= n && ig_strieq_n(v + i, "vw", 2)) return num * 64 / 10;       /* of a 640 px screen */
    return -1;
}

/* px * 100 -> the 4 size classes */
static int px_class(int px100)
{
    if (px100 < 1400) return 0;
    if (px100 < 1800) return 1;
    if (px100 < 2600) return 2;
    return 3;
}

/* a CSS length; 0 if it is not one */
static int parse_len(const char* v, unsigned int n, CssLen* out)
{
    unsigned int i = 0, st;
    int whole, frac = 0, neg = 0, num;
    while (i < n && is_ws((unsigned char)v[i])) i++;
    while (n > i && is_ws((unsigned char)v[n - 1])) n--;
    if (n - i == 4 && ig_strieq_n(v + i, "auto", 4)) { out->v = 0; out->unit = 3; return 1; }
    if (i < n && v[i] == '-') { neg = 1; i++; }
    else if (i < n && v[i] == '+') i++;
    st = i;
    whole = read_num(v, n, &i);
    if (whole < 0) { if (st < n && v[st] == '.') { whole = 0; i = st; read_num(v, n, &i); } else return 0; }
    {
        unsigned int k;
        for (k = st; k < i; k++) if (v[k] == '.') {
            if (k + 1 < i) frac += (v[k + 1] - '0') * 10;
            if (k + 2 < i) frac += v[k + 2] - '0';
            break;
        }
    }
    num = whole * 100 + frac;
    if (num > 10000000) num = 10000000;
    if (neg) num = -num;
    out->v = num;
    if (i == n) { if (num != 0) return 0; out->unit = 0; return 1; }       /* a bare 0 */
    if (n - i == 2 && ig_strieq_n(v + i, "px", 2)) out->unit = 0;
    else if (n - i == 2 && ig_strieq_n(v + i, "pt", 2)) { out->unit = 0; out->v = num * 4 / 3; }
    else if (n - i == 2 && ig_strieq_n(v + i, "em", 2)) out->unit = 1;
    else if (n - i == 3 && ig_strieq_n(v + i, "rem", 3)) { out->unit = 0; out->v = num * 16; }
    else if (n - i == 2 && ig_strieq_n(v + i, "ex", 2)) { out->unit = 1; out->v = num / 2; }
    else if (n - i == 2 && ig_strieq_n(v + i, "ch", 2)) { out->unit = 1; out->v = num / 2; }
    else if (n - i == 1 && v[i] == '%') out->unit = 2;
    else if (n - i == 2 && ig_strieq_n(v + i, "vw", 2)) { out->unit = 0; out->v = num * 64 / 10; }
    else if (n - i == 2 && ig_strieq_n(v + i, "vh", 2)) { out->unit = 0; out->v = num * 2; }
    else return 0;
    return 1;
}

/* the words of a value (spaces outside parentheses), up to max */
static int split_words(const char* v, unsigned int n, unsigned int* ws, unsigned int* we, int max)
{
    unsigned int i = 0;
    int count = 0;
    while (i < n && count < max) {
        int paren = 0;
        while (i < n && is_ws((unsigned char)v[i])) i++;
        if (i >= n) break;
        ws[count] = i;
        while (i < n && (!is_ws((unsigned char)v[i]) || paren)) {
            if (v[i] == '(') paren++; else if (v[i] == ')' && paren) paren--;
            i++;
        }
        we[count++] = i;
    }
    return count;
}

/* margin / padding shorthand: 1-4 lengths -> top right bottom left */
static void parse_sides(const char* v, unsigned int n, CssLen* out, unsigned int* boxMask, unsigned int firstBit)
{
    unsigned int ws[4], we[4];
    CssLen l[4];
    int c = split_words(v, n, ws, we, 4), k;
    for (k = 0; k < c; k++) if (!parse_len(v + ws[k], we[k] - ws[k], &l[k])) return;
    if (c == 1) { l[1] = l[2] = l[3] = l[0]; }
    else if (c == 2) { l[2] = l[0]; l[3] = l[1]; }
    else if (c == 3) { l[3] = l[1]; }
    else if (c != 4) return;
    for (k = 0; k < 4; k++) { out[k] = l[k]; *boxMask |= 1u << (firstBit + k); }
}

/* "1px solid #ccc" for one side (side -1: all) */
static void parse_border(const char* v, unsigned int n, CssProps* pr, int side)
{
    unsigned int ws[4], we[4];
    int c = split_words(v, n, ws, we, 4), k, width = -1, visible = 1, color = -1;
    for (k = 0; k < c; k++) {
        const char* w = v + ws[k];
        unsigned int wl = we[k] - ws[k];
        CssLen len;
        int col;
        if (word_is(w, wl, "none") || word_is(w, wl, "hidden")) visible = 0;
        else if (word_is(w, wl, "thin")) width = 1;
        else if (word_is(w, wl, "medium")) width = 2;
        else if (word_is(w, wl, "thick")) width = 3;
        else if (word_is(w, wl, "solid") || word_is(w, wl, "dashed") || word_is(w, wl, "dotted") || word_is(w, wl, "double") ||
                 word_is(w, wl, "groove") || word_is(w, wl, "ridge") || word_is(w, wl, "inset") || word_is(w, wl, "outset")) { if (width < 0) width = -2; }
        else if (parse_len(w, wl, &len)) width = len.unit == 1 ? len.v / 100 * 16 / 1 : len.v / 100;
        else if ((col = parse_color(w, wl)) != -1) color = col;
    }
    if (width == -2) width = 2;          /* a style alone: "medium" */
    if (width == -1) width = visible && c ? (color != -1 ? 2 : 0) : 0;
    if (!visible) width = 0;
    if (width > 3) width = 3;
    if (width < 0) width = 0;
    for (k = 0; k < 4; k++) if (side < 0 || side == k) { pr->bw[k] = (unsigned char)width; pr->boxMask |= BX_B(k); }
    if (color >= 0) { pr->bc = color; pr->boxMask |= BX_BC; }
}

/* var(--x, fallback) -> the value; the result in out (truncated at its size) */
static void expand_vars(P* p, const char* v, unsigned int n, IgBuf* out, int depth)
{
    unsigned int i = 0;
    while (i < n) {
        if (i + 4 <= n && ig_strieq_n(v + i, "var(", 4)) {
            unsigned int e = i + 3, ns, ne, fs = 0, fe = 0;
            int paren = 0, k, found = 0;
            /* the closing parenthesis */
            for (; e < n; e++) { if (v[e] == '(') paren++; else if (v[e] == ')' && --paren == 0) break; }
            if (e >= n) { ig_buf_append(out, v + i, n - i); return; }
            ns = i + 4;
            while (ns < e && is_ws((unsigned char)v[ns])) ns++;
            ne = ns;
            while (ne < e && v[ne] != ',' && !is_ws((unsigned char)v[ne])) ne++;
            {
                unsigned int c = ne;
                while (c < e && v[c] != ',') c++;
                if (c < e) { fs = c + 1; fe = e; }
            }
            for (k = 0; k < p->varCount; k++)
                if (ig_strlen(p->varName[k]) == ne - ns && ig_memcmp(p->varName[k], v + ns, ne - ns) == 0) {
                    const char* val = p->varVal[k];
                    if (depth < 4) expand_vars(p, val, ig_strlen(val), out, depth + 1);
                    found = 1;
                    break;
                }
            if (!found && fs && depth < 4) expand_vars(p, v + fs, fe - fs, out, depth + 1);
            i = e + 1;
            continue;
        }
        ig_buf_putc(out, v[i]);
        i++;
    }
}

static void set_var(P* p, const char* name, unsigned int nl, const char* val, unsigned int vl)
{
    int k;
    char* nv;
    while (vl && is_ws((unsigned char)*val)) { val++; vl--; }
    for (k = 0; k < p->varCount; k++)
        if (ig_strlen(p->varName[k]) == nl && ig_memcmp(p->varName[k], name, nl) == 0) {
            nv = ig_strndup(val, vl);
            if (nv) { ig_free(p->varVal[k]); p->varVal[k] = nv; }
            return;
        }
    if (p->varCount >= 64) return;
    p->varName[p->varCount] = ig_strndup(name, nl);
    p->varVal[p->varCount] = ig_strndup(val, vl);
    if (!p->varName[p->varCount] || !p->varVal[p->varCount]) { ig_free(p->varName[p->varCount]); ig_free(p->varVal[p->varCount]); return; }
    p->varCount++;
}

/* Parses "prop: value; prop: value" into props (the !important ones into imp, if given). */
static void parse_decls(P* pp, const char* s, unsigned int n, CssProps* prNormal, CssProps* imp)
{
    unsigned int i = 0;
    int w1 = 0, h1 = 0, ovh = 0;
    IgBuf exp;
    ig_buf_init(&exp);
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
        {
            CssProps* pr = prNormal;
            const char* p = s + ps;
            unsigned int pl = pe - ps;
            const char* v;
            unsigned int vl;
            {
                unsigned int k;
                for (k = vs; k < ve; k++) if (s[k] == '!') {
                    if (ve - k >= 10 && ig_strieq_n(s + k + 1, "important", 9) && imp) pr = imp;
                    ve = k;
                    while (ve > vs && is_ws((unsigned char)s[ve - 1])) ve--;
                    break;
                }
            }
            v = s + vs;
            vl = ve - vs;
            /* a custom property (--x): kept for var() when it is a page-wide one */
            if (pl > 2 && p[0] == '-' && p[1] == '-') {
                if (pp && pp->allowVars) set_var(pp, p, pl, v, vl);
                continue;
            }
            if (pp && pp->varCount && contains_ci(v, vl, "var(")) {
                exp.len = 0;
                expand_vars(pp, v, vl, &exp, 0);
                if (exp.failed || !exp.data) continue;
                v = exp.data;
                vl = exp.len;
            }
            if (word_is(p, pl, "display")) {
                pr->mask |= CSS_DISPLAY;
                pr->flexRow = 0;
                if (word_is(v, vl, "none")) pr->display = D_NONE;
                else if (word_is(v, vl, "inline") || word_is(v, vl, "inline-block") || word_is(v, vl, "contents")) pr->display = D_INLINE;
                else if (word_is(v, vl, "flex") || word_is(v, vl, "grid") || word_is(v, vl, "inline-flex") || word_is(v, vl, "inline-grid")) {
                    pr->display = D_BLOCK;
                    pr->flexRow = 1;
                }
                else if (word_is(v, vl, "table") || word_is(v, vl, "inline-table")) pr->display = D_TABLE;
                else if (word_is(v, vl, "table-row-group") || word_is(v, vl, "table-header-group") ||
                         word_is(v, vl, "table-footer-group")) pr->display = D_ROWGROUP;
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
                        int rel;
                        sz = css_font_size(v + st, k - st, &rel);
                        if (sz > 0) { pr->mask |= CSS_SIZE; pr->sizeVal = sz; pr->sizeRel = (unsigned char)rel; break; }
                    }
                    while (k < vl && !is_ws((unsigned char)v[k])) k++;
                }
            } else if (word_is(p, pl, "font-size")) {
                int rel;
                int sz = css_font_size(v, vl, &rel);
                if (sz > 0) { pr->mask |= CSS_SIZE; pr->sizeVal = sz; pr->sizeRel = (unsigned char)rel; }
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
                pr->pre = (unsigned char)(word_is(v, vl, "pre") || word_is(v, vl, "pre-wrap") || word_is(v, vl, "break-spaces") ? 1 :
                                          word_is(v, vl, "nowrap") ? 2 : 0);
            } else if (word_is(p, pl, "margin") || word_is(p, pl, "padding")) {
                parse_sides(v, vl, p[0] == 'm' ? pr->m : pr->pd, &pr->boxMask, p[0] == 'm' ? 0 : 4);
            } else if (pl > 8 && (ig_strieq_n(p, "margin-", 7) || ig_strieq_n(p, "padding-", 8))) {
                int isM = p[0] == 'm' || p[0] == 'M';
                const char* side = p + (isM ? 7 : 8);
                unsigned int sl = pl - (isM ? 7 : 8);
                int k = word_is(side, sl, "top") || word_is(side, sl, "block-start") ? 0 :
                        word_is(side, sl, "right") || word_is(side, sl, "inline-end") ? 1 :
                        word_is(side, sl, "bottom") || word_is(side, sl, "block-end") ? 2 :
                        word_is(side, sl, "left") || word_is(side, sl, "inline-start") ? 3 : -1;
                CssLen len;
                if (k >= 0 && parse_len(v, vl, &len)) {
                    if (isM) { pr->m[k] = len; pr->boxMask |= BX_M(k); }
                    else { pr->pd[k] = len; pr->boxMask |= BX_P(k); }
                }
                if (isM && k == 3 && (contains_ci(v, vl, "-999") || contains_ci(v, vl, "-1000"))) { pr->mask |= CSS_HIDDEN; pr->hidden = 1; }
            } else if (word_is(p, pl, "border")) {
                parse_border(v, vl, pr, -1);
            } else if (word_is(p, pl, "border-top") || word_is(p, pl, "border-right") || word_is(p, pl, "border-bottom") || word_is(p, pl, "border-left")) {
                parse_border(v, vl, pr, p[7] == 't' || p[7] == 'T' ? 0 : p[7] == 'r' || p[7] == 'R' ? 1 : p[7] == 'b' || p[7] == 'B' ? 2 : 3);
            } else if (word_is(p, pl, "border-width")) {
                CssLen side[4];
                unsigned int bm = 0;
                int k;
                parse_sides(v, vl, side, &bm, 0);
                for (k = 0; k < 4; k++) if (bm & (1u << k)) { int w = side[k].v / 100; pr->bw[k] = (unsigned char)(w < 0 ? 0 : w > 3 ? 3 : w); pr->boxMask |= BX_B(k); }
            } else if (word_is(p, pl, "border-color")) {
                int c = parse_color(v, vl);
                if (c >= 0) { pr->bc = c; pr->boxMask |= BX_BC; }
            } else if (word_is(p, pl, "border-style")) {
                if (word_is(v, vl, "none") || word_is(v, vl, "hidden")) { int k; for (k = 0; k < 4; k++) { pr->bw[k] = 0; pr->boxMask |= BX_B(k); } }
            } else if (word_is(p, pl, "max-width")) {
                CssLen len;
                if (parse_len(v, vl, &len) && len.unit != 3) { pr->maxw = len; pr->boxMask |= BX_MAXW; }
                else if (word_is(v, vl, "none")) { pr->maxw.v = 0; pr->maxw.unit = 3; pr->boxMask |= BX_MAXW; }
            } else if (word_is(p, pl, "list-style") || word_is(p, pl, "list-style-type")) {
                static const char* const kLs[] = { "none", "disc", "circle", "square", "decimal", "lower-alpha", "upper-alpha", "lower-roman", "upper-roman", "lower-latin", "upper-latin", "decimal-leading-zero" };
                static const unsigned char kLsV[] = { LS_NONE, LS_DISC, LS_CIRCLE, LS_SQUARE, LS_DECIMAL, LS_LALPHA, LS_UALPHA, LS_LROMAN, LS_UROMAN, LS_LALPHA, LS_UALPHA, LS_DECIMAL };
                unsigned int ws[3], we[3];
                int c = split_words(v, vl, ws, we, 3), k, q;
                for (k = 0; k < c; k++)
                    for (q = 0; q < (int)(sizeof(kLsV)); q++)
                        if (word_is_f(v + ws[k], we[k] - ws[k], kLs[q])) { pr->listStyle = kLsV[q]; pr->mask |= CSS_LIST; }
            } else if (word_is(p, pl, "content")) {
                /* "text" "more" / attr(x) / none; escapes like \201C decoded */
                IgBuf c;
                unsigned int k = 0;
                int any = 0, isAttr = 0, bad = 0;
                ig_buf_init(&c);
                while (k < vl) {
                    if (v[k] == '"' || v[k] == '\'') {
                        char q = v[k++];
                        while (k < vl && v[k] != q) {
                            if (v[k] == '\\' && k + 1 < vl) {
                                unsigned int cp = 0, digits = 0;
                                k++;
                                while (k < vl && digits < 6 && hexval((unsigned char)v[k]) >= 0) { cp = cp * 16 + (unsigned int)hexval((unsigned char)v[k]); k++; digits++; }
                                if (digits) {
                                    char u[3];
                                    if (k < vl && v[k] == ' ') k++;
                                    if ((cp >= 0xE000 && cp <= 0xF8FF) || cp > 0xFFFF || cp == 0) { bad = 1; continue; }
                                    if (cp == 0xA || cp == 0xD) { ig_buf_putc(&c, ' '); continue; }
                                    if (cp < 0x80) ig_buf_putc(&c, (char)cp);
                                    else if (cp < 0x800) { u[0] = (char)(0xC0 | (cp >> 6)); u[1] = (char)(0x80 | (cp & 0x3F)); ig_buf_append(&c, u, 2); }
                                    else { u[0] = (char)(0xE0 | (cp >> 12)); u[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); u[2] = (char)(0x80 | (cp & 0x3F)); ig_buf_append(&c, u, 3); }
                                    continue;
                                }
                                if (k < vl) ig_buf_putc(&c, v[k++]);
                                continue;
                            }
                            /* a private-use character straight in UTF-8 (an icon font): not shown */
                            if ((unsigned char)v[k] == 0xEE || ((unsigned char)v[k] == 0xEF && k + 1 < vl && (unsigned char)v[k + 1] < 0xA4)) bad = 1;
                            ig_buf_putc(&c, v[k++]);
                        }
                        if (k < vl) k++;
                        any = 1;
                    } else if (k + 5 <= vl && ig_strieq_n(v + k, "attr(", 5)) {
                        unsigned int st = k + 5, e = st;
                        while (e < vl && v[e] != ')') e++;
                        while (st < e && is_ws((unsigned char)v[st])) st++;
                        {
                            unsigned int ae = e;
                            while (ae > st && is_ws((unsigned char)v[ae - 1])) ae--;
                            /* \1name\1 marks an attribute in the text */
                            ig_buf_putc(&c, 1);
                            ig_buf_append(&c, v + st, ae - st);
                            ig_buf_putc(&c, 1);
                        }
                        isAttr = 1;
                        any = 1;
                        k = e + 1;
                    } else k++;
                }
                if (pr->content) { ig_free(pr->content); pr->content = 0; }
                if (any && !bad && c.len && !c.failed) {
                    ig_buf_putc(&c, 0);
                    pr->content = ig_buf_detach(&c);
                    pr->contentAttr = (unsigned char)isAttr;
                } else ig_buf_free(&c);
            } else if (word_is(p, pl, "flex-direction") || word_is(p, pl, "flex-flow")) {
                pr->mask |= CSS_FLEXDIR;
                pr->flexCol = (unsigned char)contains_ci(v, vl, "column");
            } else if (word_is(p, pl, "float")) {
                pr->mask |= CSS_FLOAT;
                pr->isFloat = (unsigned char)(word_is(v, vl, "left") || word_is(v, vl, "right") || word_is(v, vl, "inline-start") || word_is(v, vl, "inline-end"));
            } else if (word_is(p, pl, "text-transform")) {
                pr->mask |= CSS_TRANSFORM;
                pr->transform = (unsigned char)(word_is(v, vl, "uppercase") ? 1 : word_is(v, vl, "lowercase") ? 2 : word_is(v, vl, "capitalize") ? 3 : 0);
            } else if (word_is(p, pl, "left") || word_is(p, pl, "text-indent") || word_is(p, pl, "top")) {
                /* off-screen text (skip links, screen-reader labels) */
                if (contains_ci(v, vl, "-999") || contains_ci(v, vl, "-1000")) { pr->mask |= CSS_HIDDEN; pr->hidden = 1; }
            } else if (word_is(p, pl, "clip")) {
                if (contains_ci(v, vl, "rect(0") || contains_ci(v, vl, "rect(1px")) { pr->mask |= CSS_HIDDEN; pr->hidden = 1; }
            } else if (word_is(p, pl, "clip-path")) {
                if (contains_ci(v, vl, "inset(50%")) { pr->mask |= CSS_HIDDEN; pr->hidden = 1; }
            } else if (word_is(p, pl, "width")) {
                CssLen len;
                if (word_is(v, vl, "1px") || word_is(v, vl, "0") || word_is(v, vl, "0px")) w1 = 1;
                if (parse_len(v, vl, &len)) { pr->width = len; pr->boxMask |= BX_W; }
            } else if (word_is(p, pl, "height")) {
                CssLen len;
                if (word_is(v, vl, "1px") || word_is(v, vl, "0") || word_is(v, vl, "0px")) h1 = 1;
                if (parse_len(v, vl, &len) && len.unit == 0) { pr->height = len; pr->mask |= CSS_IMGSIZE; }
            } else if (word_is(p, pl, "overflow")) {
                if (word_is(v, vl, "hidden")) ovh = 1;
            } else if (word_is(p, pl, "opacity")) {
                if (word_is(v, vl, "0")) { pr->mask |= CSS_HIDDEN; pr->hidden = 1; }
            }
        }
    }
    if (w1 && h1 && ovh) { prNormal->mask |= CSS_HIDDEN; prNormal->hidden = 1; }
    if (prNormal->boxMask) prNormal->mask |= CSS_BOX;
    if (imp && imp->boxMask) imp->mask |= CSS_BOX;
    ig_buf_free(&exp);
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
            /* a light screen, no hover, a coarse pointer, 1 pixel per pixel */
            if (contains_ci(q, ql, "dark")) ok = 0;
            if (contains_ci(q, ql, "hover: hover") || contains_ci(q, ql, "hover:hover")) ok = 0;
            if (contains_ci(q, ql, "pointer: fine") || contains_ci(q, ql, "pointer:fine")) ok = 0;
            if (contains_ci(q, ql, "pixel-ratio") && !contains_ci(q, ql, "max-")) {
                /* min-device-pixel-ratio: 2 and the like */
                unsigned int k;
                for (k = 0; k + 11 < ql; k++) if (ig_strieq_n(q + k, "pixel-ratio", 11)) {
                    unsigned int m = k + 11;
                    int v;
                    while (m < ql && (q[m] == ':' || is_ws((unsigned char)q[m]))) m++;
                    v = read_num(q, ql, &m);
                    if (v > 1) ok = 0;
                    break;
                }
            }
            if (contains_ci(q, ql, "min-resolution") && (contains_ci(q, ql, "dppx") || contains_ci(q, ql, "192dpi") || contains_ci(q, ql, "144dpi"))) ok = 0;
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

static int is_ident(int c) { return is_alnum(c) || c == '-' || c == '_' || c >= 0x80; }

/* An+B / odd / even / N inside :nth-child( ) */
static int parse_nth(const char* s, unsigned int n, short* A, short* B)
{
    unsigned int i = 0;
    int a = 0, b = 0, sign = 1, num = -1;
    while (i < n && is_ws((unsigned char)s[i])) i++;
    while (n > i && is_ws((unsigned char)s[n - 1])) n--;
    if (n - i == 3 && ig_strieq_n(s + i, "odd", 3)) { *A = 2; *B = 1; return 1; }
    if (n - i == 4 && ig_strieq_n(s + i, "even", 4)) { *A = 2; *B = 0; return 1; }
    if (i < n && (s[i] == '-' || s[i] == '+')) { if (s[i] == '-') sign = -1; i++; }
    if (i < n && s[i] >= '0' && s[i] <= '9') { num = 0; while (i < n && s[i] >= '0' && s[i] <= '9') { num = num * 10 + (s[i] - '0'); i++; if (num > 10000) return 0; } }
    if (i < n && (s[i] == 'n' || s[i] == 'N')) {
        a = sign * (num < 0 ? 1 : num);
        i++;
        while (i < n && is_ws((unsigned char)s[i])) i++;
        if (i < n) {
            int bs = 1;
            if (s[i] == '+') i++; else if (s[i] == '-') { bs = -1; i++; } else return 0;
            while (i < n && is_ws((unsigned char)s[i])) i++;
            if (i >= n || s[i] < '0' || s[i] > '9') return 0;
            while (i < n && s[i] >= '0' && s[i] <= '9') { b = b * 10 + (s[i] - '0'); i++; if (b > 10000) return 0; }
            b *= bs;
        }
    } else {
        if (num < 0) return 0;
        b = sign * num;
    }
    if (i != n) return 0;
    *A = (short)a;
    *B = (short)b;
    return 1;
}

/* [name], [name=v] ... at s (after '['), up to ']' at e; into c */
static int parse_attr_cond(const char* s, unsigned int n, CssAttrCond* c)
{
    unsigned int i = 0, ns, ne;
    ig_memset(c, 0, sizeof(CssAttrCond));
    while (i < n && is_ws((unsigned char)s[i])) i++;
    ns = i;
    while (i < n && is_ident((unsigned char)s[i])) i++;
    ne = i;
    if (ne == ns) return 0;
    while (i < n && is_ws((unsigned char)s[i])) i++;
    if (i < n) {
        unsigned int vs, ve;
        char q = 0;
        if (s[i] == '=') c->op = AT_EQ;
        else if (i + 1 < n && s[i + 1] == '=') {
            switch (s[i]) {
            case '~': c->op = AT_WORD; break;
            case '^': c->op = AT_PREFIX; break;
            case '$': c->op = AT_SUFFIX; break;
            case '*': c->op = AT_SUB; break;
            case '|': c->op = AT_DASH; break;
            default: return 0;
            }
            i++;
        } else return 0;
        i++;
        while (i < n && is_ws((unsigned char)s[i])) i++;
        if (i < n && (s[i] == '"' || s[i] == '\'')) q = s[i++];
        vs = i;
        if (q) { while (i < n && s[i] != q) i++; ve = i; if (i < n) i++; }
        else { while (i < n && !is_ws((unsigned char)s[i])) i++; ve = i; }
        while (i < n && is_ws((unsigned char)s[i])) i++;
        if (i < n && (s[i] == 'i' || s[i] == 'I')) i++;      /* [a=b i]: case is ignored anyway below */
        while (i < n && is_ws((unsigned char)s[i])) i++;
        if (i != n) return 0;
        c->val = ig_strndup(s + vs, ve - vs);
        if (!c->val) return 0;
    }
    {
        unsigned int k;
        c->name = ig_strndup(s + ns, ne - ns);
        if (!c->name) { ig_free(c->val); c->val = 0; return 0; }
        for (k = 0; c->name[k]; k++) c->name[k] = (char)lower((unsigned char)c->name[k]);
    }
    return 1;
}

/* the end of a [...] or (...) group starting at s[i] (the index after its closing bracket) */
static unsigned int group_end(const char* s, unsigned int n, unsigned int i)
{
    char open = s[i], close = open == '[' ? ']' : ')';
    int depth = 0;
    char q = 0;
    for (; i < n; i++) {
        char c = s[i];
        if (q) { if (c == q) q = 0; continue; }
        if (c == '"' || c == '\'') { q = c; continue; }
        if (c == open) depth++;
        else if (c == close && --depth == 0) return i + 1;
    }
    return n + 1;          /* unclosed */
}

static void free_compound(CssCompound* c);

/* tag#id.a.b[x=y]:first-child at s[0..n) -> a compound (strings allocated); 0 if it is not
 * one we can match. aSubject: attribute conditions are allowed (only the element itself has
 * its attributes at hand) */
static int parse_compound(const char* s, unsigned int n, CssCompound* c, int* nIds, int* nCls, int* nTags, int aSubject)
{
    unsigned int i = 0, ts, te;
    IgBuf cls;
    ig_memset(c, 0, sizeof(CssCompound));
    ig_buf_init(&cls);
    ts = i;
    while (i < n && (is_ident((unsigned char)s[i]) || s[i] == '*')) i++;
    te = i;
    if (te > ts && !(te - ts == 1 && s[ts] == '*')) {
        char name[16];
        unsigned int k;
        if (te - ts >= sizeof(name)) return 0;
        for (k = ts; k < te; k++) name[k - ts] = (char)lower((unsigned char)s[k]);
        c->tag = lookup_tag(name, te - ts);
        if (!c->tag) return 0;
        (*nTags)++;
    }
    c->bloom = c->tag ? 1u << (c->tag & 31) : 0;
    while (i < n) {
        if (s[i] == '#' && !c->id) {
            unsigned int st = ++i;
            while (i < n && is_ident((unsigned char)s[i])) i++;
            if (i == st) goto fail;
            c->id = ig_strndup(s + st, i - st);
            if (!c->id) goto fail;
            c->bloom |= bloom_bit(s + st, i - st);
            (*nIds)++;
        } else if (s[i] == '.') {
            unsigned int st = ++i;
            while (i < n && is_ident((unsigned char)s[i])) i++;
            if (i == st) goto fail;
            if (cls.len) ig_buf_putc(&cls, 0);
            ig_buf_append(&cls, s + st, i - st);
            c->bloom |= bloom_bit(s + st, i - st);
            (*nCls)++;
        } else if (s[i] == '[') {
            unsigned int e = group_end(s, n, i);
            if (!aSubject || e > n || c->attrCount >= MAX_ATTR_CONDS) goto fail;
            if (!parse_attr_cond(s + i + 1, e - i - 2, &c->attr[c->attrCount])) goto fail;
            c->attrCount++;
            (*nCls)++;
            i = e;
        } else if (s[i] == ':') {
            unsigned int st;
            if (i + 1 < n && s[i + 1] == ':') goto fail;            /* ::before, ::placeholder */
            st = ++i;
            while (i < n && is_ident((unsigned char)s[i])) i++;
            {
                const char* ps = s + st;
                unsigned int pl = i - st;
                if (word_is(ps, pl, "link") || word_is(ps, pl, "visited") || word_is(ps, pl, "any-link")) { (*nCls)++; continue; }
                if (word_is(ps, pl, "root")) { if (c->tag && c->tag != T_HTML) goto fail; c->tag = T_HTML; c->bloom |= 1u << (T_HTML & 31); (*nCls)++; continue; }
                if (word_is(ps, pl, "first-child")) { c->hasNth = 1; c->nthA = 0; c->nthB = 1; (*nCls)++; continue; }
                if (word_is(ps, pl, "hover")) { c->hover = 1; (*nCls)++; continue; }
                if (i < n && s[i] == '(' && (word_is(ps, pl, "nth-child") || word_is(ps, pl, "not"))) {
                    unsigned int e = group_end(s, n, i);
                    const char* as = s + i + 1;
                    unsigned int al = e > n ? 0 : e - i - 2;
                    if (e > n) goto fail;
                    if (word_is(ps, pl, "nth-child")) {
                        if (!parse_nth(as, al, &c->nthA, &c->nthB)) goto fail;
                        c->hasNth = 1;
                    } else {
                        /* :not(.x) / :not(#x) / :not(tag) / :not([a]) */
                        unsigned int k = 0;
                        while (k < al && is_ws((unsigned char)as[k])) k++;
                        while (al > k && is_ws((unsigned char)as[al - 1])) al--;
                        if (k < al && as[k] == '.' && !c->notClass) {
                            unsigned int q = k + 1;
                            while (q < al && is_ident((unsigned char)as[q])) q++;
                            if (q != al || q == k + 1) goto fail;
                            c->notClass = ig_strndup(as + k + 1, q - k - 1);
                        } else if (k < al && as[k] == '#' && !c->notId) {
                            unsigned int q = k + 1;
                            while (q < al && is_ident((unsigned char)as[q])) q++;
                            if (q != al || q == k + 1) goto fail;
                            c->notId = ig_strndup(as + k + 1, q - k - 1);
                        } else if (k < al && as[k] == '[' && aSubject && c->attrCount < MAX_ATTR_CONDS) {
                            if (as[al - 1] != ']' || !parse_attr_cond(as + k + 1, al - k - 2, &c->attr[c->attrCount])) goto fail;
                            c->attr[c->attrCount++].negate = 1;
                        } else if (k < al && is_ident((unsigned char)as[k]) && !c->notTag) {
                            char name[16];
                            unsigned int q = k;
                            while (q < al && is_ident((unsigned char)as[q])) { if (q - k >= 15) goto fail; name[q - k] = (char)lower((unsigned char)as[q]); q++; }
                            if (q != al) goto fail;
                            c->notTag = lookup_tag(name, q - k);
                            if (!c->notTag) goto fail;
                        } else goto fail;
                    }
                    (*nCls)++;
                    i = e;
                    continue;
                }
                goto fail;      /* :hover, :focus, :checked, :last-child...: not something we can know */
            }
        } else goto fail;
    }
    if (!c->tag && !c->id && !cls.len && te == ts && !c->attrCount && !c->hasNth && !c->notClass && !c->notId && !c->notTag && !c->hover) goto fail;
    if (cls.len) {
        ig_buf_putc(&cls, 0);
        ig_buf_putc(&cls, 0);
        if (cls.failed) goto fail;
        c->classes = cls.data;
        cls.data = 0;
    }
    ig_buf_free(&cls);
    return 1;
fail:
    ig_buf_free(&cls);
    free_compound(c);
    ig_memset(c, 0, sizeof(CssCompound));
    return 0;
}

static void free_compound(CssCompound* c)
{
    int k;
    ig_free(c->id);
    ig_free(c->classes);
    ig_free(c->notClass);
    ig_free(c->notId);
    for (k = 0; k < c->attrCount && k < MAX_ATTR_CONDS; k++) { ig_free(c->attr[k].name); ig_free(c->attr[k].val); }
}

#define MAX_COMPOUNDS 8

/* one selector -> a rule. Descendant (space) and child (>) combinators are matched; a selector
 * with anything else (+, ~, pseudo-classes, attributes) is dropped */
static void add_selector(P* p, const char* s, unsigned int n, const CssProps* pr, const CssProps* imp)
{
    CssCompound comp[MAX_COMPOUNDS];
    unsigned char child[MAX_COMPOUNDS];
    unsigned int cs[MAX_COMPOUNDS], ce[MAX_COMPOUNDS];
    int count = 0, nIds = 0, nCls = 0, nTags = 0, k, ok = 1, parts = 0, pseudo = 0, anyTag = 0;
    unsigned int i = 0;
    int pendingChild = 0;
    while (n && is_ws((unsigned char)s[n - 1])) n--;
    /* the compounds and how they join (brackets and quotes may hold spaces) */
    while (i < n && ok) {
        unsigned int st;
        while (i < n && is_ws((unsigned char)s[i])) i++;
        if (i < n && s[i] == '>') { pendingChild = 1; i++; continue; }
        if (i >= n) break;
        if (s[i] == '+' || s[i] == '~') { ok = 0; break; }
        st = i;
        while (i < n && !is_ws((unsigned char)s[i]) && s[i] != '>' && s[i] != '+' && s[i] != '~') {
            if (s[i] == '[' || s[i] == '(') { i = group_end(s, n, i); if (i > n) { ok = 0; break; } continue; }
            i++;
        }
        if (!ok) break;
        if (parts >= MAX_COMPOUNDS) { ok = 0; break; }
        cs[parts] = st;
        ce[parts] = i;
        child[parts] = (unsigned char)pendingChild;   /* how it joins the one before it */
        pendingChild = 0;
        parts++;
    }
    if (ok && parts) {
        /* ::before / ::after on the subject: a rule for generated text */
        const char* se = s + ce[parts - 1];
        unsigned int sl = ce[parts - 1] - cs[parts - 1];
        static const char* const kPs[] = { "::before", "::after", ":before", ":after" };
        int q;
        for (q = 0; q < 4; q++) {
            unsigned int ql = ig_strlen(kPs[q]);
            if (sl > ql && ig_strieq_n(se - ql, kPs[q], ql)) { pseudo = (q & 1) ? 2 : 1; ce[parts - 1] -= ql; break; }
            if (sl == ql && ig_strieq_n(se - ql, kPs[q], ql)) { pseudo = (q & 1) ? 2 : 1; ce[parts - 1] -= ql; anyTag = 1; break; }
        }
        if (pseudo && !pr->content) ok = 0;         /* only the text is used */
    }
    for (k = 0; k < parts && ok; k++) {
        if (anyTag && k == parts - 1) {
            /* "::before" alone: any element */
            ig_memset(&comp[count], 0, sizeof(CssCompound));
            count++;
            continue;
        }
        if (!parse_compound(s + cs[k], ce[k] - cs[k], &comp[count], &nIds, &nCls, &nTags, k == parts - 1)) { ok = 0; break; }
        count++;
    }
    if (ok && (count == 0 || pendingChild)) ok = 0;
    if (ok) {
        CssRule* r;
        if (!grow((void**)&p->rules, &p->ruleCap, p->ruleCount, sizeof(CssRule))) ok = 0;
        else {
            r = &p->rules[p->ruleCount];
            ig_memset(r, 0, sizeof(CssRule));
            r->subj = comp[count - 1];
            r->spec = nIds * 10000 + nCls * 100 + nTags;
            r->props = *pr;
            r->props.content = 0;
            if (imp && imp->mask && !pseudo) {
                r->imp = (CssProps*)ig_malloc(sizeof(CssProps));
                if (r->imp) { *r->imp = *imp; r->imp->content = 0; }
            }
            if (pseudo) {
                r->pseudo = (unsigned char)pseudo;
                r->content = ig_strdup(pr->content);
                r->contentAttr = pr->contentAttr;
                r->props.mask = 0;
                r->props.boxMask = 0;
                if (!r->content) ok = 0;
            }
            if (count > 1) {
                r->anc = (CssCompound*)ig_malloc((unsigned int)(count - 1) * sizeof(CssCompound));
                r->ancChild = (unsigned char*)ig_malloc((unsigned int)(count - 1));
                if (!r->anc || !r->ancChild) {
                    ig_free(r->anc); ig_free(r->ancChild);
                    ok = 0;
                } else {
                    /* nearest first: comp[count-2] is the subject's parent side */
                    for (k = 0; k < count - 1; k++) {
                        r->anc[k] = comp[count - 2 - k];
                        r->ancChild[k] = child[count - 1 - k];
                    }
                    r->ancCount = (unsigned int)(count - 1);
                }
            }
            if (ok) {
                /* the hover parts, so the app can tell when a re-style could change anything */
                int q;
                for (q = 0; q < count; q++) if (comp[q].hover) {
                    BwDoc* d = p->doc;
                    if (d->hoverCount < 256 && grow((void**)&d->hoverBlooms, &d->hoverCap, d->hoverCount, sizeof(unsigned int)))
                        d->hoverBlooms[d->hoverCount++] = comp[q].bloom;
                }
                p->ruleCount++;
                p->doc->cssRules++;
                return;
            }
        }
    }
    for (k = 0; k < count; k++) free_compound(&comp[k]);
}

static void add_style_url(P* p, const char* u, unsigned int n)
{
    BwDoc* d = p->doc;
    char* url;
    if (!n || d->styleCount >= BW_MAX_STYLESHEETS) return;
    if (!grow((void**)&d->styleUrls, &d->styleCap, d->styleCount, sizeof(char*))) return;
    url = bw_url_resolve(d->baseUrl, u, n);
    if (url) d->styleUrls[d->styleCount++] = url;
}

/* "url(x.css) screen" / "'x.css'" */
static void add_import(P* p, const char* s, unsigned int n)
{
    unsigned int i = 0, st, e;
    while (i < n && is_ws((unsigned char)s[i])) i++;
    if (i + 4 <= n && ig_strieq_n(s + i, "url(", 4)) i += 4;
    while (i < n && (is_ws((unsigned char)s[i]) || s[i] == '"' || s[i] == '\'')) i++;
    st = i;
    while (i < n && s[i] != '"' && s[i] != '\'' && s[i] != ')' && !is_ws((unsigned char)s[i])) i++;
    e = i;
    while (i < n && (s[i] == '"' || s[i] == '\'' || s[i] == ')')) i++;
    if (i < n && !media_matches(s + i, n - i)) {
        /* "print" etc. (an empty media list matches) */
        unsigned int k = i;
        while (k < n && is_ws((unsigned char)s[k])) k++;
        if (k < n) return;
    }
    add_style_url(p, s + st, e - st);
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
            if (s[i] == ';') {
                /* @import in the page's own <style>: one more stylesheet to load */
                if (p->inlineCss && i - st > 7 && ig_strieq_n(s + st, "@import", 7)) add_import(p, s + st + 7, i - st - 7);
                i++;
                continue;
            }
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
            CssProps pr, imp;
            i = ds;
            while (i < n && s[i] != '}') {
                if (s[i] == '"' || s[i] == '\'') { char q = s[i++]; while (i < n && s[i] != q) i++; }
                if (i < n) i++;
            }
            de = i;
            if (i < n) i++;
            ig_memset(&pr, 0, sizeof(pr));
            ig_memset(&imp, 0, sizeof(imp));
            /* custom properties count from the page-wide rules only (not a dark theme class) */
            {
                unsigned int k = ss, nonGlobal = 0;
                while (k < se) {
                    unsigned int st = k, e;
                    while (k < se && s[k] != ',') k++;
                    e = k;
                    while (st < e && is_ws((unsigned char)s[st])) st++;
                    while (e > st && is_ws((unsigned char)s[e - 1])) e--;
                    if (!(word_is(s + st, e - st, ":root") || word_is(s + st, e - st, "html") || word_is(s + st, e - st, "body") ||
                          word_is(s + st, e - st, "*") || word_is(s + st, e - st, "html:root"))) nonGlobal = 1;
                    k++;
                }
                p->allowVars = !nonGlobal;
            }
            parse_decls(p, s + ds, de - ds, &pr, &imp);
            p->allowVars = 0;
            if (!pr.mask && !imp.mask && !pr.boxMask && !pr.content) { ig_free(imp.content); continue; }
            /* each selector of the group */
            {
                unsigned int k = ss;
                while (k < se) {
                    unsigned int st = k;
                    while (k < se && s[k] != ',') k++;
                    add_selector(p, s + st, k - st, &pr, &imp);
                    k++;
                }
                ig_free(pr.content);
                ig_free(imp.content);
            }
        }
    }
}

static void free_rules(P* p)
{
    unsigned int i;
    for (i = 0; i < p->ruleCount; i++) {
        unsigned int k;
        free_compound(&p->rules[i].subj);
        ig_free(p->rules[i].imp);
        ig_free(p->rules[i].content);
        for (k = 0; k < p->rules[i].ancCount; k++) free_compound(&p->rules[i].anc[k]);
        ig_free(p->rules[i].anc);
        ig_free(p->rules[i].ancChild);
    }
    ig_free(p->rules);
    p->rules = 0;
    ig_free(p->idx);
    ig_free(p->start);
    p->idx = p->start = 0;
    p->ruleCount = p->ruleCap = 0;
    p->indexedRules = 0;
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

/* width="150" / width="40%" -> 150 / -40; 0 none */
static int attr_width(P* p)
{
    const char* v = attr(p, "width");
    unsigned int i = 0, n;
    int r;
    if (!v) return 0;
    n = ig_strlen(v);
    while (i < n && v[i] == ' ') i++;
    r = read_num(v, n, &i);
    if (r <= 0) return 0;
    if (i < n && v[i] == '%') return r > 100 ? -100 : -r;
    return r > 4000 ? 4000 : r;
}

/* a width hint into an op's len: px, or 100000 + percent */
static unsigned int width_hint(int w)
{
    if (w > 0) return (unsigned int)w;
    if (w < 0) return 100000u + (unsigned int)(-w);
    return 0;
}

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
/* cls is one of the words of list (compared while scanning: no strlen per call) */
static int has_class(const char* list, const char* cls)
{
    if (!list || !*cls) return 0;
    for (;;) {
        const char* c;
        while (is_ws((unsigned char)*list)) list++;
        if (!*list) return 0;
        c = cls;
        while (*c && *c == *list) { c++; list++; }
        if (!*c && (!*list || is_ws((unsigned char)*list))) return 1;
        /* to the word's end: anything above a space is in it (one compare a character), then the
         * rare control characters that aren't white space */
        for (;;) {
            while ((unsigned char)*list > ' ') list++;
            if (!*list || is_ws((unsigned char)*list)) break;
            list++;
        }
    }
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
                    if (invisible(c)) continue;
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
    case T_OL: case T_P: case T_PLAINTEXT: case T_PRE: case T_SECTION: case T_SUMMARY:
    case T_UL: case T_XMP: case T_NOSCRIPT:
        return D_BLOCK;
    case T_TABLE: return D_TABLE;
    case T_TBODY: case T_THEAD: case T_TFOOT: return D_ROWGROUP;
    case T_TR: return D_ROW;
    case T_TD: case T_TH: return D_CELL;
    case T_LI: return D_LISTITEM;
    case T_HEAD: case T_SCRIPT: case T_STYLE: case T_TEMPLATE: case T_TITLE: case T_META: case T_LINK:
    case T_BASE: case T_NOEMBED: case T_PARAM: case T_SOURCE: case T_TRACK: case T_SVG: case T_MATH:
    case T_CANVAS: case T_AREA: case T_DATALIST: case T_COL: case T_COLGROUP:
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

/* a margin / padding in screen px: desktop lengths halved for the small screen, then capped;
 * at the original size (full) as they are, capped only against nonsense */
static int css_px(const CssLen* l, int px100, int horizontal, int cap, int full)
{
    long v;
    switch (l->unit) {
    case 0: v = full ? l->v : l->v / 2; break;                    /* px * 100, halved */
    case 1: v = (long)l->v * px100 / 100 / (full ? 1 : 2); break; /* em */
    case 2: v = horizontal ? (long)l->v * (full ? 10 : 3) / (full ? 1 : 2) : 0; break;   /* % of about 300 (1000) px */
    default: v = 0; break;                                        /* auto */
    }
    v /= 100;
    if (v < 0) v = 0;
    if (full) cap = 300;
    if (v > cap) v = cap;
    return (int)v;
}

/* a width: px (not scaled: the layout clamps it), < 0 a percent, 0 none / auto */
static int css_size(const CssLen* l, int px100)
{
    long v;
    switch (l->unit) {
    case 0: v = l->v / 100; break;
    case 1: v = (long)l->v * px100 / 10000; break;
    case 2: v = -(l->v / 100); if (v < -100) v = -100; if (v == 0) v = 0; return (int)v;
    default: return 0;
    }
    if (v <= 0) return 0;
    if (v > 4000) v = 4000;
    return (int)v;
}

static void apply_props(Elem* e, const CssProps* pr, int parentPx, int full)
{
    if (pr->mask & CSS_DISPLAY) { e->display = pr->display; e->isFlex = pr->flexRow; }
    if (pr->mask & CSS_FLEXDIR) e->flexCol = pr->flexCol;
    if (pr->mask & CSS_COLOR) e->color = pr->color;
    if (pr->mask & CSS_BG) e->bg = pr->bg;
    if (pr->mask & CSS_BOLD) e->font = (unsigned char)(pr->bold ? (e->font | BW_FONT_BOLD) : (e->font & ~BW_FONT_BOLD));
    if (pr->mask & CSS_ITALIC) e->font = (unsigned char)(pr->italic ? (e->font | BW_FONT_ITALIC) : (e->font & ~BW_FONT_ITALIC));
    if (pr->mask & CSS_MONO) e->font = (unsigned char)(e->font | BW_FONT_MONO);
    if (pr->mask & CSS_SIZE) {
        int px = pr->sizeRel ? (int)((long)parentPx * pr->sizeVal / 10000) : pr->sizeVal;
        if (px < 600) px = 600;
        if (px > 8000) px = 8000;
        e->px100 = px;
    }
    if (pr->mask & CSS_ALIGN) e->align = pr->align;
    if (pr->mask & CSS_UNDERLINE) e->flags = (unsigned char)(pr->underline ? (e->flags | BW_OPF_UNDERLINE) : (e->flags & ~BW_OPF_UNDERLINE));
    if (pr->mask & CSS_STRIKE) e->flags = (unsigned char)(pr->strike ? (e->flags | BW_OPF_STRIKE) : (e->flags & ~BW_OPF_STRIKE));
    if (pr->mask & CSS_PRE) {
        e->flags = (unsigned char)(e->flags & ~(BW_OPF_PRE | BW_OPF_NOWRAP));
        if (pr->pre == 1) e->flags |= BW_OPF_PRE;
        else if (pr->pre == 2) e->flags |= BW_OPF_NOWRAP;
    }
    if (pr->mask & CSS_HIDDEN) e->hidden = 1;
    if (pr->mask & CSS_LIST) e->listStyle = pr->listStyle;
    if (pr->mask & CSS_FLOAT) e->isFloat = pr->isFloat;
    if (pr->mask & CSS_TRANSFORM) e->transform = pr->transform;
    if (pr->mask & CSS_IMGSIZE) e->imgH = (short)(pr->height.v / 100);
    if (pr->boxMask) {
        int k;
        for (k = 0; k < 4; k++) {
            if (pr->boxMask & BX_M(k)) {
                const CssLen* l = &pr->m[k];
                int v = css_px(l, e->px100, k == 1 || k == 3, k == 0 || k == 2 ? 16 : 40, full);
                if (k == 0) e->mt = (short)v; else if (k == 1) e->mr = (short)v; else if (k == 2) e->mb = (short)v; else e->ml = (short)v;
                if (k == 1 || k == 3) e->autoSides = (unsigned char)(l->unit == 3 ? (e->autoSides | (k == 1 ? 1 : 2)) : (e->autoSides & ~(k == 1 ? 1 : 2)));
            }
            if (pr->boxMask & BX_P(k)) {
                int v = css_px(&pr->pd[k], e->px100, k == 1 || k == 3, k == 0 || k == 2 ? 10 : 16, full);
                if (k == 0) e->pt = (short)v; else if (k == 1) e->pr = (short)v; else if (k == 2) e->pb = (short)v; else e->pl = (short)v;
            }
            if (pr->boxMask & BX_B(k)) e->bw[k] = pr->bw[k];
        }
        if (pr->boxMask & BX_BC) e->bc = pr->bc;
        if (pr->boxMask & BX_W) {
            e->width = (short)css_size(&pr->width, e->px100);
            if (pr->width.unit == 0 && pr->width.v > 0) e->imgW = (short)(pr->width.v / 100);
        }
        if (pr->boxMask & BX_MAXW) e->maxw = (short)css_size(&pr->maxw, e->px100);
    }
}

#define MAX_MATCHED 64
#define MAX_CLASS_BUCKETS 16
#define KEY_BUCKETS 64
#define B_ID (T_COUNT)
#define B_CLASS (T_COUNT + KEY_BUCKETS)
#define B_ANY (T_COUNT + 2 * KEY_BUCKETS)
#define N_BUCKETS (B_ANY + 1)

static unsigned int key_bucket(const char* s, unsigned int n)
{
    unsigned int h = 2166136261u, i;
    for (i = 0; i < n; i++) { h ^= (unsigned char)s[i]; h *= 16777619u; }
    return (h >> 8) & (KEY_BUCKETS - 1);
}

static unsigned int rule_bucket(const CssRule* r)
{
    if (r->subj.id) return B_ID + key_bucket(r->subj.id, ig_strlen(r->subj.id));
    if (r->subj.classes) return B_CLASS + key_bucket(r->subj.classes, ig_strlen(r->subj.classes));
    if (r->subj.tag) return (unsigned int)r->subj.tag;
    return B_ANY;
}

/* (re)builds the index when rules were added since */
static int build_index(P* p)
{
    unsigned int i;
    if (p->idx && p->indexedRules == p->ruleCount) return 1;
    ig_free(p->idx);
    p->idx = 0;
    if (!p->start) {
        p->start = (unsigned int*)ig_malloc((N_BUCKETS + 1) * sizeof(unsigned int));
        if (!p->start) return 0;
    }
    p->idx = (unsigned int*)ig_malloc((p->ruleCount ? p->ruleCount : 1) * sizeof(unsigned int));
    if (!p->idx) return 0;
    for (i = 0; i <= N_BUCKETS; i++) p->start[i] = 0;
    /* (the index is rebuilt after each <style>: hash only the rules new since the last build) */
    for (i = p->indexedRules; i < p->ruleCount; i++) p->rules[i].bucket = rule_bucket(&p->rules[i]);
    for (i = 0; i < p->ruleCount; i++) p->start[p->rules[i].bucket + 1]++;
    for (i = 1; i <= N_BUCKETS; i++) p->start[i] += p->start[i - 1];
    {
        /* fill in rule order (each bucket stays sorted by source order) */
        unsigned int* fill = (unsigned int*)ig_malloc(N_BUCKETS * sizeof(unsigned int));
        if (!fill) { ig_free(p->idx); p->idx = 0; return 0; }
        for (i = 0; i < N_BUCKETS; i++) fill[i] = p->start[i];
        for (i = 0; i < p->ruleCount; i++) p->idx[fill[p->rules[i].bucket]++] = i;
        ig_free(fill);
    }
    p->indexedRules = p->ruleCount;
    return 1;
}

static unsigned int elem_bloom(int tag, const char* id, const char* cls)
{
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
    return have;
}

static int classes_match(const char* list, const char* need)
{
    if (!list) return 0;
    while (*need) {
        if (!has_class(list, need)) return 0;
        need += ig_strlen(need) + 1;
    }
    return 1;
}

/* :nth-child(An+B) for a 1-based index */
static int nth_matches(int a, int b, int index)
{
    if (a == 0) return index == b;
    if ((index - b) % a != 0) return 0;
    return (index - b) / a >= 0;
}

static int hovered(P* p, int num)
{
    unsigned int k;
    for (k = 0; k < p->hoverN; k++) if (p->hover[k] == num) return 1;
    return 0;
}

static int compound_matches(P* p, const CssCompound* c, const Elem* e)
{
    const char* buf = p->ancBuf.data;
    if (c->hover && !hovered(p, e->num)) return 0;
    if (c->bloom & ~e->bloom) return 0;
    if (c->tag && c->tag != e->tag) return 0;
    if (c->id && (e->idOff == ~0u || !buf || !ig_streq(buf + e->idOff, c->id))) return 0;
    if (c->classes && (e->clsOff == ~0u || !buf || !classes_match(buf + e->clsOff, c->classes))) return 0;
    if (c->hasNth && !nth_matches(c->nthA, c->nthB, e->childIndex)) return 0;
    if (c->notTag && c->notTag == e->tag) return 0;
    if (c->notClass && e->clsOff != ~0u && buf && has_class(buf + e->clsOff, c->notClass)) return 0;
    if (c->notId && e->idOff != ~0u && buf && ig_streq(buf + e->idOff, c->notId)) return 0;
    return 1;
}

static int lower_eq(const char* a, const char* b, unsigned int n)
{
    return ig_strieq_n(a, b, n);
}

/* the conditions of the subject that need the element itself: [attr], :nth-child, :not() */
static int subject_extra_matches(P* p, const CssCompound* c, int tag, const char* id, const char* cls)
{
    int k;
    if (c->hover && !hovered(p, p->curElemNum)) return 0;
    if (c->hasNth && !nth_matches(c->nthA, c->nthB, p->curChildIndex)) return 0;
    if (c->notTag && c->notTag == tag) return 0;
    if (c->notClass && cls && has_class(cls, c->notClass)) return 0;
    if (c->notId && id && ig_streq(id, c->notId)) return 0;
    for (k = 0; k < c->attrCount; k++) {
        const CssAttrCond* a = &c->attr[k];
        const char* v = attr(p, a->name);
        int ok;
        if (!v) ok = 0;
        else if (a->op == AT_HAS || !a->val) ok = 1;
        else {
            unsigned int vl = ig_strlen(v), ql = ig_strlen(a->val);
            switch (a->op) {
            case AT_EQ: ok = vl == ql && lower_eq(v, a->val, vl); break;
            case AT_PREFIX: ok = ql && vl >= ql && lower_eq(v, a->val, ql); break;
            case AT_SUFFIX: ok = ql && vl >= ql && lower_eq(v + vl - ql, a->val, ql); break;
            case AT_SUB: ok = ql && contains_ci(v, vl, a->val); break;
            case AT_WORD: ok = has_class(v, a->val); break;
            case AT_DASH: ok = (vl == ql && lower_eq(v, a->val, vl)) || (vl > ql && v[ql] == '-' && lower_eq(v, a->val, ql)); break;
            default: ok = 0; break;
            }
        }
        if (a->negate) ok = !ok;
        if (!ok) return 0;
    }
    return 1;
}

/* the rule's left part against the open elements (the new element's parent is the top) */
static int ancestors_match(P* p, const CssRule* r)
{
    int pos = p->depth - 1;
    unsigned int k;
    for (k = 0; k < r->ancCount; k++) {
        const CssCompound* c = &r->anc[k];
        if (r->ancChild[k]) {
            if (pos < 1 || !compound_matches(p, c, &p->stack[pos])) return 0;
            pos--;
        } else {
            while (pos >= 1 && !compound_matches(p, c, &p->stack[pos])) pos--;
            if (pos < 1) return 0;
            pos--;
        }
    }
    return 1;
}

/* remembers a pushed element's id and classes for the descendant selectors of its children */
static void note_attrs(P* p, Elem* e)
{
    const char* id = attr(p, "id");
    const char* cls = attr(p, "class");
    IgBuf* b = &p->ancBuf;
    e->bufStart = b->len;
    e->idOff = e->clsOff = ~0u;
    e->bloom = elem_bloom(e->tag, id, cls);
    if (!p->ruleCount) return;
    if (id) { e->idOff = b->len; ig_buf_append(b, id, ig_strlen(id) + 1); }
    if (cls) { e->clsOff = b->len; ig_buf_append(b, cls, ig_strlen(cls) + 1); }
    if (b->failed) e->idOff = e->clsOff = ~0u;
}

/* computes the style of a new element from its parent, the tag, the attributes and the CSS */
static void compute_style(P* p, Elem* e, const Elem* parent, int tag)
{
    const char* v;
    unsigned int impMatched[MAX_MATCHED];
    unsigned int nImp = 0;
    CssProps inl, inlImp;
    *e = *parent;
    e->tag = (unsigned char)tag;
    e->display = (unsigned char)default_display(tag);
    e->bg = -1;
    e->blockOpen = 0;
    e->listKind = 0;
    e->marginBottom = 0;
    e->isFloat = 0;
    e->isFlex = 0;
    e->flexCol = 0;
    e->flexItem = 0;
    e->afterOff = ~0u;
    e->labelIndex = -1;
    e->num = p->curElemNum;
    e->childCount = 0;
    e->childIndex = p->curChildIndex;
    e->labelOff = ~0u;
    e->imgW = e->imgH = 0;
    /* the box: the tag's defaults, which the CSS may change */
    e->mt = e->mb = (short)margin_top(tag, parent->listDepth);
    e->ml = e->mr = e->pt = e->pb = e->pr = e->pl = 0;
    if (tag == T_UL || tag == T_OL || tag == T_MENU || tag == T_DIR) e->pl = (short)(indent_of(tag) * (p->full ? 2 : 1));
    else e->ml = (short)(indent_of(tag) * (p->full ? 2 : 1));
    e->width = e->maxw = 0;
    e->autoSides = 0;
    e->bw[0] = e->bw[1] = e->bw[2] = e->bw[3] = 0;
    e->bc = 0x999999;
    if (tag == T_FIELDSET) {
        e->bw[0] = e->bw[1] = e->bw[2] = e->bw[3] = 1;
        e->bc = 0xAAAAAA;
        e->pt = e->pb = 3;
        e->pl = e->pr = 6;
    }
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
        e->px100 = e->px100 * 83 / 100; break;
    case T_BIG:
        e->px100 = e->px100 * 120 / 100; break;
    case T_H1: e->px100 = e->px100 * 2; e->font |= BW_FONT_BOLD; break;
    case T_H2: e->px100 = e->px100 * 3 / 2; e->font |= BW_FONT_BOLD; break;
    case T_H3: e->px100 = e->px100 * 117 / 100; e->font |= BW_FONT_BOLD; break;
    case T_H4: e->font |= BW_FONT_BOLD; break;
    case T_H5: case T_H6: e->font |= BW_FONT_BOLD; break;
    case T_CENTER: case T_CAPTION: e->align = BW_ALIGN_CENTER; break;
    case T_MARK: e->bg = 0xFFFF00; break;
    case T_NOBR: e->flags |= BW_OPF_NOWRAP; break;
    default: break;
    }
    if (tag == T_UL || tag == T_OL || tag == T_MENU || tag == T_DIR) {
        e->listKind = (unsigned char)(tag == T_OL ? 2 : 1);
        e->listCounter = attr_int(p, "start", 1);
        e->listDepth = parent->listDepth + 1;
        e->olType = 0;
        {
            /* <ol type=a|A|i|I|1> */
            const char* t = attr(p, "type");
            if (t && tag == T_OL) e->olType = (unsigned char)(t[0] == 'a' ? LS_LALPHA : t[0] == 'A' ? LS_UALPHA : t[0] == 'i' ? LS_LROMAN : t[0] == 'I' ? LS_UROMAN : LS_DECIMAL);
        }
        e->listStyle = LS_AUTO;
    }
    /* legacy presentation attributes */
    v = attr(p, "align");
    if (tag == T_TH) e->align = BW_ALIGN_CENTER;
    if ((tag == T_TD || tag == T_TH) && has_attr(p, "nowrap")) e->flags |= BW_OPF_NOWRAP;
    if (v && tag != T_TABLE && (e->display == D_BLOCK || e->display == D_ROW || tag == T_TD || tag == T_TH)) {
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
            static const int kFontPx[7] = { 1000, 1300, 1600, 1800, 2400, 3200, 4800 };
            int n = -1;
            unsigned int k = 0;
            while (v[k] == ' ') k++;
            if (v[k] == '+' && v[k + 1] >= '0' && v[k + 1] <= '9') n = 3 + (v[k + 1] - '0');
            else if (v[k] == '-' && v[k + 1] >= '0' && v[k + 1] <= '9') n = 3 - (v[k + 1] - '0');
            else if (v[k] >= '1' && v[k] <= '7') n = v[k] - '0';
            if (n >= 0) {
                if (n < 1) n = 1;
                if (n > 7) n = 7;
                e->px100 = kFontPx[n - 1];
            }
        }
    }
    v = attr(p, "bgcolor");
    if (v && tag != T_BODY && (e->display == D_BLOCK || e->display == D_TABLE || e->display == D_ROW || e->display == D_CELL)) {
        int c = parse_color(v, ig_strlen(v));
        if (c >= 0) e->bg = c;
    }
    if (has_attr(p, "hidden")) e->hidden = 1;
    if (attr(p, "aria-hidden") && ig_streq(attr(p, "aria-hidden"), "true") && (tag == T_SVG || tag == T_SPAN || tag == T_I)) e->hidden = 1;
    /* the stylesheet: the matching rules by specificity, then source order */
    p->lastCssMask = 0;
    p->pseudoRule[0] = p->pseudoRule[1] = 0;
    if (p->ruleCount) {
        const char* id = attr(p, "id");
        const char* cls = attr(p, "class");
        unsigned int have = elem_bloom(tag, id, cls);
        unsigned int matched[MAX_MATCHED];
        unsigned int nm = 0, k;
        unsigned int buckets[2 + MAX_CLASS_BUCKETS];
        unsigned int nb = 0;
        unsigned int seen0 = 0, seen1 = 0;
        if (!build_index(p)) return;
        buckets[nb++] = (unsigned int)tag;
        buckets[nb++] = B_ANY;
        if (id) buckets[nb++] = B_ID + key_bucket(id, ig_strlen(id));
        if (cls) {
            unsigned int c = 0;
            while (cls[c] && nb < 2 + MAX_CLASS_BUCKETS) {
                unsigned int st, b;
                while (cls[c] && is_ws((unsigned char)cls[c])) c++;
                st = c;
                while (cls[c] && !is_ws((unsigned char)cls[c])) c++;
                if (c == st) break;
                b = key_bucket(cls + st, c - st);
                /* two classes in one bucket: visit it once */
                if (b < 32 ? (seen0 & (1u << b)) : (seen1 & (1u << (b - 32)))) continue;
                if (b < 32) seen0 |= 1u << b; else seen1 |= 1u << (b - 32);
                buckets[nb++] = B_CLASS + b;
            }
        }
        for (k = 0; k < nb; k++) {
            unsigned int j;
            for (j = p->start[buckets[k]]; j < p->start[buckets[k] + 1]; j++) {
                unsigned int r = p->idx[j];
                const CssRule* rule = &p->rules[r];
                if (rule->subj.bloom & ~have) continue;
                if (rule->subj.tag && rule->subj.tag != tag) continue;
                if (rule->subj.id && (!id || !ig_streq(rule->subj.id, id))) continue;
                if (rule->subj.classes && !classes_match(cls, rule->subj.classes)) continue;
                if ((rule->subj.attrCount || rule->subj.hasNth || rule->subj.notTag || rule->subj.notClass || rule->subj.notId || rule->subj.hover) &&
                    !subject_extra_matches(p, &rule->subj, tag, id, cls)) continue;
                if (rule->ancCount && !ancestors_match(p, rule)) continue;
                if (rule->pseudo) {
                    int which = rule->pseudo - 1;
                    if (!p->pseudoRule[which] || p->pseudoRule[which]->spec <= rule->spec) p->pseudoRule[which] = rule;
                    continue;
                }
                if (nm < MAX_MATCHED) {
                    /* insertion by (specificity, source order) */
                    unsigned int at = nm;
                    while (at > 0 && (p->rules[matched[at - 1]].spec > rule->spec ||
                                      (p->rules[matched[at - 1]].spec == rule->spec && matched[at - 1] > r))) {
                        matched[at] = matched[at - 1];
                        at--;
                    }
                    matched[at] = r;
                    nm++;
                }
            }
        }
        for (k = 0; k < nm; k++) {
            apply_props(e, &p->rules[matched[k]].props, parent->px100, p->full);
            p->lastCssMask |= p->rules[matched[k]].props.mask;
        }
        nImp = nm;
        for (k = 0; k < nm; k++) impMatched[k] = matched[k];
    }
    v = attr(p, "style");
    if (v) {
        ig_memset(&inl, 0, sizeof(inl));
        ig_memset(&inlImp, 0, sizeof(inlImp));
        parse_decls(p, v, ig_strlen(v), &inl, &inlImp);
        ig_free(inl.content); inl.content = 0;
        ig_free(inlImp.content); inlImp.content = 0;
        apply_props(e, &inl, parent->px100, p->full);
        p->lastCssMask |= inl.mask;
    }
    /* !important: the rules' (in the same order), then the style attribute's */
    {
        unsigned int k;
        for (k = 0; k < nImp; k++) {
            const CssProps* ip = p->rules[impMatched[k]].imp;
            if (ip) { apply_props(e, ip, parent->px100, p->full); p->lastCssMask |= ip->mask; }
        }
        if (v && inlImp.mask) { apply_props(e, &inlImp, parent->px100, p->full); p->lastCssMask |= inlImp.mask; }
    }
    /* floated blocks flow side by side (a horizontal menu); a closed <dialog> is not shown */
    if (e->isFloat && (e->display == D_BLOCK || e->display == D_LISTITEM) && tag != T_TABLE) e->display = D_INLINE;
    /* the blocks in a flex / grid row flow side by side with a gap */
    if (parent->isFlex && !parent->flexCol && (e->display == D_BLOCK || e->display == D_LISTITEM)) { e->display = D_INLINE; e->flexItem = 1; }
    if (tag == T_DIALOG && !has_attr(p, "open")) e->hidden = 1;
    if (e->autoSides == 3) e->center = 1; else e->center = 0;
    /* an inline element's background goes behind its text */
    if (e->display == D_INLINE) { if (e->bg >= 0) e->ibg = e->bg; }
    else e->ibg = -1;
    if (e->display == D_NONE) e->hidden = 1;
    e->font = BW_FONT_MAKE(px_class(e->px100), e->font & 7);
}

/* ------------------------------------------------------------------ the element stack */

static void emit_block_start(P* p, Elem* e, int margin)
{
    BwOp* op;
    BwDoc* d = p->doc;
    (void)margin;
    if (e->hidden || capturing(p)) return;
    op = add_op(p, BW_OP_BLOCK_START);
    if (!op) return;
    op->a = e->mt;
    op->b = e->ml + e->bw[3] + e->pl;
    op->align = e->align;
    op->color = e->bg >= 0 ? e->bg : -1;
    op->font = e->font;
    op->ref = -1;
    /* the rest of the box, when there is any */
    if (e->pt || e->pb || e->pr || e->mr || e->bw[0] || e->bw[1] || e->bw[2] || e->bw[3] || e->width || e->maxw || e->center) {
        if (grow((void**)&d->blocks, &d->blockCap, d->blockCount, sizeof(BwBlockBox))) {
            BwBlockBox* b = &d->blocks[d->blockCount];
            b->ml = (short)e->ml; b->mr = (short)e->mr;
            b->pt = (short)e->pt; b->pb = (short)e->pb; b->pl = (short)e->pl; b->pr = (short)e->pr;
            b->bt = e->bw[0]; b->br = e->bw[1]; b->bb = e->bw[2]; b->bl = e->bw[3];
            b->bc = e->bc;
            b->width = e->width;
            b->maxw = e->maxw;
            b->center = e->center;
            op->ref = (int)d->blockCount++;
        } else d->failed = 1;
    }
    e->marginBottom = e->mb;
    e->blockOpen = 1;
    p->lastSpace = 1;
}

/* a list marker's text for number n */
static void marker_text(BwDoc* d, int style, int n, int level)
{
    char buf[16];
    int i = 0, j;
    switch (style) {
    case LS_DISC: pool_put(d, 0x2022); return;
    case LS_CIRCLE: pool_put(d, 0x25E6); return;
    case LS_SQUARE: pool_put(d, 0x25AA); return;
    case LS_LALPHA: case LS_UALPHA:
        {
            char t[8];
            int k = 0, v = n < 1 ? 1 : n;
            while (v > 0 && k < 7) { t[k++] = (char)((style == LS_LALPHA ? 'a' : 'A') + (v - 1) % 26); v = (v - 1) / 26; }
            while (k) buf[i++] = t[--k];
        }
        break;
    case LS_LROMAN: case LS_UROMAN:
        {
            static const int kVal[] = { 1000, 900, 500, 400, 100, 90, 50, 40, 10, 9, 5, 4, 1 };
            static const char* const kSym[] = { "m", "cm", "d", "cd", "c", "xc", "l", "xl", "x", "ix", "v", "iv", "i" };
            int v = n < 1 ? 1 : n > 3999 ? 3999 : n, k;
            for (k = 0; k < 13 && i < 14; k++)
                while (v >= kVal[k] && i < 14) {
                    const char* sy = kSym[k];
                    while (*sy && i < 14) { buf[i++] = (char)(style == LS_UROMAN ? *sy - 32 : *sy); sy++; }
                    v -= kVal[k];
                }
        }
        break;
    case LS_DECIMAL:
        ig_itoa(n, buf);
        i = (int)ig_strlen(buf);
        break;
    default:
        pool_put(d, (unsigned short)(level <= 1 ? 0x2022 : level == 2 ? 0x25E6 : 0x25AA));
        return;
    }
    for (j = 0; j < i; j++) pool_put(d, (unsigned short)buf[j]);
    pool_put(d, '.');
}

static void finish_capture(P* p);
static void put_label(P* p, const char* utf8);

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
    /* a label around its control: that one (the first visible one made inside) */
    if (e->tag == T_LABEL && e->labelIndex >= 0 && !p->doc->labels[e->labelIndex].forId) {
        unsigned int k;
        for (k = (unsigned int)e->firstControl; k < p->doc->controlCount; k++)
            if (p->doc->controls[k].target >= 0) { p->doc->labels[e->labelIndex].control = (int)k; break; }
    }
    if (e->tag == T_PICTURE) { ig_free(p->picSrc); p->picSrc = 0; }
    if (e->afterOff != ~0u && p->ancBuf.data && !e->hidden) {
        char* at = ig_strdup(p->ancBuf.data + e->afterOff);
        if (at) { put_label(p, at); ig_free(at); }
    }
    /* a link with nothing to show (an icon font, an SVG): its aria-label or title */
    if (e->tag == T_A && e->target >= 0 && e->labelOff != ~0u && p->ancBuf.data && !e->hidden && !capturing(p) &&
        (p->depth < 2 || p->stack[p->depth - 2].target != e->target)) {
        unsigned int k;
        int shown = 0;
        for (k = e->opStart; k < p->doc->opCount; k++) {
            const BwOp* o = &p->doc->ops[k];
            if (o->target == e->target && (o->type == BW_OP_TEXT || o->type == BW_OP_IMAGE || o->type == BW_OP_CONTROL)) { shown = 1; break; }
        }
        if (!shown) {
            char* lab = ig_strdup(p->ancBuf.data + e->labelOff);
            if (lab) {
                if (!p->lastSpace) put_text_char(p, ' ', e);
                put_label(p, lab);
                ig_free(lab);
            }
        }
    }
    if (e->blockOpen) {
        static const unsigned char kEnd[5] = { 0, BW_OP_BLOCK_END, BW_OP_TABLE_END, BW_OP_ROW_END, BW_OP_CELL_END };
        BwOp* op = add_op(p, kEnd[e->blockOpen]);
        if (op) op->a = e->marginBottom;
        p->lastSpace = 1;
    }
    if (e->bufStart <= p->ancBuf.len) p->ancBuf.len = e->bufStart;
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
        if (n) {
            /* the button's text (an icon-only one keeps its aria-label) */
            ig_free(c->label);
            c->label = dup16(s, n);
            c->labelLen = c->label ? n : 0;
        }
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
        const char* id = attr(p, "id");
        if (nm && nm[0]) c->name = ig_strdup(nm);
        if (id && id[0]) c->idAttr = ig_strdup(id);
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

/* A picture format the phone decodes (by the URL's or type's ending): not WebP / AVIF / SVG */
static int decodable(const char* u, unsigned int n)
{
    unsigned int e = 0;
    while (e < n && u[e] != '?' && u[e] != '#') e++;
    if (e >= 5 && (ig_strieq_n(u + e - 5, ".webp", 5) || ig_strieq_n(u + e - 5, ".avif", 5))) return 0;
    if (e >= 4 && ig_strieq_n(u + e - 4, ".svg", 4)) return 0;
    if (e >= 5 && ig_strieq_n(u + e - 5, ".svgz", 5)) return 0;
    if (e >= 5 && ig_strieq_n(u + e - 5, ".heic", 5)) return 0;
    return 1;
}

/* The candidate of a srcset to load: the smallest that is at least 480 px wide (else the
 * widest), or the 1x one; decodable formats first. A new string, or NULL. *densX: its density
 * descriptor x 100 (100: 1x or none, 0: a width descriptor) */
static char* pick_srcset(const char* s, int* densX)
{
    unsigned int i = 0, n = ig_strlen(s);
    unsigned int bestS = 0, bestL = 0;
    int bestW = -1, bestOk = -1, best1x = -1;
    while (i < n) {
        unsigned int us, ue, ds;
        int w = 0, x100 = 100, ok;
        while (i < n && (is_ws((unsigned char)s[i]) || s[i] == ',')) i++;
        if (i >= n) break;
        us = i;
        while (i < n && !is_ws((unsigned char)s[i])) i++;
        ue = i;
        if (ue > us && s[ue - 1] == ',') ue--;          /* "a.jpg, b.jpg" without descriptors */
        else {
            while (i < n && is_ws((unsigned char)s[i])) i++;
            ds = i;
            while (i < n && s[i] != ',') i++;
            if (i > ds) {
                unsigned int k = ds;
                int v = read_num(s, i, &k);
                if (v > 0 && k < i && (s[k] == 'w' || s[k] == 'W')) w = v;
                else if (v >= 0 && k < i && (s[k] == 'x' || s[k] == 'X')) x100 = v * 100;
                else if (k < i && s[k] == '.') { /* 1.5x */ x100 = 150; }
            }
        }
        if (ue <= us) continue;
        ok = decodable(s + us, ue - us);
        {
            int better = 0;
            if (bestW < 0) better = 1;
            else if (ok != bestOk) better = ok > bestOk;
            else if (w) {
                /* the smallest at or above 480, else the largest below */
                if (bestW >= 480) better = w >= 480 && w < bestW;
                else better = w > bestW;
            } else better = x100 < best1x;
            if (better) { bestS = us; bestL = ue - us; bestW = w ? w : 0; bestOk = ok; best1x = x100; }
        }
    }
    if (bestW < 0) return 0;
    *densX = bestW > 0 ? 0 : best1x;
    return ig_strndup(s + bestS, bestL);
}

static void handle_image(P* p, int tag, int cssW, int cssH)
{
    BwDoc* d = p->doc;
    const char* src = attr(p, "src");
    const char* ds = attr(p, "data-src");
    const char* alt = attr(p, "alt");
    const char* ss = attr(p, "srcset");
    Elem* e = top(p);
    BwImage* im;
    BwOp* op;
    char* chosen = 0;
    int densX = 100;
    int w = attr_int(p, "width", 0), h = attr_int(p, "height", 0);
    (void)tag;
    if (e->hidden || capturing(p)) return;
    if (cssW > 0 && cssW < 2000) { if (w && h && !cssH) h = h * cssW / w; w = cssW; }
    if (cssH > 0 && cssH < 2000) { if (w && h && cssW <= 0) w = w * cssH / h; h = cssH; }
    if (!ds) ds = attr(p, "data-lazy-src");
    if (!ds) ds = attr(p, "data-original");
    if (ds && ds[0] && (!src || !src[0] || ig_strieq_n(src, "data:", 5))) src = ds;
    if (!ss || !ss[0]) ss = attr(p, "data-srcset");
    /* <picture>: the <source> chosen; else this img's srcset, if src is missing or undecodable */
    if (p->picSrc && in_stack(p, T_PICTURE)) chosen = pick_srcset(p->picSrc, &densX);
    else if (ss && ss[0]) {
        chosen = pick_srcset(ss, &densX);                 /* (src is often the biggest one) */
        /* only 1.5x / 2x copies listed: src is the 1x one, and the phone's screen is 1x
         * (Wikipedia: a 180 KB 500 px picture instead of the 250 px one) */
        if (chosen && densX > 100 && src && src[0] && !ig_strieq_n(src, "data:", 5) && decodable(src, ig_strlen(src))) { ig_free(chosen); chosen = 0; }
    }
    if (chosen && !decodable(chosen, ig_strlen(chosen)) && src && src[0] && decodable(src, ig_strlen(src))) { ig_free(chosen); chosen = 0; }
    if (chosen) src = chosen;
    if ((w > 0 && w <= 2) || (h > 0 && h <= 2)) { ig_free(chosen); return; }     /* tracking pixels, spacers */
    if (!src || !src[0]) {
        if (alt && alt[0]) { put_label(p, alt); }
        ig_free(chosen);
        return;
    }
    if (!grow((void**)&d->images, &d->imageCap, d->imageCount, sizeof(BwImage))) { d->failed = 1; ig_free(chosen); return; }
    im = &d->images[d->imageCount];
    ig_memset(im, 0, sizeof(BwImage));
    if (ig_strieq_n(src, "data:", 5)) im->src = 0;
    else im->src = bw_url_resolve(d->baseUrl, src, ig_strlen(src));
    /* SVG, WebP, AVIF: the phone can't show them, so they aren't downloaded (as a failed picture:
     * its alt text or nothing) */
    if (im->src && !decodable(im->src, ig_strlen(im->src))) im->failed = 1;
    /* <noscript><img> after the lazy-loaded copy of the same picture: once is enough */
    if (im->src && d->imageCount && in_stack(p, T_NOSCRIPT) && d->images[d->imageCount - 1].src &&
        ig_streq(d->images[d->imageCount - 1].src, im->src)) {
        ig_free(im->src);
        im->src = 0;
        ig_free(chosen);
        return;
    }
    im->attrW = w;
    im->attrH = h;
    im->target = e->target;
    im->altOff = d->poolLen;
    if (alt) {
        unsigned int i = 0, n = ig_strlen(alt);
        while (i < n) {
            unsigned int c = read_char(BW_CS_UTF8, alt, n, &i, 0, 0);
            if (invisible(c)) continue;
            if (!pool_put(d, (unsigned short)c)) break;
        }
    }
    im->altLen = d->poolLen - im->altOff;
    if (!im->src) im->failed = 1;
    op = add_op(p, BW_OP_IMAGE);
    if (op) { op->ref = (int)d->imageCount; op->target = e->target; op->font = e->font; op->color = e->color; op->elem = p->curElemNum; }
    d->imageCount++;
    p->lastSpace = 0;
    ig_free(chosen);
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
    c->disabled = has_attr(p, "disabled") ? 1 : has_attr(p, "readonly") ? 2 : 0;
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
        if (op) { op->ref = idx; op->target = c->target; op->font = e->font; op->elem = p->curElemNum; }
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

/* a ::before / ::after rule's text with its attr() parts filled in (new string) */
static char* content_text(P* p, const CssRule* r)
{
    IgBuf b;
    const char* c = r->content;
    if (!r->contentAttr) return ig_strdup(c);
    ig_buf_init(&b);
    while (*c) {
        if (*c == 1) {
            const char* e = c + 1;
            char name[40];
            unsigned int n = 0;
            while (*e && *e != 1) { if (n < sizeof(name) - 1) name[n++] = (char)lower((unsigned char)*e); e++; }
            name[n] = 0;
            {
                const char* v = attr(p, name);
                if (v) ig_buf_puts(&b, v);
            }
            c = *e ? e + 1 : e;
            continue;
        }
        ig_buf_putc(&b, *c++);
    }
    return ig_buf_detach(&b);
}

/* how good a <link rel=...icon> is for a 16 px icon on the phone: 0 = unusable (SVG). PNG / GIF
 * decode for sure, ICO maybe; sizes near 16-32 px scale down cleanly; apple-touch-icon is big. */
static int icon_score(const char* rel, const char* href, const char* type, const char* sizes)
{
    unsigned int hl = ig_strlen(href), q = 0;
    int sc, n = 0;
    while (q < hl && href[q] != '?' && href[q] != '#') q++;            /* the path's end */
    if ((type && contains_ci(type, ig_strlen(type), "svg")) || (q >= 4 && ig_strieq_n(href + q - 4, ".svg", 4)) ||
        ig_strieq_n(href, "data:image/svg", 14))
        return 0;
    sc = contains_ci(rel, ig_strlen(rel), "apple-touch") ? 5 : 10;
    if ((type && (contains_ci(type, ig_strlen(type), "png") || contains_ci(type, ig_strlen(type), "gif"))) ||
        (q >= 4 && (ig_strieq_n(href + q - 4, ".png", 4) || ig_strieq_n(href + q - 4, ".gif", 4))))
        sc += 4;
    else if (!type && q >= 4 && ig_strieq_n(href + q - 4, ".ico", 4)) sc += 1;
    if (sizes) {
        const char* s = sizes;
        while (*s >= '0' && *s <= '9') { n = n * 10 + (*s - '0'); if (n > 10000) break; s++; }
        if (n > 0 && n <= 16) sc += 3;
        else if (n > 16 && n <= 32) sc += 4;
        else if (n > 32 && n <= 64) sc += 2;
    }
    return sc;
}

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
    /* its place among its siblings (for :nth-child) */
    parent->childCount++;
    p->curChildIndex = parent->childCount;
    /* its number: the same in every parse of the page, so a hover chain carries over */
    p->curElemNum = -1;
    if (grow((void**)&d->elemParent, &d->elemCap, d->elemCount, sizeof(int)) &&
        grow((void**)&d->elemBloom, &d->elemBloomCap, d->elemCount, sizeof(unsigned int))) {
        d->elemParent[d->elemCount] = parent->num;
        d->elemBloom[d->elemCount] = elem_bloom(tag, attr(p, "id"), attr(p, "class"));
        p->curElemNum = (int)d->elemCount++;
    }

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
            handle_image(p, tag, tmp.imgW, tmp.imgH);
        }
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
    case T_LINK:
        {
            const char* rel = attr(p, "rel");
            const char* href = attr(p, "href");
            const char* media = attr(p, "media");
            if (rel && href && href[0] && contains_ci(rel, ig_strlen(rel), "icon") && !contains_ci(rel, ig_strlen(rel), "mask")) {
                int sc = icon_score(rel, href, attr(p, "type"), attr(p, "sizes"));
                if (sc > d->iconScore) {
                    char* u = bw_url_resolve(d->baseUrl, href, ig_strlen(href));
                    if (u) { ig_free(d->iconUrl); d->iconUrl = u; d->iconScore = sc; }
                }
            }
            if (rel && href && href[0] && contains_ci(rel, ig_strlen(rel), "stylesheet") && !contains_ci(rel, ig_strlen(rel), "alternate") &&
                (!media || media_matches(media, ig_strlen(media))) && d->styleCount < BW_MAX_STYLESHEETS &&
                grow((void**)&d->styleUrls, &d->styleCap, d->styleCount, sizeof(char*))) {
                char* u = bw_url_resolve(d->baseUrl, href, ig_strlen(href));
                if (u) d->styleUrls[d->styleCount++] = u;
            }
        }
        return;
    case T_SOURCE:
        {
            /* <picture><source srcset type media>: the first one the phone can show */
            const char* ss = attr(p, "srcset");
            const char* type = attr(p, "type");
            const char* media = attr(p, "media");
            if (ss && ss[0] && in_stack(p, T_PICTURE) && !p->picSrc &&
                (!type || contains_ci(type, ig_strlen(type), "jpeg") || contains_ci(type, ig_strlen(type), "jpg") ||
                 contains_ci(type, ig_strlen(type), "png") || contains_ci(type, ig_strlen(type), "gif")) &&
                (!media || media_matches(media, ig_strlen(media))))
                p->picSrc = ig_strdup(ss);
        }
        return;
    case T_AREA:
        {
            /* an image map's area: a link under the picture, named by its alt text */
            const char* href = attr(p, "href");
            const char* alt = attr(p, "alt");
            if (!alt || !alt[0]) alt = attr(p, "title");
            if (href && href[0] && !parent->hidden && !capturing(p) && p->depth < MAX_DEPTH) {
                int t = new_link(p, href);
                if (t >= 0) {
                    Elem* e2 = &p->stack[p->depth];
                    compute_style(p, e2, parent, T_SPAN);
                    note_attrs(p, e2);
                    p->depth++;
                    e2->target = t;
                    e2->color = d->linkColor;
                    e2->flags |= BW_OPF_UNDERLINE;
                    if (!p->lastSpace) put_text_char(p, ' ', e2);
                    put_label(p, "[");
                    put_label(p, alt && alt[0] ? alt : href);
                    put_label(p, "]");
                    pop(p);
                    p->lastSpace = 0;
                }
            }
        }
        return;
    case T_PARAM: case T_TRACK: case T_COL: case T_EMBED:
        return;
    case T_FRAME:
        {
            const char* src = attr(p, "src");
            int t = new_link(p, src);
            if (t >= 0 && !parent->hidden) {
                const char* nm = attr(p, "name");
                Elem* e2;
                if (p->depth < MAX_DEPTH) {
                    e2 = &p->stack[p->depth];
                    compute_style(p, e2, parent, T_DIV);
                    note_attrs(p, e2);
                    p->depth++;
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
    note_attrs(p, e);
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
                    const char* lab = attr(p, "aria-label");
                    if (!lab || !lab[0]) lab = attr(p, "title");
                    e->opStart = d->opCount;
                    if (lab && lab[0]) { e->labelOff = p->ancBuf.len; ig_buf_append(&p->ancBuf, lab, ig_strlen(lab) + 1); if (p->ancBuf.failed) e->labelOff = ~0u; }
                    e->target = t;
                    /* the page's link color and underline unless a rule for this link set them */
                    if (!(p->lastCssMask & CSS_COLOR)) e->color = d->linkColor;
                    if (!(p->lastCssMask & CSS_UNDERLINE)) e->flags |= BW_OPF_UNDERLINE;
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
    case T_LABEL:
        if (!e->hidden && !capturing(p) && grow((void**)&d->labels, &d->labelCap, d->labelCount, sizeof(BwLabel))) {
            const char* f = attr(p, "for");
            BwLabel* lb = &d->labels[d->labelCount];
            int t;
            lb->forId = f && f[0] ? ig_strdup(f) : 0;
            lb->control = -1;
            t = add_target(p, BW_TGT_LABEL, (int)d->labelCount);
            if (t >= 0) {
                e->labelIndex = (int)d->labelCount;
                e->firstControl = (int)d->controlCount;
                if (e->target < 0) e->target = t;      /* (a label inside a link stays the link) */
                d->labelCount++;
            } else ig_free(lb->forId);
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
                const char* lab = attr(p, "aria-label");
                c->disabled = has_attr(p, "disabled");
                if (!lab || !lab[0]) lab = attr(p, "title");
                if (lab && lab[0]) c->label = utf8_to_16(lab, &c->labelLen);
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
                c->disabled = has_attr(p, "disabled");
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
    if (e->display == D_BLOCK || e->display == D_LISTITEM) {
        emit_block_start(p, e, 0);
        if (e->display == D_LISTITEM && e->blockOpen && e->listStyle != LS_NONE) {
            /* the marker */
            BwOp* op = add_op(p, BW_OP_MARKER);
            if (op) {
                int k;
                Elem* list = 0;
                for (k = p->depth - 2; k >= 1; k--) if (p->stack[k].listKind) { list = &p->stack[k]; break; }
                op->font = e->font & ~BW_FONT_ITALIC;
                op->color = e->color;
                op->off = d->poolLen;
                {
                    int style = e->listStyle;
                    int v = 1;
                    if (style == LS_AUTO && list && list->listKind == 2) {
                        style = list->olType ? list->olType : LS_DECIMAL;
                    }
                    if (list) {
                        v = attr_int(p, "value", list->listCounter);
                        list->listCounter = v + 1;
                    }
                    marker_text(d, style, v, list ? list->listDepth : 1);
                }
                op->len = d->poolLen - op->off;
            }
        }
    } else if (e->display == D_TABLE && !e->hidden && !capturing(p)) {
        BwOp* op = add_op(p, BW_OP_TABLE_START);
        if (op) {
            const char* al = attr(p, "align");
            int border = attr_int(p, "border", has_attr(p, "border") ? 1 : 0);
            op->a = 4;
            op->b = border > 0 ? 1 : 0;
            op->align = (unsigned char)(al && ig_strieq_n(al, "center", 6) ? BW_ALIGN_CENTER : al && ig_strieq_n(al, "right", 5) ? BW_ALIGN_RIGHT : BW_ALIGN_LEFT);
            op->color = e->bg >= 0 ? e->bg : -1;
            op->ref = attr_int(p, "cellpadding", -1);
            op->font = e->font;
            op->len = width_hint(e->width ? e->width : attr_width(p));
            e->blockOpen = 2;
            e->marginBottom = 4;
        }
        e->bg = -1;
        p->lastSpace = 1;
    } else if (e->display == D_ROW && !e->hidden && !capturing(p)) {
        BwOp* op = add_op(p, BW_OP_ROW_START);
        if (op) { op->color = e->bg >= 0 ? e->bg : -1; op->align = e->align; e->blockOpen = 3; }
        e->bg = -1;
        p->lastSpace = 1;
    } else if (e->display == D_CELL && !e->hidden && !capturing(p)) {
        BwOp* op = add_op(p, BW_OP_CELL_START);
        if (op) {
            int span = attr_int(p, "colspan", 1);
            op->a = span < 1 ? 1 : span > 20 ? 20 : span;
            op->b = tag == T_TH;
            {
                int rs = attr_int(p, "rowspan", 1);
                op->ref = rs < 1 ? 1 : rs > 50 ? 50 : rs;
            }
            op->align = e->align;
            op->color = e->bg >= 0 ? e->bg : -1;
            op->font = e->font;
            op->len = width_hint(e->width ? e->width : attr_width(p));
            e->blockOpen = 4;
        }
        e->bg = -1;
        p->lastSpace = 1;
    }
    if (tag == T_DT && e->blockOpen) e->font |= BW_FONT_BOLD;
    if (e->flexItem && !e->hidden && !capturing(p)) { add_op(p, BW_OP_GAP); p->lastSpace = 1; }
    /* generated text: ::before now, ::after when it closes; <q> gets quotation marks */
    if (!e->hidden) {
        const CssRule* br = p->pseudoRule[0];
        const CssRule* ar = p->pseudoRule[1];
        char* bt = br ? content_text(p, br) : tag == T_Q ? ig_strdup("\xE2\x80\x9C") : 0;
        char* at = ar ? content_text(p, ar) : tag == T_Q ? ig_strdup("\xE2\x80\x9D") : 0;
        if (at && at[0]) { e->afterOff = p->ancBuf.len; ig_buf_append(&p->ancBuf, at, ig_strlen(at) + 1); if (p->ancBuf.failed) e->afterOff = ~0u; }
        if (bt && bt[0]) put_label(p, bt);
        ig_free(bt);
        ig_free(at);
    }

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
            if (!media || media_matches(media, ig_strlen(media))) {
                p->inlineCss = 1;
                parse_css(p, p->s + st, e - st, 0);
                p->inlineCss = 0;
            }
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
                if (invisible(c)) continue;
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
            const char* ph = attr(p, "placeholder");
            IgBuf b;
            unsigned int i = st;
            if (ph && ph[0]) c->label = utf8_to_16(ph, &c->labelLen);
            c->disabled = has_attr(p, "disabled") ? 1 : has_attr(p, "readonly") ? 2 : 0;
            ig_buf_init(&b);
            if (i < e && p->s[i] == '\r') i++;
            if (i < e && p->s[i] == '\n') i++;
            while (i < e) {
                unsigned int ch = read_char(p->cs, p->s, e, &i, 1, 0);
                unsigned short u = (unsigned short)ch;
                if (invisible(ch)) continue;
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
    {
        /* <label for=x>: the control with id x */
        BwDoc* d = p->doc;
        unsigned int k, c;
        for (k = 0; k < d->labelCount; k++) {
            if (!d->labels[k].forId) continue;
            for (c = 0; c < d->controlCount; c++)
                if (d->controls[c].idAttr && ig_streq(d->controls[c].idAttr, d->labels[k].forId) && d->controls[c].target >= 0) {
                    d->labels[k].control = (int)c;
                    break;
                }
        }
    }
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
    ig_buf_init(&p->ancBuf);
    root = &p->stack[0];
    ig_memset(root, 0, sizeof(Elem));
    root->font = BW_FONT_MAKE(1, 0);
    root->px100 = 1600;
    root->ibg = -1;
    root->labelOff = ~0u;
    root->afterOff = ~0u;
    root->num = -1;
    root->color = d->textColor;
    root->bg = -1;
    root->target = -1;
    root->display = D_BLOCK;
    root->idOff = root->clsOff = ~0u;
    p->depth = 1;
    p->lastSpace = 1;
}

static void done_parser(P* p)
{
    ig_buf_free(&p->attrBuf);
    ig_buf_free(&p->cap16);
    ig_buf_free(&p->ancBuf);
    ig_free(p->optValue);
    ig_free(p->picSrc);
    {
        int k;
        for (k = 0; k < p->varCount; k++) { ig_free(p->varName[k]); ig_free(p->varVal[k]); }
    }
    free_rules(p);
}

/* The charset the page is read in: cs (forced) if >= 0, else the one the Content-Type or the page
 * names, else UTF-8 if the bytes are valid UTF-8, else windows-1250 (Czech pages) */
int bw_doc_charset(const char* data, unsigned int len, const char* contentType, int cs)
{
    if (cs < 0) cs = bw_charset_sniff(contentType, data, len);
    if (cs < 0) {
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
    return cs;
}

BwDoc* bw_doc_parse(const char* data, unsigned int len, const char* url, const char* contentType, int cs)
{
    return bw_doc_parse_ex(data, len, url, contentType, cs, 0, 0, 0);
}

BwDoc* bw_doc_parse_ex(const char* data, unsigned int len, const char* url, const char* contentType, int cs,
                       const char* const* css, const unsigned int* cssLen, unsigned int cssCount)
{
    return bw_doc_parse_hover(data, len, url, contentType, cs, css, cssLen, cssCount, 0, 0, 0);
}

BwDoc* bw_doc_parse_hover(const char* data, unsigned int len, const char* url, const char* contentType, int cs,
                          const char* const* css, const unsigned int* cssLen, unsigned int cssCount,
                          const int* hover, unsigned int hoverCount, unsigned int options)
{
    BwDoc* d;
    P* p;
    cs = bw_doc_charset(data, len, contentType, cs);
    if (len >= 3 && cs == BW_CS_UTF8 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF) {
        data += 3; len -= 3;
    }
    d = new_doc(url, cs);
    if (!d) return 0;
    p = (P*)ig_malloc(sizeof(P));
    if (!p) { bw_doc_free(d); return 0; }
    init_parser(p, d, data, len, cs);
    p->hover = hover;
    p->hoverN = hover ? hoverCount : 0;
    p->full = (options & BW_PARSE_FULLSIZE) != 0;
    d->fullSize = p->full;
    {
        unsigned int k;
        for (k = 0; k < cssCount; k++) if (css[k]) parse_css(p, css[k], cssLen[k], 0);
        d->externalCss = cssCount;
    }
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
        ig_free(c->name); ig_free(c->value); ig_free(c->label); ig_free(c->idAttr);
        for (k = 0; k < c->optionCount; k++) { ig_free(c->options[k].label); ig_free(c->options[k].value); }
        ig_free(c->options);
    }
    ig_free(d->controls);
    for (i = 0; i < d->formCount; i++) ig_free(d->forms[i].action);
    ig_free(d->forms);
    ig_free(d->elemParent);
    ig_free(d->elemBloom);
    ig_free(d->hoverBlooms);
    for (i = 0; i < d->labelCount; i++) ig_free(d->labels[i].forId);
    ig_free(d->labels);
    for (i = 0; i < d->styleCount; i++) ig_free(d->styleUrls[i]);
    ig_free(d->styleUrls);
    for (i = 0; i < d->anchorCount; i++) ig_free(d->anchors[i].name);
    ig_free(d->anchors);
    ig_free(d->targets);
    ig_free(d->blocks);
    ig_free(d->title);
    ig_free(d->baseUrl);
    ig_free(d->refreshUrl);
    ig_free(d->iconUrl);
    ig_free(d);
}

int bw_doc_find_anchor(const BwDoc* d, const char* name)
{
    unsigned int i;
    if (!name) return -1;
    for (i = 0; i < d->anchorCount; i++) if (ig_streq(d->anchors[i].name, name)) return (int)i;
    return -1;
}

unsigned int bw_doc_hover_chain(const BwDoc* d, int elem, int* out, unsigned int max)
{
    unsigned int n = 0;
    while (elem >= 0 && (unsigned int)elem < d->elemCount && n < max) {
        out[n++] = elem;
        elem = d->elemParent[elem];
    }
    return n;
}

int bw_doc_hover_matters(const BwDoc* d, int elem)
{
    unsigned int depth = 0, k;
    if (!d->hoverCount) return 0;
    while (elem >= 0 && (unsigned int)elem < d->elemCount && depth < 200) {
        unsigned int have = d->elemBloom[elem];
        for (k = 0; k < d->hoverCount; k++) if (!(d->hoverBlooms[k] & ~have)) return 1;
        elem = d->elemParent[elem];
        depth++;
    }
    return 0;
}

void bw_doc_copy_form_state(BwDoc* to, const BwDoc* from)
{
    unsigned int i, n = to->controlCount < from->controlCount ? to->controlCount : from->controlCount;
    for (i = 0; i < n; i++) {
        BwControl* t = &to->controls[i];
        const BwControl* f = &from->controls[i];
        if (t->type != f->type) continue;
        t->checked = f->checked;
        if ((unsigned int)f->selected < t->optionCount) t->selected = f->selected;
        if (t->type == BW_CTL_TEXT || t->type == BW_CTL_PASSWORD || t->type == BW_CTL_TEXTAREA)
            bw_control_set_value(t, f->value ? f->value : (const unsigned short*)"\0\0", f->valueLen);
    }
}
