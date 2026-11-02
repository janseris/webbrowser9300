/* bwc_udivmod64 against the host's 64-bit division. gcc -O1 -DBWC_HOST u64test.c ../bwlibc.c */
#include <stdio.h>
#include <stdint.h>
void bwc_udivmod64(unsigned int nh, unsigned int nl, unsigned int dh, unsigned int dl, unsigned int* q, unsigned int* r);
double bwp_sqrt(double x) { return x; } double bwp_exp(double x) { return x; } double bwp_log(double x) { return x; }
double bwp_pow(double x, double y) { return x; } double bwp_sin(double x) { return x; } double bwp_cos(double x) { return x; }
double bwp_tan(double x) { return x; } double bwp_asin(double x) { return x; } double bwp_acos(double x) { return x; }
double bwp_atan(double x) { return x; } double bwp_atan2(double y, double x) { return x; }
static uint64_t rs = 88172645463325252ull;
static uint64_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }
int main(void)
{
    int i, bad = 0;
    for (i = 0; i < 2000000; i++) {
        uint64_t n = rnd() >> (rnd() % 64), d = rnd() >> (rnd() % 64); unsigned int q[2], r[2];
        if (!d) d = 1 + i;
        if (i % 7 == 0) d = 0x7fffffff;
        bwc_udivmod64(n >> 32, (unsigned)n, d >> 32, (unsigned)d, q, r);
        if ((((uint64_t)q[0] << 32) | q[1]) != n / d || (((uint64_t)r[0] << 32) | r[1]) != n % d) { if (bad++ < 5) printf("%llu / %llu\n", (unsigned long long)n, (unsigned long long)d); }
    }
    printf("%d differences\n", bad);
    return bad;
}
