/* A page's scripts on a PC, as the phone runs them (js/bwhost.c + bwdom.c + bwdomjs.c + this MuJS
 * + js/bwlibc.c + the core's URL code). build_pc.sh builds it.
 *   ./pagetest page.html [url] [scripts-dir]   -> the page after its scripts on stdout, notes on stderr
 * External scripts are read from scripts-dir/<the URL with every character but [A-Za-z0-9.-] as _>;
 * the ones missing are listed (getscripts.py fetches them). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "mujs.h"
#include "bwhost.h"
double bwp_sqrt(double x) { return sqrt(x); } double bwp_exp(double x) { return exp(x); } double bwp_log(double x) { return log(x); }
double bwp_pow(double x, double y) { return pow(x, y); } double bwp_sin(double x) { return sin(x); } double bwp_cos(double x) { return cos(x); }
double bwp_tan(double x) { return tan(x); } double bwp_asin(double x) { return asin(x); } double bwp_acos(double x) { return acos(x); }
double bwp_atan(double x) { return atan(x); } double bwp_atan2(double y, double x) { return atan2(y, x); }
void* ig_malloc(unsigned int n) { return malloc(n ? n : 1); }
void ig_free(void* p) { free(p); }

static char* rd(const char* f, long* n) { FILE* p = fopen(f, "rb"); char* b; if (!p) return 0; fseek(p, 0, SEEK_END); *n = ftell(p); rewind(p); b = malloc(*n + 1); fread(b, 1, *n, p); b[*n] = 0; fclose(p); return b; }

typedef struct { const char* dir; char* texts[256]; int n; clock_t deadline; int left; } Ctx;
static const char* fetch(void* c, const char* url, unsigned int* len)
{
    Ctx* x = c; char path[1200]; long n; char* t; unsigned int i, k, d;
    if (!x->dir || x->n >= 256) return 0;
    k = d = (unsigned int)snprintf(path, 600, "%s/", x->dir);
    for (i = 0; url[i] && k < d + 240; i++) path[k++] = (url[i] >= 'a' && url[i] <= 'z') || (url[i] >= 'A' && url[i] <= 'Z') || (url[i] >= '0' && url[i] <= '9') || url[i] == '.' || url[i] == '-' ? url[i] : '_';
    path[k] = 0;
    t = rd(path, &n);
    if (!t) return 0;
    x->texts[x->n++] = t;
    *len = (unsigned int)n;
    return t;
}
static int expired(void* c) { Ctx* x = c; return clock() > x->deadline; }
static int hook(void* c) { Ctx* x = c; return clock() < x->deadline && --x->left > 0 ? 100000 : 0; }

int main(int argc, char** argv)
{
    long n; char* b = rd(argv[1], &n); BwJsPage pg; js_State* J; Ctx x; int i; clock_t t0 = clock();
    if (!b) return 2;
    memset(&x, 0, sizeof x);
    x.dir = argc > 3 ? argv[3] : 0;
    x.deadline = clock() + (getenv("SECONDS") ? atoi(getenv("SECONDS")) : 30) * CLOCKS_PER_SEC;
    x.left = 200000;
    memset(&pg, 0, sizeof pg);
    pg.html = b; pg.len = (unsigned)n; pg.href = argc > 2 ? argv[2] : "https://example.cz/a/b.html?x=1#top";
    pg.ua = "Mozilla/5.0 (Series80/2.0; Nokia9300) Browser9300/0.44"; pg.ref = ""; pg.lang = "cs"; pg.w = 640; pg.h = 200;
    pg.cookie = "seen=1"; pg.expired = expired; pg.ctx = &x; pg.fetch = fetch; pg.fetchCtx = &x;
    J = js_newstate(0, 0, 0); js_setlimit(J, 100000, 0); js_setlimithook(J, hook, &x);
    if (bw_js_page(J, &pg)) fprintf(stderr, "SETUP FAILED\n");
    fprintf(stderr, "nodes %d: run %d failed %d skipped %d external %d (missing %d) big %d written %d timers %d stopped %d changed %d, %.0f ms\n%s",
            pg.nodes, pg.run, pg.failed, pg.skipped, pg.external, pg.externalMissing, pg.big, pg.written, pg.timers, pg.stopped, pg.out != 0,
            (clock() - t0) * 1000.0 / CLOCKS_PER_SEC, pg.say ? pg.say : "");
    if (pg.missing) fprintf(stderr, "MISSING:\n%s", pg.missing);
    if (pg.navigate) fprintf(stderr, "NAVIGATE: %s\n", pg.navigate);
    if (pg.cookies) fprintf(stderr, "COOKIES:\n%s", pg.cookies);
    if (pg.out) fwrite(pg.out, 1, pg.outLen, stdout); else fwrite(b, 1, n, stdout);
    free(pg.out); free(pg.say); free(pg.missing); free(pg.navigate); free(pg.cookies);
    js_freestate(J); free(b);
    for (i = 0; i < x.n; i++) free(x.texts[i]);
    return 0;
}
