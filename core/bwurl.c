/* Browser 9300 - URLs: resolving relative references, splitting, typed addresses. C89. */

#include "bwcore.h"

static int is_alpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int is_space(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

/* length of "scheme:" (with the colon) at s, 0 if there is none */
static unsigned int scheme_len(const char* s, unsigned int n)
{
    unsigned int i;
    if (!n || !is_alpha((unsigned char)s[0])) return 0;
    for (i = 1; i < n; i++) {
        char c = s[i];
        if (c == ':') return i + 1;
        if (!(is_alpha((unsigned char)c) || (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.')) return 0;
    }
    return 0;
}

/* appends p[0..n) with spaces / non-ASCII / backslashes fixed */
static void put_clean(IgBuf* b, const char* p, unsigned int n)
{
    static const char hex[] = "0123456789ABCDEF";
    unsigned int i;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)p[i];
        if (c == '\n' || c == '\r' || c == '\t') continue;
        if (c == '\\') c = '/';
        if (c <= ' ' || c >= 0x7f || c == '"' || c == '<' || c == '>') {
            ig_buf_putc(b, '%');
            ig_buf_putc(b, hex[c >> 4]);
            ig_buf_putc(b, hex[c & 15]);
        } else ig_buf_putc(b, (char)c);
    }
}

/* Removes "." and ".." segments from the path at b->data[from..] (up to '?' or '#'). */
static void remove_dots(IgBuf* b, unsigned int from)
{
    char* d;
    unsigned int end, i, o;
    if (b->failed || !b->data) return;
    d = b->data;
    end = from;
    while (end < b->len && d[end] != '?' && d[end] != '#') end++;
    /* in place: o is the output position; segments are copied one at a time */
    i = from; o = from;
    while (i < end) {
        /* a segment starts at i with '/' */
        unsigned int s = i + 1, e = s;
        while (e < end && d[e] != '/') e++;
        if (e - s == 1 && d[s] == '.') {
            if (e == end) { d[o++] = '/'; }
            i = e;
            continue;
        }
        if (e - s == 2 && d[s] == '.' && d[s + 1] == '.') {
            /* back to the previous '/' */
            while (o > from && d[o - 1] != '/') o--;
            if (o > from) o--;
            if (e == end) d[o++] = '/';
            i = e;
            continue;
        }
        {
            unsigned int k;
            for (k = i; k < e; k++) d[o++] = d[k];
        }
        i = e;
    }
    if (o == from) d[o++] = '/';
    /* move the query / fragment down */
    if (o != end) {
        unsigned int k;
        for (k = end; k < b->len; k++) d[o + k - end] = d[k];
        b->len = o + (b->len - end);
    }
}

/* the end of "scheme://authority" in an absolute URL (0 if not hierarchical) */
static unsigned int authority_end(const char* u, unsigned int n)
{
    unsigned int s = scheme_len(u, n), i;
    if (!s || s + 2 > n || u[s] != '/' || u[s + 1] != '/') return 0;
    i = s + 2;
    while (i < n && u[i] != '/' && u[i] != '?' && u[i] != '#') i++;
    return i;
}

char* bw_url_resolve(const char* base, const char* rel, unsigned int relLen)
{
    IgBuf b;
    unsigned int bn = base ? ig_strlen(base) : 0;
    unsigned int s, aend, pathStart;
    while (relLen && is_space((unsigned char)rel[0])) { rel++; relLen--; }
    while (relLen && is_space((unsigned char)rel[relLen - 1])) relLen--;
    ig_buf_init(&b);
    s = scheme_len(rel, relLen);
    if (s) {
        /* absolute: "http:x" without slashes is treated as relative to a base of the same scheme */
        unsigned int ra = authority_end(rel, relLen);
        if (ra) {
            /* lower-case the scheme and host */
            unsigned int i;
            for (i = 0; i < ra; i++) {
                char c = rel[i];
                if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
                ig_buf_putc(&b, c);
            }
            pathStart = b.len;
            if (ra == relLen || rel[ra] != '/') ig_buf_putc(&b, '/');
            put_clean(&b, rel + ra, relLen - ra);
            remove_dots(&b, pathStart);
        } else put_clean(&b, rel, relLen);       /* mailto:, javascript:, data: ... */
        return ig_buf_detach(&b);
    }
    aend = base ? authority_end(base, bn) : 0;
    if (!aend) { put_clean(&b, rel, relLen); return ig_buf_detach(&b); }
    if (relLen >= 2 && (rel[0] == '/' || rel[0] == '\\') && (rel[1] == '/' || rel[1] == '\\')) {
        unsigned int bs = scheme_len(base, bn);
        ig_buf_append(&b, base, bs);
        pathStart = b.len;
        put_clean(&b, rel, relLen);
        /* the path starts after the authority */
        {
            unsigned int i = pathStart + 2;
            while (i < b.len && b.data && b.data[i] != '/' && b.data[i] != '?' && b.data[i] != '#') i++;
            if (i == b.len || (b.data && b.data[i] != '/')) {
                /* no path: insert "/" */
                unsigned int tail = b.len - i;
                ig_buf_putc(&b, '/');
                if (b.data && tail) {
                    unsigned int k;
                    for (k = b.len - 1; k > i; k--) b.data[k] = b.data[k - 1];
                    b.data[i] = '/';
                }
            }
            remove_dots(&b, i);
        }
        return ig_buf_detach(&b);
    }
    ig_buf_append(&b, base, aend);
    pathStart = b.len;
    if (relLen && (rel[0] == '/' || rel[0] == '\\')) {
        put_clean(&b, rel, relLen);
    } else {
        /* the base path and query */
        unsigned int pEnd = aend, qEnd;
        while (pEnd < bn && base[pEnd] != '?' && base[pEnd] != '#') pEnd++;
        qEnd = pEnd;
        while (qEnd < bn && base[qEnd] != '#') qEnd++;
        if (relLen == 0) {
            ig_buf_append(&b, base + aend, qEnd - aend);
            if (qEnd == aend) ig_buf_putc(&b, '/');
        } else if (rel[0] == '#') {
            ig_buf_append(&b, base + aend, qEnd - aend);
            if (qEnd == aend) ig_buf_putc(&b, '/');
            put_clean(&b, rel, relLen);
        } else if (rel[0] == '?') {
            ig_buf_append(&b, base + aend, pEnd - aend);
            if (pEnd == aend) ig_buf_putc(&b, '/');
            put_clean(&b, rel, relLen);
        } else {
            /* the directory of the base path */
            unsigned int d = pEnd;
            while (d > aend && base[d - 1] != '/') d--;
            if (d == aend) ig_buf_putc(&b, '/');
            else ig_buf_append(&b, base + aend, d - aend);
            put_clean(&b, rel, relLen);
        }
    }
    remove_dots(&b, pathStart);
    return ig_buf_detach(&b);
}

int bw_url_split(const char* url, char* host, unsigned int hostMax, int* port, int* secure,
                 const char** path, unsigned int* pathLen)
{
    unsigned int n = ig_strlen(url), i, hs, he, pe;
    int p = 0;
    if (n >= 7 && ig_strieq_n(url, "http://", 7)) { *secure = 0; hs = 7; p = 80; }
    else if (n >= 8 && ig_strieq_n(url, "https://", 8)) { *secure = 1; hs = 8; p = 443; }
    else return 0;
    /* user:pass@ is skipped */
    i = hs;
    while (i < n && url[i] != '/' && url[i] != '?' && url[i] != '#') {
        if (url[i] == '@') hs = i + 1;
        i++;
    }
    he = i;
    /* port */
    {
        unsigned int k;
        for (k = hs; k < he; k++)
            if (url[k] == ':') {
                unsigned int m;
                int v = 0;
                for (m = k + 1; m < he; m++) {
                    if (url[m] < '0' || url[m] > '9') { v = -1; break; }
                    v = v * 10 + (url[m] - '0');
                    if (v > 65535) { v = -1; break; }
                }
                if (v > 0) p = v;
                he = k;
                break;
            }
    }
    if (he == hs || he - hs + 1 > hostMax) return 0;
    for (i = hs; i < he; i++) {
        char c = url[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        host[i - hs] = c;
    }
    host[he - hs] = 0;
    *port = p;
    /* path: from the first '/' or '?' after the authority, without the fragment */
    i = he;
    while (i < n && url[i] != '/' && url[i] != '?' && url[i] != '#') i++;
    pe = i;
    while (pe < n && url[pe] != '#') pe++;
    if (pe == i) { *path = "/"; *pathLen = 1; }
    else if (url[i] == '?') {
        /* "http://host?q": the path is "/" + the query, which is not a substring: rare, keep "/" */
        *path = url + i; *pathLen = pe - i;
    }
    else { *path = url + i; *pathLen = pe - i; }
    return 1;
}

const char* bw_url_fragment(const char* url)
{
    unsigned int i;
    for (i = 0; url[i]; i++) if (url[i] == '#') return url + i + 1;
    return 0;
}

int bw_url_same_page(const char* a, const char* b)
{
    unsigned int i = 0;
    const char* fb = bw_url_fragment(b);
    if (!fb) return 0;
    for (;;) {
        char ca = a[i], cb = b[i];
        if (cb == '#') return ca == 0 || ca == '#';
        if (ca != cb) return 0;
        if (!ca) return 0;
        i++;
    }
}

char* bw_url_from_input(const char* typed)
{
    unsigned int n = ig_strlen(typed), s;
    char* r;
    IgBuf b;
    while (n && is_space((unsigned char)typed[0])) { typed++; n--; }
    while (n && is_space((unsigned char)typed[n - 1])) n--;
    s = scheme_len(typed, n);
    /* "localhost:8000" looks like a scheme; a scheme followed by digits is a port */
    if (s && s < n && typed[s] >= '0' && typed[s] <= '9') s = 0;
    if (s) return bw_url_resolve(0, typed, n);
    ig_buf_init(&b);
    ig_buf_puts(&b, "http://");
    ig_buf_append(&b, typed, n);
    r = ig_buf_detach(&b);
    if (!r) return 0;
    {
        char* u = bw_url_resolve(0, r, ig_strlen(r));
        ig_free(r);
        return u;
    }
}
