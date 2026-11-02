/* MuJS as the phone builds it (GCC 2.9 armi -O2, js/shim headers, js/bwlibc.c) in the ARM emulator.
 * Only the platform half differs from the phone: this heap, rough C maths for the Math:: calls, a
 * fixed clock and time zone (+1 h). The Python side (run.py) writes a script, calls mj_run, reads
 * the result. */
#include "bwlibc.h"
#include "mujs.h"
void ig_memcpy(void* dst, const void* src, unsigned int n);
void ig_memset(void* dst, int c, unsigned int n);

/* ---- the heap at 0x3000000 up (live and peak bytes tracked) */
typedef struct Blk { unsigned int size; struct Blk* next; } Blk;
typedef struct { char* top; Blk* small[257]; Blk* big; Blk* bigc[32]; unsigned int live, peak, allocs; int slices; } HeapState;
#define H ((HeapState*)0x2F00000)
void heap_init(void) { ig_memset(H, 0, sizeof(HeapState)); H->top = (char*)0x3000000; }
unsigned int heap_live(void) { return H->live; }
unsigned int heap_peak(void) { return H->peak; }
void heap_reset_peak(void) { H->peak = H->live; H->allocs = 0; }
static unsigned int big_class(unsigned int sz) { unsigned int c = 11; while ((1u << c) < sz) c++; return c; }
void* bwc_malloc(size_t n)
{
    Blk* b;
    unsigned int sz = (n + 15) & ~7u;          /* + the header */
    if (sz < 16) sz = 16;
    if (sz <= 2048) {
        if (H->small[sz >> 3]) { b = H->small[sz >> 3]; H->small[sz >> 3] = b->next; }
        else { b = (Blk*)H->top; H->top += sz; b->size = sz; }
    } else {
        unsigned int c = big_class(sz);
        if (H->bigc[c]) { b = H->bigc[c]; H->bigc[c] = b->next; }
        else { b = (Blk*)H->top; H->top += 1u << c; b->size = 1u << c; }
    }
    H->live += b->size; H->allocs++;
    if (H->live > H->peak) H->peak = H->live;
    return (char*)b + 8;
}
void bwc_free(void* p)
{
    Blk* b;
    if (!p) return;
    b = (Blk*)((char*)p - 8);
    H->live -= b->size;
    if (b->size <= 2048) { b->next = H->small[b->size >> 3]; H->small[b->size >> 3] = b; }
    else { unsigned int c = big_class(b->size); b->next = H->bigc[c]; H->bigc[c] = b; }
}
void* bwc_realloc(void* p, size_t n)
{
    Blk* b; void* q;
    if (!p) return bwc_malloc(n);
    if (!n) { bwc_free(p); return 0; }
    b = (Blk*)((char*)p - 8);
    if (n + 8 <= b->size) return p;
    q = bwc_malloc(n);
    ig_memcpy(q, p, b->size - 8);
    bwc_free(p);
    return q;
}
void bwc_abort(void) { *(volatile int*)4 = 1; for (;;) {} }
time_t bwc_time(time_t* t) { if (t) *t = 1793620800; return 1793620800; }
double js_platform_tza(void) { return 3600000.0; }
double js_platform_now(void) { return 1793620800000.0; }

/* ---- rough maths for the Math:: primitives (finite in-domain arguments only) */
typedef union { double d; unsigned int w[2]; } Dbl;
static double frexp_(double x, int* e)
{
    Dbl u; int ex;
    u.d = x;
    ex = (u.w[0] >> 20) & 0x7ff;
    if (!ex) { u.d = x * 18014398509481984.0; ex = ((u.w[0] >> 20) & 0x7ff) - 54; }
    *e = ex - 1022;
    u.w[0] = (u.w[0] & 0x800fffffu) | (1022u << 20);
    return u.d;
}
double bwp_sqrt(double x) { double g = x > 1 ? x / 2 : 1; int i; for (i = 0; i < 60; i++) g = 0.5 * (g + x / g); return g; }
double bwp_exp(double x)
{
    double s = 1, t = 1; int i, k = 0;
    while (x > 0.5) { x /= 2; k++; }
    while (x < -0.5) { x /= 2; k++; }
    for (i = 1; i < 30; i++) { t *= x / i; s += t; }
    while (k--) s *= s;
    return s;
}
double bwp_log(double x)
{
    int e, i; double m, y, s = 0, t;
    m = frexp_(x, &e);
    y = (m - 1) / (m + 1); t = y;
    for (i = 1; i < 80; i += 2) { s += t / i; t *= y * y; }
    return 2 * s + e * 0.69314718055994530942;
}
double bwp_pow(double x, double y) { return bwp_exp(y * bwp_log(x)); }
double bwp_sin(double x) { double s = 0, t; int i; x = bwc_fmod(x, 6.283185307179586); t = x; for (i = 1; i < 40; i += 2) { s += t; t *= -x * x / ((i + 1) * (i + 2)); } return s; }
double bwp_cos(double x) { return bwp_sin(x + 1.5707963267948966); }
double bwp_tan(double x) { return bwp_sin(x) / bwp_cos(x); }
double bwp_atan(double x) { double s = 0, t; int i; if (x > 1 || x < -1) return (x > 0 ? 1.5707963267948966 : -1.5707963267948966) - bwp_atan(1 / x); if (x > 0.5 || x < -0.5) return 2 * bwp_atan(x / (1 + bwp_sqrt(1 + x * x))); t = x; for (i = 1; i < 120; i += 2) { s += t / i; t *= -x * x; } return s; }
double bwp_atan2(double y, double x) { if (x > 0) return bwp_atan(y / x); if (x < 0) return bwp_atan(y / x) + (y >= 0 ? 3.141592653589793 : -3.141592653589793); return y > 0 ? 1.5707963267948966 : -1.5707963267948966; }
double bwp_asin(double x) { return bwp_atan2(x, bwp_sqrt(1 - x * x)); }
double bwp_acos(double x) { return bwp_atan2(bwp_sqrt(1 - x * x), x); }

/* ---- the Python side's entry points */
static int hook(void* ctx) { (void)ctx; return --H->slices > 0 ? 100000 : 0; }
int emu_init(void) { heap_init(); return 0; }
int mj_new(void) { js_State* J = js_newstate(0, 0, 0); js_setlimit(J, 100000, 0); js_setlimithook(J, hook, 0); return (int)J; }
int mj_run(js_State* J, const char* src, int slices)
{
    H->slices = slices;                     /* of 100 000 instructions */
    if (js_ploadstring(J, "[test]", src)) return 1;
    js_pushundefined(J);
    return js_pcall(J, 0) ? 2 : 0;
}
int mj_result(js_State* J) { return (int)js_tostring(J, -1); }
int mj_pop(js_State* J) { js_pop(J, 1); return 0; }
int mj_free(js_State* J) { js_freestate(J); return 0; }

/* a page's scripts: the page at src, its length, and a table of external scripts (n, then url,
 * text, length triples); returns the BwJsPage (the Python side reads out / say / missing) */
#include "bwhost.h"
void* ig_malloc(unsigned int n) { return bwc_malloc(n ? n : 1); }
void ig_free(void* p) { bwc_free(p); }
static const char* emu_fetch(void* ctx, const char* url, unsigned int* len)
{
    const unsigned int* t = (const unsigned int*)ctx;
    unsigned int i;
    for (i = 0; i < t[0]; i++) {
        if (bwc_strcmp((const char*)t[1 + i * 3], url) == 0) { *len = t[3 + i * 3]; return (const char*)t[2 + i * 3]; }
    }
    return 0;
}
int mj_page(js_State* J, const char* src, int len, const unsigned int* table)
{
    BwJsPage* pg = (BwJsPage*)bwc_malloc(sizeof(BwJsPage));
    bwc_memset(pg, 0, sizeof(BwJsPage));
    pg->html = src; pg->len = (unsigned int)len;
    pg->href = (const char*)table[1 + table[0] * 3];
    pg->ua = "Mozilla/5.0 (Series80/2.0; Nokia9300) Browser9300/0.42";
    pg->ref = ""; pg->lang = "cs"; pg->cookie = "seen=1"; pg->w = 640; pg->h = 200;
    pg->fetch = emu_fetch; pg->fetchCtx = (void*)table;
    H->slices = 20000;
    bw_js_page(J, pg);
    return (int)pg;
}
