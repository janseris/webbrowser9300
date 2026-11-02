/* Compares js/bwlibc.c's formatting and maths helpers with the host C library.
 * gcc -O1 -DBWC_HOST fmttest.c ../bwlibc.c -lm */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
int bwc_snprintf(char* b, size_t n, const char* f, ...);
double bwc_trunc(double), bwc_floor(double), bwc_ceil(double), bwc_fmod(double, double);
long bwc_strtol(const char*, char**, int);
double bwc_strtod(const char*, char**);
int bwc_jsfmt(char*, double, int, int);
double bwp_sqrt(double x) { return sqrt(x); } double bwp_exp(double x) { return exp(x); } double bwp_log(double x) { return log(x); }
double bwp_pow(double x, double y) { return pow(x, y); } double bwp_sin(double x) { return sin(x); } double bwp_cos(double x) { return cos(x); }
double bwp_tan(double x) { return tan(x); } double bwp_asin(double x) { return asin(x); } double bwp_acos(double x) { return acos(x); }
double bwp_atan(double x) { return atan(x); } double bwp_atan2(double y, double x) { return atan2(y, x); }
double bwc_sqrt(double), bwc_exp(double), bwc_log(double), bwc_pow(double, double), bwc_sin(double), bwc_cos(double), bwc_tan(double);
double bwc_asin(double), bwc_acos(double), bwc_atan(double), bwc_atan2(double, double);
static int same(double a, double b) { if (isnan(a) || isnan(b)) return isnan(a) && isnan(b); return a == b && signbit(a) == signbit(b); }
static uint64_t rs = 88172645463325252ull;
static uint64_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }
static double pick(int i)
{
    double d; uint64_t u;
    switch (i % 8) {
    case 0: u = rnd(); memcpy(&d, &u, 8); return d;                     /* any bits */
    case 1: return (double)(int64_t)(rnd() % 2000001) - 1000000;         /* integers */
    case 2: return ((double)(int)(rnd() % 200001) - 100000) / 8;         /* exact binary halves */
    case 3: return ((double)(int)(rnd() % 200001) - 100000) / 1000;      /* typical decimals */
    case 4: return ldexp((double)(rnd() >> 11), (int)(rnd() % 120) - 110);
    case 5: return (double)(rnd() % 100000) * pow(10, (int)(rnd() % 50) - 25);
    case 6: u = rnd() & 0x000fffffffffffffull; memcpy(&d, &u, 8); return d; /* denormals */
    default: return ((double)(rnd() % 1000) + 0.5) / pow(10, (int)(rnd() % 8));
    }
}
int main(void)
{
    static const char* ff[] = { "%.*f", "%.*e", "%.*g", "%.*E", "%.*G" };
    char a[1200], b[1200];
    int i, bad = 0, n = 0;
    for (i = 0; i < 400000; i++) {
        double x = pick(i);
        int k = (int)(rnd() % 5), p = (int)(rnd() % 22);
        if (isnan(x) || isinf(x)) continue;
        if (k == 0 && fabs(x) > 1e60) continue;   /* long but fine; keep the test fast */
        snprintf(a, sizeof a, ff[k], p, x); bwc_snprintf(b, sizeof b, ff[k], p, x); n++;
        if (strcmp(a, b)) { if (bad++ < 15) printf("%s p=%d %.17g\n  libc %s\n  bwc  %s\n", ff[k], p, x, a, b); }
        if (bwc_trunc(x) != trunc(x) || bwc_floor(x) != floor(x) || bwc_ceil(x) != ceil(x) || signbit(bwc_ceil(x)) != signbit(ceil(x))) { if (bad++ < 15) printf("round %.17g\n", x); }
        { double y = pick(i + 3); if (y != 0 && fabs(x / y) < 1e12) { double r1 = fmod(x, y), r2 = bwc_fmod(x, y); if (r1 != r2 && !(isnan(r1) && isnan(r2))) { if (bad++ < 15) printf("fmod %.17g %.17g: %.17g %.17g\n", x, y, r1, r2); } } }
    }
    {   /* everything else MuJS prints */
        static const char* fi[] = { "%d", "%5d", "% 5d", "%-5d|", "%02d", "%04d", "%+d", "%x", "%X", "%02X", "%02x", "%u", "%o", "%#x", "%.0d", "%3.2d" };
        int v[] = { 0, 1, -1, 7, 42, -42, 255, 65535, 2147483647, -2147483647 - 1, 100000, 9 };
        int j, m;
        for (j = 0; j < (int)(sizeof fi / sizeof *fi); j++) for (m = 0; m < (int)(sizeof v / sizeof *v); m++) {
            snprintf(a, sizeof a, fi[j], v[m]); bwc_snprintf(b, sizeof b, fi[j], v[m]); n++;
            if (strcmp(a, b)) { if (bad++ < 30) printf("%s %d: libc '%s' bwc '%s'\n", fi[j], v[m], a, b); }
        }
        snprintf(a, sizeof a, "%s:%d: '%.*s' %c %% %-6s| %6s|%g %g %e %f %g %g", "f.js", 12, 3, "abcdef", 'z', "ab", "cd", 0.0, -0.0, 0.0, -0.0, 1e100, 1.0/0.0);
        bwc_snprintf(b, sizeof b, "%s:%d: '%.*s' %c %% %-6s| %6s|%g %g %e %f %g %g", "f.js", 12, 3, "abcdef", 'z', "ab", "cd", 0.0, -0.0, 0.0, -0.0, 1e100, 1.0/0.0);
        n++; if (strcmp(a, b)) { bad++; printf("mixed:\n  libc %s\n  bwc  %s\n", a, b); }
        bwc_snprintf(b, 5, "%s", "truncated"); n++; if (strcmp(b, "trun")) { bad++; printf("trunc '%s'\n", b); }
        if (bwc_strtol("  -123x", 0, 10) != -123 || bwc_strtol("0x1F", 0, 0) != 31 || bwc_strtol("+77", 0, 10) != 77) { bad++; printf("strtol\n"); }
    }
    {   /* the special values of the maths functions */
        double sv[] = { 0.0, -0.0, 1, -1, 0.5, -0.5, 2, -2, 3, -3, 1.5, 1e300, -1e300, 1e-300, 1.0/0.0, -1.0/0.0, 0.0/0.0, 10, 0.1, 710, -746, 64, 65, -65, 2.5, 1e20 };
        int ns = sizeof sv / sizeof *sv, j, m;
        for (j = 0; j < ns; j++) {
            double x = sv[j];
            n += 11;
            if (!same(bwc_sqrt(x), sqrt(x)) || !same(bwc_exp(x), exp(x)) || !same(bwc_log(x), log(x)) || !same(bwc_sin(x), sin(x))
                || !same(bwc_cos(x), cos(x)) || !same(bwc_tan(x), tan(x)) || !same(bwc_asin(x), asin(x)) || !same(bwc_acos(x), acos(x))
                || !same(bwc_atan(x), atan(x))) { bad++; printf("maths at %g\n", x); }
            for (m = 0; m < ns; m++) {
                double y = sv[m];
                if (!same(bwc_pow(x, y), pow(x, y)) && !(fabs(bwc_pow(x, y) / pow(x, y) - 1) < 1e-14)) { bad++; printf("pow(%g, %g) = %g, libc %g\n", x, y, bwc_pow(x, y), pow(x, y)); }
                if (!same(bwc_atan2(x, y), atan2(x, y))) { bad++; printf("atan2(%g, %g) = %g, libc %g\n", x, y, bwc_atan2(x, y), atan2(x, y)); }
            }
        }
    }
    {   /* strtod: what the host prints back, hard cases, exact halfway points, long digit strings */
        static const char* hard[] = { "2.2250738585072011e-308", "2.2250738585072012e-308", "4.9406564584124654e-324", "2.4703282292062327e-324",
            "2.4703282292062328e-324", "1.7976931348623157e308", "1.7976931348623158e308", "1.7976931348623159e308", "9007199254740993",
            "9007199254740995", "0.1", "1e23", "8.98846567431158e307", "123456789012345678901234567890", "0.3", "7e-10", "1e-400", "1e400",
            "0", "-0", "000123.4500e+2", ".5", "5.", "1e", "1e+", "-1.5e-3x", "  42", "0.000000000000000000000000000001", "179769313486231580793728971405301e276",
            "2.225073858507201136057409796709131975934819546351645648023426109724822222021076945516529523908135087914149158913039621106870086438694594645527657207407820621743379988141063267329253552286881372149012981122451451889849057222307285255133155755015914397476397983411801999323962548289017107081850690630666655994938275772572015763062690663332647565300009245888316433037779791869612049497390377829704905051080609940730262937128958950003583799967207254304360284078895771796150945516748243471030702609144621572289880258182545180325707018860872113128079512233426288368622321503775666622503982534335974568884423900265498198385487948292206894721689831099698365846814022854243330660339850886445804001034933970427567186443383770486037861622771738545623065874679014086723327636718751234567890123456789012345678901e-308" };
        int j;
        for (j = 0; j < (int)(sizeof hard / sizeof *hard); j++) {
            char *e1, *e2; double a1 = strtod(hard[j], &e1), a2 = bwc_strtod(hard[j], &e2); n++;
            if (!same(a1, a2) || e1 != e2) { bad++; printf("strtod(%.60s) %.17g, libc %.17g\n", hard[j], a2, a1); }
        }
        for (j = 0; j < 300000; j++) {
            double x = pick(j), y; char buf[1300]; static const char* pf[] = { "%.17g", "%.15g", "%.20e", "%.30g", "%.3g", "%.0e", "%.40f" };
            if (isnan(x) || isinf(x)) continue;
            if (j % 3 == 0) {   /* the exact midpoint to the next double up */
                long double m = ((long double)x + (long double)nextafter(x, 1.0/0.0)) / 2;
                if (isinf(nextafter(x, 1.0/0.0))) continue;
                snprintf(buf, sizeof buf, "%.1100Lg", m);
            } else snprintf(buf, sizeof buf, pf[j % 7], x);
            y = strtod(buf, 0); n++;
            if (!same(y, bwc_strtod(buf, 0))) { if (bad++ < 40) printf("strtod %s: %.17g libc %.17g\n", buf, bwc_strtod(buf, 0), y); }
        }
        for (j = 0; j < 20000; j++) {   /* random digit strings */
            char buf[1000]; int l = 1 + (int)(rnd() % 900), q; double y;
            for (q = 0; q < l; q++) buf[q] = (char)('0' + rnd() % 10);
            if (rnd() % 2) buf[rnd() % l] = '.';
            snprintf(buf + l, 20, "e%d", (int)(rnd() % 1300) - 650);
            y = strtod(buf, 0); n++;
            if (!same(y, bwc_strtod(buf, 0))) { if (bad++ < 40) printf("strtod %.50s...: %.17g libc %.17g\n", buf, bwc_strtod(buf, 0), y); }
        }
    }
    {   /* JavaScript rounding: half up on the exact value */
        char o[200];
        bwc_jsfmt(o, 12.5, 'f', 0); n++; if (strcmp(o, "13")) { bad++; printf("jsfmt 12.5 %s\n", o); }
        bwc_jsfmt(o, -2.5, 'f', 0); n++; if (strcmp(o, "-3")) { bad++; printf("jsfmt -2.5 %s\n", o); }
        bwc_jsfmt(o, 1.005, 'f', 2); n++; if (strcmp(o, "1.00")) { bad++; printf("jsfmt 1.005 %s\n", o); }
        bwc_jsfmt(o, 0.125, 'f', 2); n++; if (strcmp(o, "0.13")) { bad++; printf("jsfmt 0.125 %s\n", o); }
        bwc_jsfmt(o, 2.5, 'e', 0); n++; if (strcmp(o, "3e+00")) { bad++; printf("jsfmt 2.5e %s\n", o); }
        bwc_jsfmt(o, 0.0, 'f', 2); n++; if (strcmp(o, "0.00")) { bad++; printf("jsfmt 0 %s\n", o); }
    }
    printf("%d checks, %d differences\n", n, bad);
    return bad != 0;
}
