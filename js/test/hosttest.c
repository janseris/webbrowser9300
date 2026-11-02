/* The edited MuJS on a PC: runs every script in js/tests and prints the results, with the
 * instruction limit and the host hook as the phone has them (the hook grants 20 more slices).
 * gcc -O1 -I ../mujs -DBWC_HOST hosttest.c ../mujs/one.c ../bwlibc.c -lm; ./a.out ../tests */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <math.h>
#include "mujs.h"
/* the Math:: stand-ins for js/bwlibc.c */
double bwp_sqrt(double x) { return sqrt(x); } double bwp_exp(double x) { return exp(x); } double bwp_log(double x) { return log(x); }
double bwp_pow(double x, double y) { return pow(x, y); } double bwp_sin(double x) { return sin(x); } double bwp_cos(double x) { return cos(x); }
double bwp_tan(double x) { return tan(x); } double bwp_asin(double x) { return asin(x); } double bwp_acos(double x) { return acos(x); }
double bwp_atan(double x) { return atan(x); } double bwp_atan2(double y, double x) { return atan2(y, x); }
static char* rd(const char* f) { FILE* p = fopen(f, "rb"); long n; char* b; if (!p) return 0; fseek(p, 0, SEEK_END); n = ftell(p); rewind(p); b = malloc(n + 1); fread(b, 1, n, p); b[n] = 0; fclose(p); return b; }
static int hook(void* ctx) { int* left = ctx; return --*left > 0 ? 100000 : 0; }
static int cmp(const void* a, const void* b) { return strcmp(*(char**)a, *(char**)b); }
int main(int argc, char** argv)
{
    DIR* d = opendir(argv[1]); struct dirent* e; char* names[64]; int n = 0, i;
    while ((e = readdir(d))) if (strstr(e->d_name, ".js")) names[n++] = strdup(e->d_name);
    qsort(names, n, sizeof *names, cmp);
    for (i = 0; i < n; i++) {
        char path[512]; char* src; int left = 20;
        js_State* J = js_newstate(NULL, NULL, 0);
        js_setlimit(J, 100000, 0); js_setlimithook(J, hook, &left);
        snprintf(path, sizeof path, "%s/%s", argv[1], names[i]);
        src = rd(path);
        if (js_ploadstring(J, names[i], src)) printf("%s\tcompile: %s\n", names[i], js_tostring(J, -1));
        else { js_pushundefined(J); if (js_pcall(J, 0)) printf("%s\terror: %s\n", names[i], js_tostring(J, -1)); else printf("%s\t%s\n", names[i], js_tostring(J, -1)); }
        js_freestate(J); free(src);
    }
    return 0;
}
