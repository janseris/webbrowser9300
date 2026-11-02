/* A page's scripts on a PC, as the phone runs them (js/bwhost.c + this MuJS + js/bwlibc.c):
 * gcc -O1 -DBWC_HOST -I ../mujs -I .. pagetest.c ../bwhost.c ../mujs/one.c ../bwlibc.c -lm
 * ./a.out page.html [url]   -> the page after its scripts on stdout, the notes on stderr */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "mujs.h"
#include "bwhost.h"
double bwp_sqrt(double x) { return sqrt(x); } double bwp_exp(double x) { return exp(x); } double bwp_log(double x) { return log(x); }
double bwp_pow(double x, double y) { return pow(x, y); } double bwp_sin(double x) { return sin(x); } double bwp_cos(double x) { return cos(x); }
double bwp_tan(double x) { return tan(x); } double bwp_asin(double x) { return asin(x); } double bwp_acos(double x) { return acos(x); }
double bwp_atan(double x) { return atan(x); } double bwp_atan2(double y, double x) { return atan2(y, x); }
static int hook(void* c) { int* left = c; return --*left > 0 ? 100000 : 0; }
int main(int argc, char** argv)
{
    FILE* f = fopen(argv[1], "rb"); long n; char* b; BwJsPage pg; js_State* J; int left = 2000;
    if (!f) return 2;
    fseek(f, 0, SEEK_END); n = ftell(f); rewind(f); b = malloc(n + 1); fread(b, 1, n, f); b[n] = 0; fclose(f);
    memset(&pg, 0, sizeof pg);
    pg.html = b; pg.len = (unsigned)n; pg.href = argc > 2 ? argv[2] : "https://example.cz/a/b.html?x=1#top";
    pg.ua = "Mozilla/5.0 (Series80/2.0; Nokia9300) Browser9300/0.38"; pg.ref = ""; pg.lang = "cs"; pg.w = 640; pg.h = 200;
    fprintf(stderr, "has scripts: %d\n", bw_js_has_scripts(b, (unsigned)n));
    J = js_newstate(0, 0, 0); js_setlimit(J, 100000, 0); js_setlimithook(J, hook, &left);
    if (bw_js_page(J, &pg)) fprintf(stderr, "PRELUDE FAILED\n");
    fprintf(stderr, "run %d failed %d skipped %d external %d written %d stopped %d\n%s", pg.run, pg.failed, pg.skipped, pg.external, pg.written, pg.stopped, pg.say ? pg.say : "");
    if (pg.out) fwrite(pg.out, 1, pg.outLen, stdout); else fwrite(b, 1, n, stdout);
    free(pg.out); free(pg.say); js_freestate(J); free(b);
    return 0;
}
