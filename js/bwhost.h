/* Browser 9300: a page's scripts (js/bwhost.c). Plain C, no libc headers, so the app's C++
 * (src/jsplat.cpp) can include it next to Symbian's. */
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
    const char* cookie;     /* the cookies the browser has for the page ("a=1; b=2"), may be NULL */
    int w, h;               /* the screen */
    int (*expired)(void* ctx);  /* asked before each script: 1 when the time is up (may be NULL) */
    void* ctx;
    /* an external script's text (UTF-8) by its absolute URL, NULL if the browser does not have it
     * (yet); the text stays valid while the scripts run. May be NULL: no external scripts. */
    const char* (*fetch)(void* ctx, const char* url, unsigned int* len);
    void* fetchCtx;
    /* out (malloc'ed, the caller frees them) */
    char* out;              /* the page after its scripts; NULL: as it was (nothing changed) */
    unsigned int outLen;
    char* say;              /* errors, alert(), console.log(): lines for the log */
    unsigned int sayLen;
    char* missing;          /* external scripts the page wanted and fetch did not have: a URL a line */
    unsigned int missingLen;
    char* navigate;         /* location.href = ...: the absolute URL the page sends the browser to */
    char* cookies;          /* document.cookie = ...: a cookie (Set-Cookie syntax) a line */
    unsigned int cookiesLen;
    int run, failed, skipped, external, externalMissing, written, big, timers, nodes;
    int stopped;            /* 1: the time or memory ran out, the scripts after were not run */
    } BwJsPage;

/* 1 if the page has a JavaScript script element (inline or external): no engine otherwise */
int bw_js_has_scripts(const char* html, unsigned int len);

/* The external scripts in the HTML as it came (before any script runs), absolute, a URL a
 * line (malloc'ed; NULL if none): the browser fetches them before running the page. */
char* bw_js_script_urls(const char* html, unsigned int len, const char* base, unsigned int* outLen);

/* Builds the page's tree, runs its scripts in J (the host made it: allocator, limits) and
 * writes the tree out again. Returns 0, or -1 when the page could not be set up (memory). */
int bw_js_page(struct js_State* J, BwJsPage* pg);

#ifdef __cplusplus
}
#endif
#endif
