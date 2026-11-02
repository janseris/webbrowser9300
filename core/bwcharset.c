/* Browser 9300 - charsets: UTF-8 and the 8-bit ones old Czech/western pages use. C89. */

#include "bwcore.h"
#include "bwcstab.h"

static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static int name_is(const char* s, unsigned int n, const char* lit)
{
    unsigned int i;
    for (i = 0; i < n; i++) {
        if (!lit[i] || lower((unsigned char)s[i]) != lit[i]) return 0;
    }
    return lit[n] == 0;
}

int bw_charset_from_name(const char* s, unsigned int n)
{
    if (name_is(s, n, "utf-8") || name_is(s, n, "utf8")) return BW_CS_UTF8;
    if (name_is(s, n, "windows-1250") || name_is(s, n, "cp1250") || name_is(s, n, "x-cp1250")) return BW_CS_1250;
    if (name_is(s, n, "iso-8859-2") || name_is(s, n, "iso8859-2") || name_is(s, n, "latin2") ||
        name_is(s, n, "iso_8859-2")) return BW_CS_8859_2;
    /* as browsers do: latin1 and ascii mean windows-1252 */
    if (name_is(s, n, "windows-1252") || name_is(s, n, "cp1252") || name_is(s, n, "iso-8859-1") ||
        name_is(s, n, "iso8859-1") || name_is(s, n, "latin1") || name_is(s, n, "us-ascii") ||
        name_is(s, n, "ascii") || name_is(s, n, "iso-8859-15") || name_is(s, n, "iso_8859-1")) return BW_CS_1252;
    return -1;
}

unsigned short bw_charset_char(int cs, unsigned char b)
{
    if (b < 0x80) return b;
    switch (cs) {
    case BW_CS_1250: return kCp1250[b - 0x80];
    case BW_CS_8859_2: return kIso8859_2[b - 0x80];
    case BW_CS_8859_1: return b;
    default: return kCp1252[b - 0x80];
    }
}

int bw_charset_byte(int cs, unsigned short u)
{
    const unsigned short* t;
    int i;
    if (u < 0x80) return u;
    if (cs == BW_CS_8859_1) return u < 0x100 ? u : -1;
    t = cs == BW_CS_1250 ? kCp1250 : cs == BW_CS_8859_2 ? kIso8859_2 : kCp1252;
    for (i = 0; i < 128; i++) if (t[i] == u) return 0x80 + i;
    return -1;
}

/* the value after "charset" at s (skips '=', spaces, quotes) */
static int charset_after(const char* s, unsigned int n)
{
    unsigned int i = 0, st;
    while (i < n && (s[i] == ' ' || s[i] == '=' || s[i] == '"' || s[i] == '\'')) i++;
    st = i;
    while (i < n && s[i] != '"' && s[i] != '\'' && s[i] != ';' && s[i] != ' ' && s[i] != '>' && s[i] != '/' &&
           s[i] != '\r' && s[i] != '\n') i++;
    if (i == st) return -1;
    return bw_charset_from_name(s + st, i - st);
}

static int find_ci(const char* s, unsigned int n, const char* lit, unsigned int from)
{
    unsigned int ln = ig_strlen(lit), i;
    if (n < ln) return -1;
    for (i = from; i + ln <= n; i++)
        if (lower((unsigned char)s[i]) == lit[0] && ig_strieq_n(s + i, lit, ln)) return (int)i;
    return -1;
}

int bw_charset_sniff(const char* contentType, const char* data, unsigned int len)
{
    int p;
    if (len >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF)
        return BW_CS_UTF8;
    if (contentType) {
        unsigned int n = ig_strlen(contentType);
        p = find_ci(contentType, n, "charset", 0);
        if (p >= 0) {
            int cs = charset_after(contentType + p + 7, n - (unsigned int)p - 7);
            if (cs >= 0) return cs;
        }
    }
    if (len > 4096) len = 4096;
    p = 0;
    for (;;) {
        int cs;
        p = find_ci(data, len, "charset", (unsigned int)p);
        if (p < 0) break;
        cs = charset_after(data + p + 7, len - (unsigned int)p - 7);
        if (cs >= 0) return cs;
        p += 7;
    }
    p = find_ci(data, len, "encoding=", 0);
    if (p >= 0 && p < 200) {
        int cs = charset_after(data + p + 8, len - (unsigned int)p - 8);
        if (cs >= 0) return cs;
    }
    return -1;
}

/* data in charset cs as UTF-8 (ig_malloc'ed, zero-terminated; NULL on out of memory) */
char* bw_charset_to_utf8(int cs, const char* data, unsigned int len, unsigned int* outLen)
{
    unsigned int i, n = 0;
    char* r;
    for (i = 0; i < len; i++) {
        unsigned char b = (unsigned char)data[i];
        unsigned short u = b < 0x80 || cs == BW_CS_UTF8 ? b : bw_charset_char(cs, b);
        n += u < 0x80 ? 1 : u < 0x800 ? 2 : 3;
    }
    r = (char*)ig_malloc(n + 1);
    if (!r) return 0;
    n = 0;
    for (i = 0; i < len; i++) {
        unsigned char b = (unsigned char)data[i];
        unsigned short u = b < 0x80 || cs == BW_CS_UTF8 ? b : bw_charset_char(cs, b);
        if (u < 0x80) r[n++] = (char)u;
        else if (u < 0x800) { r[n++] = (char)(0xC0 | (u >> 6)); r[n++] = (char)(0x80 | (u & 0x3F)); }
        else { r[n++] = (char)(0xE0 | (u >> 12)); r[n++] = (char)(0x80 | ((u >> 6) & 0x3F)); r[n++] = (char)(0x80 | (u & 0x3F)); }
    }
    r[n] = 0;
    *outLen = n;
    return r;
}
