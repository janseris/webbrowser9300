/* Browser 9300: the C half of MuJS's little C library (see shim/bwlibc.h).
 *
 * Strings, strtol, the IEEE helpers (isnan, floor, fmod, ...), setjmp/longjmp in ARM assembly,
 * and a vsnprintf with exact %f/%e/%g: the double is expanded to all of its decimal digits with
 * small base-10 arithmetic and rounded half-to-even on the exact value, so Number.toFixed,
 * toExponential and toPrecision give the same digits as glibc. No writable static data.
 *
 * The file also builds on a PC (BWC_HOST) so test/fmttest.c can compare it with the host's printf. */

#ifdef BWC_HOST
#include <stddef.h>
#include <stdarg.h>
#define BWC_HIGH_FIRST 0
#else
#include "bwlibc.h"
#define BWC_HIGH_FIRST 1        /* FPA doubles on the phone: the high word is stored first */
void ig_memcpy(void* dst, const void* src, unsigned int n);
void ig_memset(void* dst, int c, unsigned int n);
unsigned int ig_strlen(const char* s);
#endif

/* ---- strings ---------------------------------------------------------------------------- */

#ifndef BWC_HOST
void* bwc_calloc(size_t n, size_t s)
{
    void* p = bwc_malloc(n * s);
    if (p) bwc_memset(p, 0, n * s);
    return p;
}
#endif

#ifndef BWC_HOST
/* GCC itself calls memcpy (struct copies) and abort (unreachable code): those need real names */
#undef memcpy
#undef abort
void* memcpy(void* d, const void* s, size_t n) { ig_memcpy(d, s, n); return d; }
void abort(void) { bwc_abort(); for (;;) {} }
#define memcpy bwc_memcpy
#define abort bwc_abort
#endif

void* bwc_memcpy(void* d, const void* s, size_t n)
{
#ifdef BWC_HOST
    char* a = d; const char* b = s; while (n--) *a++ = *b++;
#else
    ig_memcpy(d, s, n);
#endif
    return d;
}

void* bwc_memset(void* d, int c, size_t n)
{
#ifdef BWC_HOST
    char* a = d; while (n--) *a++ = (char)c;
#else
    ig_memset(d, c, n);
#endif
    return d;
}

void* bwc_memmove(void* d, const void* s, size_t n)
{
    char* a = d; const char* b = s;
    if (a == b || !n) return d;
    if (a < b || a >= b + n) return bwc_memcpy(d, s, n);
    a += n; b += n;
    while (n--) *--a = *--b;
    return d;
}

int bwc_memcmp(const void* a, const void* b, size_t n)
{
    const unsigned char* x = a; const unsigned char* y = b;
    for (; n; n--, x++, y++) if (*x != *y) return *x - *y;
    return 0;
}

void* bwc_memchr(const void* s, int c, size_t n)
{
    const unsigned char* p = s;
    for (; n; n--, p++) if (*p == (unsigned char)c) return (void*)p;
    return 0;
}

size_t bwc_strlen(const char* s)
{
#ifdef BWC_HOST
    const char* p = s; while (*p) p++; return p - s;
#else
    return ig_strlen(s);
#endif
}

int bwc_strcmp(const char* a, const char* b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int bwc_strncmp(const char* a, const char* b, size_t n)
{
    for (; n; n--, a++, b++)
        {
        if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
        if (!*a) return 0;
        }
    return 0;
}

char* bwc_strchr(const char* s, int c)
{
    for (;; s++) { if (*s == (char)c) return (char*)s; if (!*s) return 0; }
}

char* bwc_strrchr(const char* s, int c)
{
    const char* r = 0;
    for (;; s++) { if (*s == (char)c) r = s; if (!*s) return (char*)r; }
}

char* bwc_strstr(const char* h, const char* n)
{
    size_t l = bwc_strlen(n);
    if (!l) return (char*)h;
    for (; *h; h++) if (*h == *n && !bwc_strncmp(h, n, l)) return (char*)h;
    return 0;
}

char* bwc_strcpy(char* d, const char* s) { char* r = d; while ((*d++ = *s++) != 0) {} return r; }
char* bwc_strcat(char* d, const char* s) { bwc_strcpy(d + bwc_strlen(d), s); return d; }

long bwc_strtol(const char* s, char** e, int b)
{
    const char* p = s;
    unsigned long v = 0;
    int neg = 0, any = 0;
    while (*p == ' ' || (*p >= 9 && *p <= 13)) p++;
    if (*p == '-' || *p == '+') neg = *p++ == '-';
    if ((b == 0 || b == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { p += 2; b = 16; }
    else if (b == 0) b = *p == '0' ? 8 : 10;
    for (;; p++)
        {
        int d = *p >= '0' && *p <= '9' ? *p - '0' : *p >= 'a' && *p <= 'z' ? *p - 'a' + 10
              : *p >= 'A' && *p <= 'Z' ? *p - 'A' + 10 : 99;
        if (d >= b) break;
        if (v > (0x7FFFFFFFul + neg - d) / b) v = 0x7FFFFFFFul + neg;      /* saturate */
        else v = v * b + d;
        any = 1;
        }
    if (e) *e = (char*)(any ? p : s);
    return neg ? -(long)v : (long)v;
}

/* ---- IEEE helpers ------------------------------------------------------------------------ */

typedef union { double d; unsigned int w[2]; } BwDbl;
#define BW_HI(u) ((u).w[BWC_HIGH_FIRST ? 0 : 1])
#define BW_LO(u) ((u).w[BWC_HIGH_FIRST ? 1 : 0])

int bwc_isnan(double x) { BwDbl u; u.d = x; return (BW_HI(u) & 0x7ff00000u) == 0x7ff00000u && ((BW_HI(u) & 0xfffffu) || BW_LO(u)); }
int bwc_isinf(double x) { BwDbl u; u.d = x; return (BW_HI(u) & 0x7fffffffu) == 0x7ff00000u && !BW_LO(u); }
int bwc_signbit(double x) { BwDbl u; u.d = x; return BW_HI(u) >> 31; }
double bwc_fabs(double x) { BwDbl u; u.d = x; BW_HI(u) &= 0x7fffffffu; return u.d; }

/* Clears the fraction bits: exact, no rounding */
double bwc_trunc(double x)
{
    BwDbl u;
    int e;
    u.d = x;
    e = (int)((BW_HI(u) >> 20) & 0x7ff) - 1023;
    if (e >= 52) return x;                                      /* integral, inf or nan */
    if (e < 0) { BW_HI(u) &= 0x80000000u; BW_LO(u) = 0; return u.d; }   /* |x| < 1: +-0 */
    if (e < 20) { BW_HI(u) &= ~(0x000fffffu >> e); BW_LO(u) = 0; }
    else BW_LO(u) &= ~(0xffffffffu >> (e - 20));
    return u.d;
}

double bwc_floor(double x) { double t = bwc_trunc(x); return x < 0 && t != x ? t - 1.0 : t; }
double bwc_ceil(double x)
{
    double t = bwc_trunc(x);
    if (x > 0 && t != x) return t + 1.0;
    if (x < 0 && t == 0) return -0.0;                            /* ceil(-0.5) is -0 */
    return t;
}

/* Exact remainder (shift-and-subtract on the binary values, like fmod in libm) */
double bwc_fmod(double x, double y)
{
    double ax, ay, r;
    if (bwc_isnan(x) || bwc_isnan(y) || bwc_isinf(x) || y == 0) return 0.0 / 0.0;
    if (bwc_isinf(y) || x == 0) return x;
    ax = bwc_fabs(x); ay = bwc_fabs(y);
    if (ax < ay) return x;
    r = ax;
    while (r >= ay)
        {
        double t = ay;
        while (t <= r * 0.5) t += t;                              /* doubling is exact */
        r -= t;                                                   /* r - t with t <= r < 2t: exact */
        }
    return x < 0 ? -r : r;
}

/* ---- the transcendental functions ---------------------------------------------------------
 * The special values (NaN, infinities, zeros, out of domain) are settled here, as C99 has them;
 * only finite in-domain arguments reach the bwp_ primitives (Math:: in src/jsplat.cpp). */

double bwp_sqrt(double x); double bwp_exp(double x); double bwp_log(double x); double bwp_pow(double x, double y);
double bwp_sin(double x); double bwp_cos(double x); double bwp_tan(double x);
double bwp_asin(double x); double bwp_acos(double x); double bwp_atan(double x); double bwp_atan2(double y, double x);

#define BW_NAN (0.0 / 0.0)
#define BW_INF (1.0 / 0.0)
#define BW_PI 3.141592653589793
#define BW_PI2 1.5707963267948966

static int bw_isint(double y) { return bwc_trunc(y) == y; }
static int bw_isodd(double y) { return bw_isint(y) && bwc_fabs(y) < 9007199254740992.0 && bwc_fmod(y, 2.0) != 0; }

double bwc_sqrt(double x) { if (x != x || x < 0) return BW_NAN; if (x == 0 || bwc_isinf(x)) return x; return bwp_sqrt(x); }
double bwc_exp(double x)
{
    if (x != x) return x;
    if (x > 709.8) return BW_INF;
    if (x < -745.2) return 0;
    if (x == 0) return 1;
    return bwp_exp(x);
}
double bwc_log(double x)
{
    if (x != x || x < 0) return BW_NAN;
    if (x == 0) return -BW_INF;
    if (bwc_isinf(x)) return x;
    if (x == 1) return 0;
    return bwp_log(x);
}

/* x^n by squaring for integral |n| <= 2^31 (exact while the results are) */
static double bw_powi(double x, double y)
{
    unsigned long n = (unsigned long)(y < 0 ? -y : y);
    double r = 1, b = x;
    while (n) { if (n & 1) r *= b; n >>= 1; if (n) b *= b; }
    return y < 0 ? 1 / r : r;
}

double bwc_pow(double x, double y)
{
    double ax = bwc_fabs(x), r;
    if (y == 0) return 1;
    if (x == 1) return 1;
    if (x != x || y != y) return BW_NAN;
    if (bwc_isinf(y))
        {
        if (ax == 1) return 1;
        return (ax > 1) == (y > 0) ? BW_INF : 0;
        }
    if (x == 0)
        {
        int neg = bwc_signbit(x) && bw_isodd(y);
        r = y > 0 ? 0.0 : BW_INF;
        return neg ? -r : r;
        }
    if (bwc_isinf(x))
        {
        int neg = x < 0 && bw_isodd(y);
        r = y > 0 ? BW_INF : 0.0;
        return neg ? -r : r;
        }
    if (x < 0 && !bw_isint(y)) return BW_NAN;
    if (bw_isint(y) && bwc_fabs(y) <= 64) return bw_powi(x, y);
    r = bwp_pow(ax, y);
    return x < 0 && bw_isodd(y) ? -r : r;
}

double bwc_sin(double x) { if (x != x || bwc_isinf(x)) return BW_NAN; if (x == 0) return x; return bwp_sin(x); }
double bwc_cos(double x) { if (x != x || bwc_isinf(x)) return BW_NAN; if (x == 0) return 1; return bwp_cos(x); }
double bwc_tan(double x) { if (x != x || bwc_isinf(x)) return BW_NAN; if (x == 0) return x; return bwp_tan(x); }
double bwc_asin(double x) { if (x != x || x > 1 || x < -1) return BW_NAN; if (x == 0) return x; return bwp_asin(x); }
double bwc_acos(double x) { if (x != x || x > 1 || x < -1) return BW_NAN; if (x == 1) return 0; return bwp_acos(x); }
double bwc_atan(double x)
{
    if (x != x) return x;
    if (bwc_isinf(x)) return x > 0 ? BW_PI2 : -BW_PI2;
    if (x == 0) return x;
    return bwp_atan(x);
}
double bwc_atan2(double y, double x)
{
    int sy = bwc_signbit(y), sx = bwc_signbit(x);
    double r;
    if (x != x || y != y) return BW_NAN;
    if (bwc_isinf(x) && bwc_isinf(y)) r = sx ? 3 * BW_PI / 4 : BW_PI / 4;
    else if (bwc_isinf(x)) r = sx ? BW_PI : 0;
    else if (bwc_isinf(y) || x == 0) { if (y == 0) r = sx ? BW_PI : 0; else r = BW_PI2; }
    else if (y == 0) r = sx ? BW_PI : 0;
    else return bwp_atan2(y, x);
    return sy ? -r : r;
}

/* ---- 64-bit helpers that the phone's EUSER does not export ---------------------------------
 * (it has __muldi3, __divdi3, the shifts; GCC also calls these six). In 32-bit words only, so
 * they never call themselves. Little-endian: a long long's low word comes first. */

/* n / d and n % d, n = nh:nl, d = dh:dl (d != 0); q and r get hi, lo */
void bwc_udivmod64(unsigned int nh, unsigned int nl, unsigned int dh, unsigned int dl, unsigned int* q, unsigned int* r)
{
    unsigned int qh = 0, ql = 0, rh = 0, rl = 0;
    int i;
    if (!nh && !dh) { q[0] = 0; q[1] = nl / dl; r[0] = 0; r[1] = nl % dl; return; }
    for (i = 63; i >= 0; i--)
        {
        unsigned int bit = i >= 32 ? (nh >> (i - 32)) & 1 : (nl >> i) & 1;
        rh = (rh << 1) | (rl >> 31);
        rl = (rl << 1) | bit;
        if (rh > dh || (rh == dh && rl >= dl))
            {
            unsigned int b = rl < dl;
            rl -= dl; rh -= dh + b;
            if (i >= 32) qh |= 1u << (i - 32); else ql |= 1u << i;
            }
        }
    q[0] = qh; q[1] = ql; r[0] = rh; r[1] = rl;
}

#ifndef BWC_HOST
typedef union { unsigned long long u; long long s; unsigned int w[2]; } BwU64;
unsigned long long __udivdi3(unsigned long long a, unsigned long long b)
{
    BwU64 x, y, z; unsigned int q[2], r[2];
    x.u = a; y.u = b;
    bwc_udivmod64(x.w[1], x.w[0], y.w[1], y.w[0], q, r);
    z.w[1] = q[0]; z.w[0] = q[1];
    return z.u;
}
unsigned long long __umoddi3(unsigned long long a, unsigned long long b)
{
    BwU64 x, y, z; unsigned int q[2], r[2];
    x.u = a; y.u = b;
    bwc_udivmod64(x.w[1], x.w[0], y.w[1], y.w[0], q, r);
    z.w[1] = r[0]; z.w[0] = r[1];
    return z.u;
}
int __cmpdi2(long long a, long long b)
{
    BwU64 x, y;
    x.s = a; y.s = b;
    if ((int)x.w[1] != (int)y.w[1]) return (int)x.w[1] < (int)y.w[1] ? 0 : 2;
    if (x.w[0] != y.w[0]) return x.w[0] < y.w[0] ? 0 : 2;
    return 1;
}
double __floatdidf(long long a)
{
    BwU64 x;
    x.s = a;
    /* hi * 2^32 is exact; adding the low word rounds once */
    return (double)(int)x.w[1] * 4294967296.0 + ((double)(x.w[0] >> 1) * 2.0 + (double)(x.w[0] & 1));
}
unsigned int __fixunsdfsi(double a)
{
    /* with signed conversions only (an unsigned one would call this again) */
    if (!(a > 0)) return 0;
    if (a >= 4294967296.0) return 0xffffffffu;
    if (a < 2147483648.0) return (unsigned int)(int)a;
    return (unsigned int)(int)(a - 2147483648.0) + 0x80000000u;
}
unsigned long long __fixunsdfdi(double a)
{
    BwU64 z;
    unsigned int hi;
    if (!(a > 0)) return 0;
    if (a >= 18446744073709551616.0) return ~0ull;
    hi = (unsigned int)(a / 4294967296.0);                      /* truncated */
    a -= (double)(hi >> 1) * 8589934592.0 + (double)(hi & 1) * 4294967296.0;
    if (a < 0) { hi--; a += 4294967296.0; }
    z.w[1] = hi;
    z.w[0] = (unsigned int)a;
    return z.u;
}
#endif

/* ---- setjmp / longjmp (ARM state, interworking returns) ---------------------------------- */

#ifndef BWC_HOST
__asm__(
"	.text\n"
"	.align 2\n"
"	.global bwc_setjmp\n"
"bwc_setjmp:\n"
"	stmia	r0, {r4-r11, sp, lr}\n"
"	mov	r0, #0\n"
"	bx	lr\n"
"	.global bwc_longjmp\n"
"bwc_longjmp:\n"
"	ldmia	r0, {r4-r11, sp, lr}\n"
"	movs	r0, r1\n"
"	moveq	r0, #1\n"
"	bx	lr\n"
);
#endif

/* ---- exact decimal digits of a double ---------------------------------------------------- */

/* Up to 767 significant digits (the smallest denormal times 2^53) */
#define BW_DIGITS 780

typedef struct
    {
    unsigned char d[BW_DIGITS];  /* digits, most significant first, no trailing zeros */
    int n;                       /* how many */
    int point;                   /* the value is 0.d[0]d[1]... * 10^point */
    } BwDec;

/* lsd-first base-10 number in t[0..len): t = t * m (m <= 625) */
static int bw_mul(unsigned char* t, int len, unsigned int m)
{
    unsigned int carry = 0;
    int i;
    for (i = 0; i < len; i++)
        {
        unsigned int v = t[i] * m + carry;
        t[i] = (unsigned char)(v % 10);
        carry = v / 10;
        }
    while (carry) { t[len++] = (unsigned char)(carry % 10); carry /= 10; }
    return len;
}

/* x finite and > 0 */
static void bw_exact(double x, BwDec* out)
{
    unsigned char t[BW_DIGITS];
    BwDbl u;
    unsigned int hi, lo;
    int e, len = 0, k = 0, i, b, lead;
    u.d = x;
    hi = BW_HI(u); lo = BW_LO(u);
    e = (int)((hi >> 20) & 0x7ff);
    hi &= 0xfffffu;
    if (e) { hi |= 0x100000u; e -= 1075; } else e = -1074;
    /* the 53-bit mantissa into decimal, a bit at a time */
    for (b = 52; b >= 0; b--)
        {
        int bit = b >= 32 ? (hi >> (b - 32)) & 1 : (lo >> b) & 1;
        unsigned int carry = (unsigned int)bit;
        for (i = 0; i < len; i++)
            {
            unsigned int v = t[i] * 2 + carry;
            t[i] = (unsigned char)(v >= 10 ? v - 10 : v);
            carry = v >= 10;
            }
        if (carry) t[len++] = 1;
        }
    if (e > 0)
        {
        for (; e >= 9; e -= 9) len = bw_mul(t, len, 512);
        if (e) len = bw_mul(t, len, 1u << e);
        }
    else if (e < 0)
        {
        k = -e;                                    /* m * 2^-k = m * 5^k / 10^k */
        for (e = k; e >= 4; e -= 4) len = bw_mul(t, len, 625);
        for (; e > 0; e--) len = bw_mul(t, len, 5);
        }
    out->point = len - k;
    for (lead = 0; lead < len && t[lead] == 0; lead++) {}      /* trailing zeros (lsd first) */
    out->n = len - lead;
    for (i = 0; i < out->n; i++) out->d[i] = t[len - 1 - i];
}

/* Rounds to keep digits on the exact value: half to even (printf), or half up (JavaScript's
 * toFixed / toExponential / toPrecision: "if there are two such n, pick the larger"). keep may be <= 0. */
static void bw_round(BwDec* v, int keep, int halfup)
{
    int up, i;
    if (keep >= v->n) return;
    if (keep < 0) { v->n = 0; return; }                        /* below half a unit: zero */
    if (halfup == 2) up = 1;                                   /* 2: up (some dropped digit is nonzero) */
    else up = v->d[keep] > 5 || (v->d[keep] == 5 && (halfup || keep + 1 < v->n || (keep > 0 && (v->d[keep - 1] & 1))));
    v->n = keep;
    if (!up) { while (v->n && v->d[v->n - 1] == 0) v->n--; return; }
    for (i = keep - 1; i >= 0; i--)
        {
        if (v->d[i] < 9) { v->d[i]++; v->n = i + 1; return; }
        }
    v->d[0] = 1; v->n = 1; v->point++;                         /* 999.. -> 1000.. */
}

static int bw_digit(const BwDec* v, int i) { return i >= 0 && i < v->n ? v->d[i] : 0; }

/* ---- vsnprintf ----------------------------------------------------------------------------- */

typedef struct { char* b; size_t n; size_t pos; } BwOut;

static void bw_put(BwOut* o, char c) { if (o->pos + 1 < o->n) o->b[o->pos] = c; o->pos++; }
static void bw_rep(BwOut* o, char c, int k) { while (k-- > 0) bw_put(o, c); }

/* The body s[0..len) with sign/prefix, padded to width */
static void bw_field(BwOut* o, const char* pre, const char* s, int len, int zeros, int width, int left, int zpad)
{
    int pl = 0, pad, i;
    while (pre[pl]) pl++;
    pad = width - pl - zeros - len;
    if (zpad && !left) { zeros += pad > 0 ? pad : 0; pad = 0; }
    if (!left) bw_rep(o, ' ', pad);
    for (i = 0; i < pl; i++) bw_put(o, pre[i]);
    bw_rep(o, '0', zeros);
    for (i = 0; i < len; i++) bw_put(o, s[i]);
    if (left) bw_rep(o, ' ', pad);
}

/* %f %e %g of x into buf (no sign); returns the length. buf must hold 1100 chars for %f of 1e308 */
static int bw_fmtdouble(char* buf, double x, char conv, int prec, int alt, int halfup)
{
    BwDec v;
    int n = 0, i, upper = conv >= 'A' && conv <= 'Z', strip = 0, exp10;
    char c = (char)(upper ? conv + 32 : conv);
    if (prec < 0) prec = 6;
    if (prec > 500) prec = 500;
    if (x == 0) { v.n = 0; v.point = 1; } else bw_exact(x, &v);
    if (c == 'g')
        {
        BwDec r = v;
        if (prec == 0) prec = 1;
        if (x != 0) bw_round(&r, prec, halfup);
        exp10 = x == 0 ? 0 : r.point - 1;
        if (exp10 < prec && exp10 >= -4) { c = 'f'; prec = prec - 1 - exp10; }
        else { c = 'e'; prec = prec - 1; }
        strip = !alt;
        }
    if (c == 'f')
        {
        if (x != 0) bw_round(&v, v.point + prec, halfup);
        if (v.n == 0) v.point = 1;
        if (v.point <= 0) buf[n++] = '0';
        else for (i = 0; i < v.point; i++) buf[n++] = (char)('0' + bw_digit(&v, i));
        if (prec || alt) buf[n++] = '.';
        for (i = 0; i < prec; i++) buf[n++] = (char)('0' + bw_digit(&v, v.point + i));
        }
    else
        {
        int ex;
        if (x != 0) bw_round(&v, prec + 1, halfup);
        ex = x == 0 ? 0 : v.point - 1;
        buf[n++] = (char)('0' + bw_digit(&v, 0));
        if (prec || alt) buf[n++] = '.';
        for (i = 1; i <= prec; i++) buf[n++] = (char)('0' + bw_digit(&v, i));
        if (strip && prec) { while (buf[n - 1] == '0') n--; if (buf[n - 1] == '.') n--; strip = 0; }
        buf[n++] = upper ? 'E' : 'e';
        buf[n++] = ex < 0 ? '-' : '+';
        if (ex < 0) ex = -ex;
        if (ex >= 100) buf[n++] = (char)('0' + ex / 100);
        buf[n++] = (char)('0' + ex / 10 % 10);
        buf[n++] = (char)('0' + ex % 10);
        }
    if (strip && prec) { while (buf[n - 1] == '0') n--; if (buf[n - 1] == '.') n--; }
    return n;
}

int bwc_vsnprintf(char* b, size_t n, const char* f, va_list a)
{
    BwOut o;
    o.b = b; o.n = n; o.pos = 0;
    for (; *f; f++)
        {
        int left = 0, plus = 0, space = 0, zpad = 0, alt = 0, width = 0, prec = -1, lng = 0;
        char conv;
        if (*f != '%') { bw_put(&o, *f); continue; }
        for (f++;; f++)
            {
            if (*f == '-') left = 1;
            else if (*f == '+') plus = 1;
            else if (*f == ' ') space = 1;
            else if (*f == '0') zpad = 1;
            else if (*f == '#') alt = 1;
            else break;
            }
        if (*f == '*') { width = va_arg(a, int); if (width < 0) { left = 1; width = -width; } f++; }
        else while (*f >= '0' && *f <= '9') width = width * 10 + *f++ - '0';
        if (*f == '.')
            {
            f++; prec = 0;
            if (*f == '*') { prec = va_arg(a, int); f++; }
            else while (*f >= '0' && *f <= '9') prec = prec * 10 + *f++ - '0';
            }
        while (*f == 'l' || *f == 'h' || *f == 'z' || *f == 'L') { if (*f == 'l') lng++; f++; }
        conv = *f;
        if (!conv) break;
        if (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'x' || conv == 'X' || conv == 'o' || conv == 'p')
            {
            char tmp[24];
            const char* pre = "";
            int len = 0, zeros, base = conv == 'x' || conv == 'X' || conv == 'p' ? 16 : conv == 'o' ? 8 : 10;
            unsigned long long uv;
            const char* dg = conv == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
            if (conv == 'p') { uv = (unsigned long)va_arg(a, void*); pre = "0x"; if (!uv) tmp[len++] = '0'; }
            else if (conv == 'd' || conv == 'i')
                {
                long long sv = lng >= 2 ? va_arg(a, long long) : lng ? (long long)va_arg(a, long) : (long long)va_arg(a, int);
                if (sv < 0) { pre = "-"; uv = (unsigned long long)-sv; }
                else { uv = (unsigned long long)sv; pre = plus ? "+" : space ? " " : ""; }
                }
            else uv = lng >= 2 ? va_arg(a, unsigned long long) : lng ? (unsigned long long)va_arg(a, unsigned long) : (unsigned long long)va_arg(a, unsigned int);
            if (uv >> 32)
                { while (uv) { tmp[len++] = dg[(int)(uv % (unsigned)base)]; uv /= (unsigned)base; } }
            else
                {
                unsigned long w = (unsigned long)uv;
                while (w) { tmp[len++] = dg[w % (unsigned)base]; w /= (unsigned)base; }
                }
            if (alt && base == 16 && len) pre = conv == 'X' ? "0X" : "0x";
            if (alt && base == 8) { tmp[len++] = '0'; }
            if (prec < 0 && !len) tmp[len++] = '0';
            zeros = prec > len ? prec - len : 0;
            {   /* reverse into place */
                char s[24]; int i;
                for (i = 0; i < len; i++) s[i] = tmp[len - 1 - i];
                bw_field(&o, pre, s, len, zeros, width, left, zpad && prec < 0);
            }
            }
        else if (conv == 'f' || conv == 'F' || conv == 'e' || conv == 'E' || conv == 'g' || conv == 'G')
            {
            char buf[1100];
            double x = va_arg(a, double);
            const char* pre = bwc_signbit(x) ? "-" : plus ? "+" : space ? " " : "";
            int len;
            if (x != x) { bw_field(&o, bwc_signbit(x) ? "-" : plus ? "+" : space ? " " : "", conv < 'a' ? "NAN" : "nan", 3, 0, width, left, 0); continue; }
            if (x < 0) x = -x;
            if (x > 1.7976931348623157e308) { bw_field(&o, pre, conv < 'a' ? "INF" : "inf", 3, 0, width, left, 0); continue; }
            len = bw_fmtdouble(buf, x, conv, prec, alt, 0);
            bw_field(&o, pre, buf, len, 0, width, left, zpad);
            }
        else if (conv == 's')
            {
            const char* s = va_arg(a, const char*);
            int len = 0;
            if (!s) s = "(null)";
            while (s[len] && (prec < 0 || len < prec)) len++;
            bw_field(&o, "", s, len, 0, width, left, 0);
            }
        else if (conv == 'c')
            {
            char c = (char)va_arg(a, int);
            bw_field(&o, "", &c, 1, 0, width, left, 0);
            }
        else bw_put(&o, conv);                                 /* %% and unknown ones */
        }
    if (n) b[o.pos < n ? o.pos : n - 1] = 0;
    return (int)o.pos;
}

int bwc_snprintf(char* b, size_t n, const char* f, ...)
{
    va_list a;
    int r;
    va_start(a, f);
    r = bwc_vsnprintf(b, n, f, a);
    va_end(a);
    return r;
}

/* MuJS's sprintf targets are fixed buffers of 32..64 bytes for numbers and dates */
int bwc_sprintf(char* b, const char* f, ...)
{
    va_list a;
    int r;
    va_start(a, f);
    r = bwc_vsnprintf(b, 0x7fffffff, f, a);
    va_end(a);
    return r;
}

/* JavaScript's Number formatting: x finite, conv 'f' or 'e', prec 0..100, half-up rounding of
 * the exact value; buf gets the C form ("1.50", "1.235e+02"), at most 420 chars */
int bwc_jsfmt(char* buf, double x, int conv, int prec)
{
    int n = 0;
    if (bwc_signbit(x) && x != 0) { buf[n++] = '-'; x = -x; }
    if (x < 0) x = -x;
    if (prec > 100) prec = 100;
    n += bw_fmtdouble(buf + n, x, (char)conv, prec, 0, 1);
    buf[n] = 0;
    return n;
}

/* ---- strtod, correctly rounded ------------------------------------------------------------
 * Up to 780 significant digits are kept (any further nonzero ones count as a trailing 1).
 * Short ones (15 digits, 10^+-22) are one exact double operation; the rest start from an
 * approximation that is corrected by comparing the exact value with the midpoints to the
 * neighbouring doubles in big integers (16-bit limbs, so no 64-bit arithmetic). */

#define BN_LIMBS 300
typedef struct { unsigned short w[BN_LIMBS]; int n; } BwBig;

static void bn_small(BwBig* b, unsigned int v) { b->n = 0; while (v) { b->w[b->n++] = (unsigned short)(v & 0xffff); v >>= 16; } }
/* b = b * m + add, m and add < 65536 */
static void bn_muladd(BwBig* b, unsigned int m, unsigned int add)
{
    unsigned int c = add;
    int i;
    for (i = 0; i < b->n; i++) { c += b->w[i] * m; b->w[i] = (unsigned short)(c & 0xffff); c >>= 16; }
    while (c && b->n < BN_LIMBS) { b->w[b->n++] = (unsigned short)(c & 0xffff); c >>= 16; }
}
static void bn_shl(BwBig* b, int k)
{
    int l = k >> 4, i;
    if (!b->n) return;
    if (l)
        {
        if (b->n + l > BN_LIMBS) l = BN_LIMBS - b->n;
        for (i = b->n - 1; i >= 0; i--) b->w[i + l] = b->w[i];
        for (i = 0; i < l; i++) b->w[i] = 0;
        b->n += l;
        }
    if (k & 15) bn_muladd(b, 1u << (k & 15), 0);
}
static void bn_pow10(BwBig* b, int e)
{
    for (; e >= 4; e -= 4) bn_muladd(b, 10000, 0);
    for (; e > 0; e--) bn_muladd(b, 10, 0);
}
static int bn_cmp(const BwBig* a, const BwBig* b)
{
    int i;
    if (a->n != b->n) return a->n < b->n ? -1 : 1;
    for (i = a->n - 1; i >= 0; i--) if (a->w[i] != b->w[i]) return a->w[i] < b->w[i] ? -1 : 1;
    return 0;
}

/* exact 10^k for 0 <= k <= 22 */
static double bw_p10(int k) { double r = 1; while (k-- > 0) r *= 10; return r; }
static double bw_scale10(double x, int s)
{
    while (s >= 22) { x *= 1e22; s -= 22; }
    while (s <= -22) { x /= 1e22; s += 22; }
    return s >= 0 ? x * bw_p10(s) : x / bw_p10(-s);
}

/* the digits dg[0..n) times 10^e compared with c * 2^j (c = chi:clo, a 55-bit integer) */
static int bw_cmpmid(const unsigned char* dg, int n, int e, unsigned int chi, unsigned int clo, int j)
{
    BwBig a, b;
    int i = 0, r;
    a.n = 0;
    r = n % 4;
    if (r) { unsigned int v = 0; for (; i < r; i++) v = v * 10 + dg[i]; bn_small(&a, v); }
    for (; i < n; i += 4) bn_muladd(&a, 10000, dg[i] * 1000u + dg[i + 1] * 100u + dg[i + 2] * 10u + dg[i + 3]);
    bn_small(&b, clo & 0xffff);
    b.w[1] = (unsigned short)(clo >> 16); b.w[2] = (unsigned short)(chi & 0xffff); b.w[3] = (unsigned short)(chi >> 16);
    b.n = 4; while (b.n && !b.w[b.n - 1]) b.n--;
    if (e > 0) bn_pow10(&a, e); else if (e < 0) bn_pow10(&b, -e);
    if (j > 0) bn_shl(&b, j); else if (j < 0) bn_shl(&a, -j);
    return bn_cmp(&a, &b);
}

double bwc_strtod(const char* s, char** ep)
{
    unsigned char dg[784];
    const char* p = s;
    int neg = 0, n = 0, e = 0, any = 0, sticky = 0, i;
    double x;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p == '-' || *p == '+') neg = *p++ == '-';
    for (; *p >= '0' && *p <= '9'; p++)
        {
        any = 1;
        if (n == 0 && *p == '0') continue;
        if (n < 780) dg[n++] = (unsigned char)(*p - '0'); else { e++; if (*p != '0') sticky = 1; }
        }
    if (*p == '.')
        for (p++; *p >= '0' && *p <= '9'; p++)
            {
            any = 1;
            if (n == 0 && *p == '0') { e--; continue; }
            if (n < 780) { dg[n++] = (unsigned char)(*p - '0'); e--; } else if (*p != '0') sticky = 1;
            }
    if (!any) { if (ep) *ep = (char*)s; return 0; }
    if (*p == 'e' || *p == 'E')
        {
        const char* q = p + 1;
        int es = 0, ev = 0;
        if (*q == '-' || *q == '+') es = *q++ == '-';
        if (*q >= '0' && *q <= '9')
            {
            for (; *q >= '0' && *q <= '9'; q++) if (ev < 100000) ev = ev * 10 + *q - '0';
            e += es ? -ev : ev;
            p = q;
            }
        }
    if (ep) *ep = (char*)p;
    if (sticky) { dg[n++] = 1; e--; }
    while (n && dg[n - 1] == 0) { n--; e++; }
    if (!n) return neg ? -0.0 : 0.0;
    if (n + e > 310) return neg ? -1.0 / 0.0 : 1.0 / 0.0;
    if (n + e < -324) return neg ? -0.0 : 0.0;
    if (n <= 15 && e >= -22 && e <= 22)
        {
        x = 0;
        for (i = 0; i < n; i++) x = x * 10 + dg[i];                /* exact below 2^53 */
        x = e >= 0 ? x * bw_p10(e) : x / bw_p10(-e);              /* one rounding */
        return neg ? -x : x;
        }
    {   /* approximately, from the first 17 digits */
        int k = n < 17 ? n : 17, sc = e + n - k;
        x = 0;
        for (i = 0; i < k; i++) x = x * 10 + dg[i];
        if (sc < -290) x = bw_scale10(x, sc + 290) / 1e290;
        else if (sc > 290) x = bw_scale10(x, sc - 290) * 1e290;
        else x = bw_scale10(x, sc);
        if (bwc_isinf(x)) x = 1.7976931348623157e308;
    }
    for (i = 0; i < 64; i++)
        {
        BwDbl u;
        unsigned int hi, lo, mh, ml, ch, cl;
        int be, k, c;
        u.d = x; hi = BW_HI(u); lo = BW_LO(u);
        if ((hi & 0x7ff00000u) == 0x7ff00000u) break;             /* rounded up to infinity */
        be = (int)(hi >> 20);
        mh = hi & 0xfffffu; ml = lo;
        if (be) { mh |= 0x100000u; k = be - 1075; } else k = -1074;
        /* up: (2m + 1) * 2^(k-1) */
        ch = (mh << 1) | (ml >> 31); cl = (ml << 1) | 1;
        c = bw_cmpmid(dg, n, e, ch, cl, k - 1);
        if (c > 0 || (c == 0 && (ml & 1)))
            {
            if (++lo == 0) hi++;
            BW_HI(u) = hi; BW_LO(u) = lo; x = u.d;
            continue;
            }
        if (!mh && !ml) break;                                    /* zero: nothing below */
        /* down: (2m - 1) * 2^(k-1), or (4m - 1) * 2^(k-2) at a power of two */
        if (mh == 0x100000u && !ml && be > 1)
            { ch = (mh << 2) | (ml >> 30); cl = (ml << 2) - 1; if (!(ml << 2)) ch--; c = bw_cmpmid(dg, n, e, ch, cl, k - 2); }
        else
            { ch = (mh << 1) | (ml >> 31); cl = (ml << 1); if (!cl) ch--; cl--; c = bw_cmpmid(dg, n, e, ch, cl, k - 1); }
        if (c < 0 || (c == 0 && (ml & 1)))
            {
            if (lo-- == 0) hi--;
            BW_HI(u) = hi; BW_LO(u) = lo; x = u.d;
            continue;
            }
        break;
        }
    return neg ? -x : x;
}

/* ---- shortest digits ------------------------------------------------------------------------
 * The fewest digits that read back as v (v > 0, finite), and of those the closest to v, as
 * JavaScript's Number to String needs (MuJS's Grisu2 gives 0.1 + 0.2 as 0.30000000000000007,
 * 5e-324 as 7e-324). buffer gets the digits (no terminator), *K the exponent: v = digits * 10^K.
 * A length k works when v rounded to k digits reads back as v; that holds from some k on
 * (17 always does; a k that works also works padded with a zero), so a binary search over
 * 1..17 finds it in about five read-backs. */
static int bw_trial(const BwDec* ex, int k, double v, char* out, int* K, int mode)
{
    BwDec r = *ex;
    char s[40];
    int i, n;
    bw_round(&r, k, mode);
    n = r.n;
    for (i = 0; i < n; i++) out[i] = (char)('0' + r.d[i]);
    *K = r.point - n;
    for (i = 0; i < n; i++) s[i] = out[i];
    s[n] = 'e';
    bwc_snprintf(s + n + 1, 8, "%d", *K);
    if (bwc_strtod(s, 0) == v) return n;
    /* at a power of two the double below is twice as close as the one above, so the nearest
     * k digits can fall short below v while the ones just above still read back */
    return mode == 0 && k < ex->n ? bw_trial(ex, k, v, out, K, 2) : 0;
}

int bwc_shortest(double v, char* buffer, int* K)
{
    BwDec ex;
    char tmp[24];
    int lo = 1, hi = 17, n = 0, kk = 0;
    bw_exact(v, &ex);
    if (ex.n <= 17) { int i; for (i = 0; i < ex.n; i++) buffer[i] = (char)('0' + ex.d[i]); *K = ex.point - ex.n; hi = ex.n; n = ex.n; kk = *K; }
    else { n = bw_trial(&ex, 17, v, buffer, K, 0); kk = *K; }
    while (lo < hi)
        {
        int mid = (lo + hi) / 2, k2, m = bw_trial(&ex, mid, v, tmp, &k2, 0);
        if (m) { int i; for (i = 0; i < m; i++) buffer[i] = tmp[i]; n = m; kk = k2; hi = mid; }
        else lo = mid + 1;
        }
    *K = kk;
    return n;
}
