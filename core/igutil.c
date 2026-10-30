/* Instagram 9300 - small libc replacements, growable buffer, UTF-8, base64, url-encoding. C89. */

#include "igcore.h"

void ig_memcpy(void* dst, const void* src, unsigned int n)
{
    char* d = (char*)dst;
    const char* s = (const char*)src;
    while (n--) *d++ = *s++;
}

void ig_memset(void* dst, int c, unsigned int n)
{
    char* d = (char*)dst;
    while (n--) *d++ = (char)c;
}

int ig_memcmp(const void* a, const void* b, unsigned int n)
{
    const unsigned char* x = (const unsigned char*)a;
    const unsigned char* y = (const unsigned char*)b;
    while (n--) {
        if (*x != *y) return (int)*x - (int)*y;
        x++; y++;
    }
    return 0;
}

unsigned int ig_strlen(const char* s)
{
    unsigned int n = 0;
    if (!s) return 0;
    while (s[n]) n++;
    return n;
}

int ig_streq(const char* a, const char* b)
{
    if (!a || !b) return a == b;
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static int ig_lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

int ig_strieq_n(const char* a, const char* b, unsigned int n)
{
    while (n--) {
        if (ig_lower((unsigned char)*a) != ig_lower((unsigned char)*b)) return 0;
        if (!*a) return 1;
        a++; b++;
    }
    return 1;
}

char* ig_strndup(const char* s, unsigned int n)
{
    char* r = (char*)ig_malloc(n + 1);
    if (!r) return 0;
    if (n) ig_memcpy(r, s, n);
    r[n] = 0;
    return r;
}

char* ig_strdup(const char* s)
{
    return ig_strndup(s ? s : "", ig_strlen(s));
}

int ig_atoi(const char* s)
{
    int v = 0, neg = 0;
    if (!s) return 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') {
        if (v > 214748363) { v = 2147483647; break; }
        v = v * 10 + (*s - '0');
        s++;
    }
    return neg ? -v : v;
}

void ig_itoa(int v, char* out)
{
    char tmp[12];
    int i = 0, j = 0;
    unsigned int u;
    if (v < 0) { out[j++] = '-'; u = (unsigned int)(-(v + 1)) + 1u; }
    else u = (unsigned int)v;
    do { tmp[i++] = (char)('0' + u % 10); u /= 10; } while (u);
    while (i) out[j++] = tmp[--i];
    out[j] = 0;
}

/* ---------------------------------------------------------------- buffer */

void ig_buf_init(IgBuf* b)
{
    b->data = 0; b->len = 0; b->cap = 0; b->failed = 0;
}

void ig_buf_free(IgBuf* b)
{
    if (b->data) ig_free(b->data);
    ig_buf_init(b);
}

int ig_buf_reserve(IgBuf* b, unsigned int extra)
{
    unsigned int need = b->len + extra + 1;
    unsigned int ncap;
    char* nd;
    if (need <= b->cap) return 1;
    ncap = b->cap ? b->cap : 256;
    while (ncap < need) ncap *= 2;
    nd = (char*)ig_malloc(ncap);
    if (!nd) { b->failed = 1; return 0; }
    if (b->len) ig_memcpy(nd, b->data, b->len);
    if (b->data) ig_free(b->data);
    b->data = nd;
    b->cap = ncap;
    return 1;
}

int ig_buf_append(IgBuf* b, const void* p, unsigned int n)
{
    if (!ig_buf_reserve(b, n)) return 0;
    if (n) ig_memcpy(b->data + b->len, p, n);
    b->len += n;
    b->data[b->len] = 0;
    return 1;
}

int ig_buf_puts(IgBuf* b, const char* s)
{
    return ig_buf_append(b, s, ig_strlen(s));
}

int ig_buf_putc(IgBuf* b, char c)
{
    return ig_buf_append(b, &c, 1);
}

int ig_buf_put_int(IgBuf* b, int v)
{
    char t[12];
    ig_itoa(v, t);
    return ig_buf_puts(b, t);
}

int ig_buf_put_urlenc(IgBuf* b, const char* s, unsigned int n)
{
    static const char hex[] = "0123456789ABCDEF";
    unsigned int i;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            if (!ig_buf_putc(b, (char)c)) return 0;
        } else if (c == ' ') {
            if (!ig_buf_putc(b, '+')) return 0;
        } else {
            char e[3];
            e[0] = '%'; e[1] = hex[c >> 4]; e[2] = hex[c & 15];
            if (!ig_buf_append(b, e, 3)) return 0;
        }
    }
    return 1;
}

int ig_buf_put_base64(IgBuf* b, const char* s, unsigned int n)
{
    static const char tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned int i;
    for (i = 0; i < n; i += 3) {
        unsigned int v = ((unsigned char)s[i]) << 16;
        char o[4];
        if (i + 1 < n) v |= ((unsigned char)s[i + 1]) << 8;
        if (i + 2 < n) v |= (unsigned char)s[i + 2];
        o[0] = tab[(v >> 18) & 63];
        o[1] = tab[(v >> 12) & 63];
        o[2] = (i + 1 < n) ? tab[(v >> 6) & 63] : '=';
        o[3] = (i + 2 < n) ? tab[v & 63] : '=';
        if (!ig_buf_append(b, o, 4)) return 0;
    }
    return 1;
}

char* ig_buf_detach(IgBuf* b)
{
    char* r;
    if (b->failed) { ig_buf_free(b); return 0; }
    if (!b->data) {
        r = (char*)ig_malloc(1);
        if (r) r[0] = 0;
        return r;
    }
    r = b->data;
    r[b->len] = 0;
    ig_buf_init(b);
    return r;
}

/* ---------------------------------------------------------------- UTF-8 */

static void put16(unsigned short* out, unsigned int outMax, unsigned int* n, unsigned int u)
{
    if (out && *n < outMax) out[*n] = (unsigned short)u;
    (*n)++;
}

unsigned int ig_utf8_to_utf16(const char* s, unsigned int n, unsigned short* out, unsigned int outMax)
{
    unsigned int i = 0, o = 0;
    while (i < n) {
        unsigned int c = (unsigned char)s[i];
        unsigned int cp, extra, k;
        if (c < 0x80) { cp = c; extra = 0; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
        else { put16(out, outMax, &o, 0xFFFD); i++; continue; }
        if (i + extra >= n && extra) { put16(out, outMax, &o, 0xFFFD); break; }   /* truncated */
        for (k = 1; k <= extra; k++) {
            unsigned int cc = (unsigned char)s[i + k];
            if ((cc & 0xC0) != 0x80) { cp = 0xFFFD; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        i += 1 + extra;
        if (cp == 0xFFFD) { put16(out, outMax, &o, 0xFFFD); continue; }
        if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) || (extra == 3 && cp < 0x10000) ||
            cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            put16(out, outMax, &o, 0xD800 | (cp >> 10));
            put16(out, outMax, &o, 0xDC00 | (cp & 0x3FF));
        } else {
            put16(out, outMax, &o, cp);
        }
    }
    return o;
}

static void put8(char* out, unsigned int outMax, unsigned int* n, unsigned int c)
{
    if (out && *n < outMax) out[*n] = (char)c;
    (*n)++;
}

unsigned int ig_utf16_to_utf8(const unsigned short* s, unsigned int n, char* out, unsigned int outMax)
{
    unsigned int i, o = 0;
    for (i = 0; i < n; i++) {
        unsigned int cp = s[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < n && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (s[i + 1] - 0xDC00);
            i++;
        } else if (cp >= 0xD800 && cp <= 0xDFFF) {
            cp = 0xFFFD;
        }
        if (cp < 0x80) put8(out, outMax, &o, cp);
        else if (cp < 0x800) {
            put8(out, outMax, &o, 0xC0 | (cp >> 6));
            put8(out, outMax, &o, 0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            put8(out, outMax, &o, 0xE0 | (cp >> 12));
            put8(out, outMax, &o, 0x80 | ((cp >> 6) & 0x3F));
            put8(out, outMax, &o, 0x80 | (cp & 0x3F));
        } else {
            put8(out, outMax, &o, 0xF0 | (cp >> 18));
            put8(out, outMax, &o, 0x80 | ((cp >> 12) & 0x3F));
            put8(out, outMax, &o, 0x80 | ((cp >> 6) & 0x3F));
            put8(out, outMax, &o, 0x80 | (cp & 0x3F));
        }
    }
    return o;
}
