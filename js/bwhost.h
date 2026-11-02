/* Browser 9300: a page's inline scripts (js/bwhost.c). Plain C, no libc headers, so the app's
 * C++ (src/jsplat.cpp) can include it next to Symbian's. */
#ifndef BW_HOST_H
#define BW_HOST_H
#ifdef __cplusplus
extern "C" {
#endif

struct js_State;

typedef struct BwJsPage
    {
    /* in */
    const char* html;       /* the page, UTF-8 */
    unsigned int len;
    const char* href;       /* zero-terminated */
    const char* ua;
    const char* ref;
    const char* lang;
    int w, h;               /* the screen */
    /* out (malloc'ed, the caller frees them) */
    char* out;              /* the page with each script replaced by what it wrote; NULL: as it was */
    unsigned int outLen;
    char* say;              /* errors, alert(), console.log(): lines for the log */
    unsigned int sayLen;
    int run, failed, skipped, external, written;
    int stopped;            /* 1: the time or memory ran out, the scripts after were not run */
    } BwJsPage;

/* 1 if the page has a script element at all (no engine needed otherwise) */
int bw_js_has_scripts(const char* html, unsigned int len);

/* Runs the scripts of pg->html in order in J (the host made it: its allocator and limits), each
 * <script> element replaced by what it wrote (document.write), which is read again (its scripts
 * run too). Returns 0, or -1 when the prelude itself failed (out of memory). */
int bw_js_page(struct js_State* J, BwJsPage* pg);

#ifdef __cplusplus
}
#endif
#endif
