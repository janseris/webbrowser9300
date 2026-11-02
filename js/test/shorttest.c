/* Prints random doubles (as bits) with bwc_shortest's digits, for shortcheck.py to compare with
 * Python's repr (shortest, closest). gcc -O1 -DBWC_HOST shorttest.c ../bwlibc.c -lm */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
int bwc_shortest(double v, char* buffer, int* K);
double bwp_sqrt(double x) { return x; } double bwp_exp(double x) { return x; } double bwp_log(double x) { return x; }
double bwp_pow(double x, double y) { return x; } double bwp_sin(double x) { return x; } double bwp_cos(double x) { return x; }
double bwp_tan(double x) { return x; } double bwp_asin(double x) { return x; } double bwp_acos(double x) { return x; }
double bwp_atan(double x) { return x; } double bwp_atan2(double y, double x) { return x; }
static uint64_t rs = 88172645463325252ull;
static uint64_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }
int main(void)
{
    int i;
    for (i = 0; i < 300000; i++) {
        double d; uint64_t u; char b[40]; int K, n;
        switch (i % 6) {
        case 0: case 1: u = rnd() & 0x7fffffffffffffffull; break;
        case 2: d = (double)(rnd() % 100000) / (double)(1 + rnd() % 1000); memcpy(&u, &d, 8); break;
        case 3: d = ldexp(1.0, (int)(rnd() % 2098) - 1074); memcpy(&u, &d, 8); break;   /* powers of two */
        case 4: u = rnd() & 0x000fffffffffffffull; if (!u) u = 1; break;              /* denormals */
        default: d = (double)(rnd() % 1000000) * pow(10, (int)(rnd() % 40) - 20); memcpy(&u, &d, 8); break;
        }
        memcpy(&d, &u, 8);
        if (isnan(d) || isinf(d) || d == 0) continue;
        n = bwc_shortest(d, b, &K); b[n] = 0;
        printf("%016llx %s %d\n", (unsigned long long)u, b, K);
    }
    return 0;
}
