/* Browser 9300 - plain C core: URLs, charsets, HTML + a little CSS, layout, forms, cookies.
 *
 * C89 so it builds with the phone's GCC 2.9 (Symbian 7.0s, EKA1) and with any host compiler
 * (test/run.sh runs it under AddressSanitizer). No libc: memory through ig_malloc/ig_free.
 * No writable static data (EKA1 .app rule): every table is const.
 *
 * Pipeline:  bytes --bw_doc_parse--> BwDoc (ops: a flat stream of styled text / blocks /
 *            images / controls)  --bw_layout--> BwLayout (positioned boxes for one width).
 * The doc is parsed once; the layout is redone when the width changes or image sizes arrive. */

#ifndef BWCORE_H
#define BWCORE_H

#include "igcore.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- URLs */
/* An absolute URL for rel against base (both UTF-8). Handles "//host", "/path", "?q", "#f",
 * relative paths with ./ and ../, and absolute URLs of any scheme. Leading/trailing
 * whitespace is dropped; bytes >= 0x80 and spaces are %-encoded. ig_malloc'ed, NULL on OOM. */
char* bw_url_resolve(const char* base, const char* rel, unsigned int relLen);
/* Splits an http(s) URL. host gets the host name (lower case), *port the port (default 80/443),
 * *secure 1 for https. *path and *pathLen: the path + query (without the fragment), "/" if empty.
 * Returns 0 if it is not an http(s) URL or the host does not fit. */
int bw_url_split(const char* url, char* host, unsigned int hostMax, int* port, int* secure,
                 const char** path, unsigned int* pathLen);
/* 1 if the two URLs differ only in the fragment (and b has one): an in-page jump */
int bw_url_same_page(const char* a, const char* b);
/* the fragment (after '#'), or NULL */
const char* bw_url_fragment(const char* url);
/* Turns what the user typed into a URL: adds http:// when no scheme is given. */
char* bw_url_from_input(const char* typed);
/* 1 if what was typed is an address (else it is searched for) */
int bw_input_is_address(const char* typed);
/* the search page URL for the typed words (UTF-8) */
char* bw_search_url(const char* typed);

/* ---------------------------------------------------------------- charsets */
enum { BW_CS_UTF8 = 0, BW_CS_1252, BW_CS_1250, BW_CS_8859_2, BW_CS_8859_1 };
int bw_charset_from_name(const char* name, unsigned int len);    /* -1 if unknown */
/* UTF-16 code for a byte >= 0x80 in an 8-bit charset */
unsigned short bw_charset_char(int cs, unsigned char b);
/* the byte for a character in an 8-bit charset, -1 if it has none */
int bw_charset_byte(int cs, unsigned short u);
/* Looks for the charset in a Content-Type value or the first bytes of a page (meta charset /
 * http-equiv); -1 if none is named. */
int bw_charset_sniff(const char* contentType, const char* data, unsigned int len);

/* ---------------------------------------------------------------- fonts */
/* A font id: bit 0 bold, bit 1 italic, bit 2 monospace, bits 3-4 size (0 small, 1 normal,
 * 2 large, 3 extra large). 32 ids; the app maps them to real fonts. */
#define BW_FONT_BOLD   1
#define BW_FONT_ITALIC 2
#define BW_FONT_MONO   4
#define BW_FONT_SIZE(f) (((f) >> 3) & 3)
#define BW_FONT_MAKE(size, flags) ((unsigned char)(((size) << 3) | (flags)))
#define BW_FONT_COUNT 32

typedef struct {
    void* ctx;
    int (*textWidth)(void* ctx, int font, const unsigned short* s, int n);
    int (*fontHeight)(void* ctx, int font);    /* line height in pixels */
    int (*fontAscent)(void* ctx, int font);
} BwMetrics;

/* ---------------------------------------------------------------- the document */
enum {
    BW_OP_TEXT = 1,      /* a run of text: off/len in the pool, font, color, flags, target */
    BW_OP_BLOCK_START,   /* a: margin top, b: left indent (px), align, color = background (or -1), ref: box (-1) */
    BW_OP_BLOCK_END,     /* a: margin bottom */
    BW_OP_BR,
    BW_OP_HR,
    BW_OP_IMAGE,         /* ref: image index */
    BW_OP_CONTROL,       /* ref: control index */
    BW_OP_ANCHOR,        /* ref: anchor index (a name / id to jump to) */
    BW_OP_MARKER,        /* a list item's bullet or number: text in the pool */
    BW_OP_TABLE_START,   /* a: margin, b: 1 = borders, align: of the table, color: background, ref: cellpadding (-1) */
    BW_OP_TABLE_END,
    BW_OP_ROW_START,     /* color: background */
    BW_OP_ROW_END,
    BW_OP_CELL_START,    /* a: colspan, b: 1 = th, ref: rowspan, align, color: background */
    BW_OP_CELL_END,
    BW_OP_GAP            /* an item of a flex / grid row starts: a gap if the line is not empty */
};

#define BW_OPF_UNDERLINE 1
#define BW_OPF_PRE       2
#define BW_OPF_STRIKE    4
#define BW_OPF_NOWRAP    8

enum { BW_ALIGN_LEFT = 0, BW_ALIGN_CENTER, BW_ALIGN_RIGHT };

typedef struct {
    unsigned char type;
    unsigned char font;
    unsigned char flags;
    unsigned char align;
    int color;           /* 0xRRGGBB (text) or background for BLOCK_START (-1 none) */
    unsigned int off;    /* text: pool offset */
    unsigned int len;    /* text: length in UTF-16 units */
    int a, b;
    int ref;
    int target;          /* the link / control the text belongs to, -1 none */
    int bg;              /* text: the background of an inline element (-1 none) */
    int elem;            /* the element it belongs to (its number, see BwDoc.elemParent), -1 */
} BwOp;

typedef struct {
    char* href;          /* absolute URL (UTF-8) */
} BwLink;

/* a block's CSS box beyond the left margin (BLOCK_START op->a: margin top, op->b: the whole left
 * indent = margin + border + padding; BLOCK_END op->a: margin bottom) */
typedef struct {
    short ml, mr, pt, pb, pl, pr;
    unsigned char bt, br, bb, bl;     /* border widths */
    int bc;                           /* border color */
    short width, maxw;                /* px; < 0 a percent of the space; 0 none */
    unsigned char center;             /* margin: auto on both sides */
} BwBlockBox;

typedef struct {
    char* src;           /* absolute URL */
    int attrW, attrH;    /* width/height attributes (0 if absent) */
    int natW, natH;      /* the decoded size, set by the app (0 = not known yet) */
    int failed;          /* set by the app when it could not be loaded */
    unsigned int altOff, altLen;   /* alt text in the pool */
    int target;
} BwImage;

enum { BW_CTL_TEXT = 1, BW_CTL_PASSWORD, BW_CTL_CHECKBOX, BW_CTL_RADIO, BW_CTL_SUBMIT,
       BW_CTL_RESET, BW_CTL_BUTTON, BW_CTL_HIDDEN, BW_CTL_SELECT, BW_CTL_TEXTAREA, BW_CTL_FILE,
       BW_CTL_IMAGE };

typedef struct {
    unsigned short* label;
    unsigned int labelLen;
    unsigned short* value;
    unsigned int valueLen;
} BwOption;

typedef struct {
    int type;
    int form;            /* -1 outside a form */
    char* name;          /* UTF-8, may be NULL */
    char* idAttr;        /* its id (for <label for>), may be NULL */
    unsigned short* value;   /* current value (UTF-16), may be NULL */
    unsigned int valueLen;
    unsigned short* label;   /* button caption */
    unsigned int labelLen;
    int checked;
    int size;            /* characters (text), rows (textarea) */
    int cols;
    int maxLength;
    BwOption* options;   /* select */
    unsigned int optionCount;
    int selected;        /* select: index */
    int target;
    int initialChecked;
    int disabled;        /* 1 disabled (not sent, not editable), 2 readonly (sent, not editable) */
} BwControl;

typedef struct {
    char* action;        /* absolute URL */
    int post;
} BwForm;

typedef struct {
    char* name;          /* UTF-8 */
} BwAnchor;

/* targets: what Tab / the arrows move between, in document order */
enum { BW_TGT_LINK = 1, BW_TGT_CONTROL, BW_TGT_LABEL };

/* <label for=x> / <label><input ...> text</label>: pressing it works the control */
typedef struct {
    char* forId;         /* the for attribute, NULL when the control is inside */
    int control;         /* the control it works, -1 none */
} BwLabel;
typedef struct {
    int kind;
    int index;           /* link or control index */
} BwTarget;

typedef struct {
    BwOp* ops; unsigned int opCount, opCap;
    unsigned short* pool; unsigned int poolLen, poolCap;
    BwLink* links; unsigned int linkCount, linkCap;
    BwImage* images; unsigned int imageCount, imageCap;
    BwControl* controls; unsigned int controlCount, controlCap;
    BwForm* forms; unsigned int formCount, formCap;
    BwAnchor* anchors; unsigned int anchorCount, anchorCap;
    BwTarget* targets; unsigned int targetCount, targetCap;
    BwBlockBox* blocks; unsigned int blockCount, blockCap;     /* BLOCK_START op->ref */
    BwLabel* labels; unsigned int labelCount, labelCap;
    /* every element by number (document order, the same in every parse of the same page) */
    int* elemParent;             /* its parent's number, -1 at the top */
    unsigned int* elemBloom;     /* bits of its tag / id / classes */
    unsigned int elemCount, elemCap, elemBloomCap;
    /* the :hover compounds of the stylesheet (their blooms): is a re-style worth it? */
    unsigned int* hoverBlooms;
    unsigned int hoverCount, hoverCap;
    unsigned short* title; unsigned int titleLen;
    char* baseUrl;       /* the page URL, or <base href> */
    char* refreshUrl;    /* <meta http-equiv=refresh>, may be NULL */
    int refreshDelay;
    int charset;
    int bgColor, textColor, linkColor;
    int failed;          /* out of memory somewhere: the doc may be incomplete */
    unsigned int cssRules;   /* how many CSS rules were read (for the page info) */
    char** styleUrls;        /* <link rel=stylesheet> for this screen, absolute, in order */
    unsigned int styleCount, styleCap;
    int fullSize;                /* parsed with BW_PARSE_FULLSIZE */
    unsigned int externalCss;    /* how many stylesheet texts were given to the parse */
} BwDoc;

#define BW_MAX_STYLESHEETS 8

/* Parses a page. url: the final URL (after redirects); contentType: the header value or NULL;
 * cs: a charset forced by the user (-1: sniff). Returns NULL only on out of memory. */
BwDoc* bw_doc_parse(const char* data, unsigned int len, const char* url, const char* contentType, int cs);
/* A plain-text page (text/plain) as a document */
BwDoc* bw_doc_plain(const char* data, unsigned int len, const char* url, int cs);
/* The same with stylesheet texts (the page's external ones, in order, UTF-8) read before the
 * page's own <style> blocks. A NULL text is skipped. */
BwDoc* bw_doc_parse_ex(const char* data, unsigned int len, const char* url, const char* contentType, int cs,
                       const char* const* css, const unsigned int* cssLen, unsigned int cssCount);
void bw_doc_free(BwDoc* doc);
/* The same with the pointer resting on an element: hover lists it and its ancestors (from
 * bw_doc_hover_chain of an earlier parse of the page), so :hover rules apply to them. */
BwDoc* bw_doc_parse_hover(const char* data, unsigned int len, const char* url, const char* contentType, int cs,
                          const char* const* css, const unsigned int* cssLen, unsigned int cssCount,
                          const int* hover, unsigned int hoverCount, unsigned int options);
/* options: lay the page out as on a computer (margins and paddings as they are), for a wide
 * layout that scrolls sideways */
#define BW_PARSE_FULLSIZE 1
/* the element and its ancestors, innermost first; the count */
unsigned int bw_doc_hover_chain(const BwDoc* doc, int elem, int* out, unsigned int max);
/* 1 if a :hover rule could apply to the chain of this element (cheap, may say yes wrongly) */
int bw_doc_hover_matters(const BwDoc* doc, int elem);
/* typed values, ticks and choices from an earlier parse of the same page (by control order) */
void bw_doc_copy_form_state(BwDoc* to, const BwDoc* from);
/* The anchor index for a fragment name, -1 if none */
int bw_doc_find_anchor(const BwDoc* doc, const char* name);

/* ---- forms */
/* Sets a text control's value (UTF-16) */
int bw_control_set_value(BwControl* c, const unsigned short* s, unsigned int n);
/* Checkbox: toggles; radio: selects it and clears the others with the same name and form */
void bw_control_click(BwDoc* doc, int control);
/* Builds the submission for a form. submitter: the control that was pressed (-1 none; it is
 * included with its name/value). *url: the URL to load (GET: with the query); *body and *bodyLen
 * the POST body (NULL for GET). Returns 0 on failure. The strings are ig_malloc'ed. */
int bw_form_submit(const BwDoc* doc, int form, int submitter, char** url, char** body,
                   unsigned int* bodyLen, int* post);
/* The value a select shows / the label of a button, as UTF-16 (points into the doc) */
const unsigned short* bw_control_text(const BwControl* c, unsigned int* len);

/* ---------------------------------------------------------------- layout */
enum { BW_BOX_TEXT = 1, BW_BOX_IMAGE, BW_BOX_CONTROL, BW_BOX_RULE };

typedef struct {
    unsigned char kind;
    unsigned char font;
    unsigned char flags;     /* BW_OPF_UNDERLINE / STRIKE */
    unsigned char pad;
    int x, y, w, h;
    int color;
    unsigned int off, len;   /* text in the doc's pool */
    int ref;                 /* image / control index */
    int target;
    int bg;                  /* text: a background behind it (-1 none) */
    int elem;                /* the element (for :hover), -1 */
} BwBox;

typedef struct {
    int x, y, w, h;
    int color;
    int outline;             /* 1: only the frame (a table border), else filled */
} BwFill;

typedef struct {
    unsigned int first;      /* its first box; the line's boxes run to the next line's first */
    int top, bottom;         /* a table row is one line (its cells side by side) */
} BwLine;

typedef struct {
    int x0, y0, x1, y1;      /* bounding box of the target's boxes (y0 = -1: not laid out) */
} BwTargetPos;

typedef struct {
    BwBox* boxes; unsigned int boxCount, boxCap;
    BwFill* fills; unsigned int fillCount, fillCap;
    BwLine* lines; unsigned int lineCount, lineCap;   /* top to bottom, bottoms increasing */
    BwTargetPos* targets;    /* one per doc target */
    int* anchorY;            /* one per doc anchor */
    int width, height;
    int failed;
} BwLayout;

/* Lays the doc out for a width; NULL on out of memory. */
BwLayout* bw_layout(const BwDoc* doc, int width, const BwMetrics* m);
void bw_layout_free(BwLayout* l);
/* the first box whose bottom is below y (boxes are in reading order, which is top to bottom
 * for the start of every line) */
unsigned int bw_layout_first_box(const BwLayout* l, int y);
/* the first line whose bottom is below y */
unsigned int bw_layout_first_line(const BwLayout* l, int y);
/* the target under a point (document coordinates), -1 none */
int bw_layout_hit(const BwLayout* l, int x, int y);
/* the box under a point (any kind), -1 none */
int bw_layout_hit_box(const BwLayout* l, int x, int y);

/* ---------------------------------------------------------------- cookies */
typedef struct {
    char* domain;        /* without a leading dot */
    char* path;
    char* name;
    char* value;
    unsigned int expires;    /* unix seconds; 0 = for this session */
    int hostOnly;
    int secure;
} BwCookie;

typedef struct {
    BwCookie* items;
    unsigned int count, cap;
} BwCookieJar;

void bw_cookies_init(BwCookieJar* j);
void bw_cookies_free(BwCookieJar* j);
/* One Set-Cookie header value received from host for a request to path. now: unix seconds. */
void bw_cookies_set(BwCookieJar* j, const char* host, const char* path, const char* header,
                    unsigned int headerLen, unsigned int now);
/* "a=1; b=2" for a request, NULL if none (ig_malloc'ed) */
char* bw_cookies_header(const BwCookieJar* j, const char* host, const char* path, int secure,
                        unsigned int now);
/* Persistent cookies as text lines (ig_malloc'ed), and back */
char* bw_cookies_save(const BwCookieJar* j, unsigned int now);
void bw_cookies_load(BwCookieJar* j, const char* text, unsigned int len, unsigned int now);
/* "Wed, 21 Oct 2026 07:28:00 GMT" -> unix seconds, 0 if unreadable */
unsigned int bw_parse_http_date(const char* s, unsigned int len);

#ifdef __cplusplus
}
#endif

#endif
