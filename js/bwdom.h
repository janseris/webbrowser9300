/* Browser 9300: a page as a tree, for its scripts (js/bwdom.c). The tree is built from the HTML
 * the browser received, scripts change it through the DOM bindings (js/bwdomjs.c), and it is
 * written back out as HTML for the browser's own parser and layout, which stay as they are.
 *
 * Text and attribute values are kept as they were in the HTML (entities not decoded): the
 * serializer writes them back unchanged, and only textContent / getAttribute decode them.
 * Nodes are never freed one by one: all of a page's nodes go when its tree does. C89. */
#ifndef BW_DOM_H
#define BW_DOM_H

enum { BWN_ELEMENT = 1, BWN_TEXT = 3, BWN_COMMENT = 8, BWN_DOCUMENT = 9, BWN_DOCTYPE = 10, BWN_FRAGMENT = 11 };

/* node flags */
#define BWF_RAW      1      /* an element whose content is raw text (script, style, textarea, ...) */
#define BWF_VOID     2      /* an element that has no content (br, img, ...) */
#define BWF_RAN      4      /* a script that has been run (or must never run: from innerHTML) */
#define BWF_STYLED   8      /* the script side has a style object for it (flushed before writing out) */

typedef struct BwAttr { char* name; char* value; struct BwAttr* next; } BwAttr;   /* value NULL: no "=" */

typedef struct BwNode
    {
    int type;
    int flags;
    unsigned int serial;            /* the wrapper's registry key */
    char* name;                     /* elements: lower case tag name */
    char* text;                     /* text, comment, doctype: as in the HTML */
    BwAttr* attrs;
    struct BwNode* parent;
    struct BwNode* first;
    struct BwNode* last;
    struct BwNode* prev;
    struct BwNode* next;
    struct BwNode* all;             /* every node of the tree, for freeing */
    } BwNode;

typedef struct BwTree
    {
    BwNode* doc;
    BwNode* allNodes;
    unsigned int serials;
    unsigned int count;
    unsigned int limit;             /* nodes at most (0: none) */
    int oom;
    } BwTree;

typedef struct { char* p; unsigned int n, cap; int oom; } BwBuf;
void bwb_add(BwBuf* b, const char* s, unsigned int n);
void bwb_str(BwBuf* b, const char* s);

BwTree* bwd_new(unsigned int limit);
void bwd_free(BwTree* t);
BwNode* bwd_node(BwTree* t, int type, const char* name, unsigned int nameLen);
int bwd_set_text(BwNode* n, const char* s, unsigned int len);

/* the whole page into t->doc (html, head and body are always there afterwards) */
void bwd_parse_document(BwTree* t, const char* html, unsigned int len);
/* a fragment (innerHTML, document.write) under a new fragment node; scripts in it are marked
 * BWF_RAN when noScripts is set (innerHTML's never run) */
BwNode* bwd_parse_fragment(BwTree* t, const char* html, unsigned int len, int noScripts);

/* the tree under n (n itself too if outer) as HTML */
void bwd_serialize(BwNode* n, int outer, BwBuf* out);
/* textContent: the text under n, entities decoded (UTF-8) */
void bwd_text(BwNode* n, BwBuf* out);
/* s with & < > (and " when attr) escaped */
void bwd_escape(const char* s, unsigned int len, int attr, BwBuf* out);
/* &amp; and the like decoded into out */
void bwd_unescape(const char* s, unsigned int len, BwBuf* out);

/* tree edits (0: done, -1: not allowed: a cycle, a document as the child, ...) */
int bwd_insert_before(BwNode* parent, BwNode* child, BwNode* ref);
void bwd_remove(BwNode* n);
BwNode* bwd_clone(BwTree* t, BwNode* n, int deep);

const char* bwd_attr(const BwNode* n, const char* name);         /* NULL if none; raw value */
int bwd_set_attr(BwNode* n, const char* name, const char* rawValue);
void bwd_remove_attr(BwNode* n, const char* name);

/* the node after n in document order (n's own children first); NULL at the end */
BwNode* bwd_next(const BwNode* root, const BwNode* n, int intoChildren);
BwNode* bwd_child_element(BwNode* parent, const char* name);     /* the first child element named */
BwNode* bwd_find_id(BwNode* root, const char* id);
int bwd_has_class(const BwNode* n, const char* cls, unsigned int len);

/* selectors: 1 if n matches the list, -1 if the selector cannot be read */
int bwd_matches(BwNode* n, const char* sel);
/* calls each(n, ctx) for the elements under root that match, in document order; each returns
 * nonzero to stop; returns -1 if the selector cannot be read */
int bwd_select(BwNode* root, const char* sel, int (*each)(BwNode* n, void* ctx), void* ctx);

#endif
