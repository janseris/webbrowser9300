/* Browser 9300: what the DOM bindings (bwdomjs.c) and the page runner (bwhost.c) share. Internal:
 * includes mujs.h and so the libc shim; the app's C++ uses bwhost.h only. */
#ifndef BW_JS_H
#define BW_JS_H
#include "mujs.h"
#include "bwdom.h"

typedef struct BwJsDom
    {
    BwTree* tree;
    int dirty;              /* the tree changed: written out for the browser */
    int parsing;            /* document.write goes in only while the page is being read */
    BwBuf wr;               /* what the running script wrote */
    unsigned int writtenTotal;
    BwBuf say;              /* notes for the log */
    BwNode* current;        /* the running script element */
    BwBuf navigate;         /* location.href = ...: where the page sends the browser */
    BwBuf cookies;          /* document.cookie = ...: one cookie a line, for the browser's jar */
    } BwJsDom;

#define BW_MAX_WRITTEN (2 * 1024 * 1024)

/* the node wrappers, Node / Element / ... prototypes and document (as __doc) in J */
void bwdj_setup(js_State* J, BwJsDom* d);
void bwdj_push_node(js_State* J, BwNode* n);
void bwdj_note(BwJsDom* d, const char* a, const char* b);

#endif
