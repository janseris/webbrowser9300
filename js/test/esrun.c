/* A script's result on a PC (this MuJS + js/bwlibc.c), as Tools > JavaScript test runs it:
 *   ./esrun file.js...   -> "name<TAB>result" a line (result: the script's completion value as a string,
 *   or "ERR: message"). build_pc.sh builds it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "mujs.h"
#ifdef BW_POOL
#include "bwpool.h"
static void* sa(unsigned int n) { return malloc(n); }
static void* sr(void* p, unsigned int n) { return realloc(p, n); }
static void* palloc(void* c, void* p, int n) { return bwpool_realloc((BwPool*)c, p, (unsigned int)n); }
#endif
static clock_t deadline;
static int hook(void* c) { (void)c; return clock() < deadline ? 100000 : 0; }
double bwp_sqrt(double x) { return sqrt(x); } double bwp_exp(double x) { return exp(x); } double bwp_log(double x) { return log(x); }
double bwp_pow(double x, double y) { return pow(x, y); } double bwp_sin(double x) { return sin(x); } double bwp_cos(double x) { return cos(x); }
double bwp_tan(double x) { return tan(x); } double bwp_asin(double x) { return asin(x); } double bwp_acos(double x) { return acos(x); }
double bwp_atan(double x) { return atan(x); } double bwp_atan2(double y, double x) { return atan2(y, x); }
void* ig_malloc(unsigned int n) { return malloc(n ? n : 1); }
void ig_free(void* p) { free(p); }
static void print(js_State* J) { int i, n = js_gettop(J); for (i = 1; i < n; i++) printf(i > 1 ? " %s" : "%s", js_tostring(J, i)); printf("\n"); js_pushundefined(J); }
int main(int argc, char** argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        FILE* f = fopen(argv[i], "rb"); long n; char* b; js_State* J; const char* nm = strrchr(argv[i], '/');
        if (!f) continue;
        fseek(f, 0, SEEK_END); n = ftell(f); rewind(f); b = malloc(n + 1); fread(b, 1, n, f); b[n] = 0; fclose(f);
#ifdef BW_POOL
        BwPool* pool = bwpool_new(sa, sr, free);
        J = js_newstate(palloc, pool, 0);
#else
        J = js_newstate(0, 0, 0);
#endif
        deadline = clock() + (strstr(argv[i], "runaway") ? 2 : 60) * CLOCKS_PER_SEC;
        js_setlimit(J, 100000, 0); js_setlimithook(J, hook, 0);
        js_newcfunction(J, print, "print", 0); js_setglobal(J, "print");
        if (js_ploadstring(J, nm ? nm + 1 : argv[i], b) || (js_pushundefined(J), js_pcall(J, 0)))
            printf("%s\terror: %s\n", nm ? nm + 1 : argv[i], js_trystring(J, -1, "?"));
        else
            printf("%s\t%s\n", nm ? nm + 1 : argv[i], js_trystring(J, -1, "?"));
        js_freestate(J); free(b);
#ifdef BW_POOL
        bwpool_free(pool);
#endif
    }
    return 0;
}
