/* Instagram 9300 - HTTP/1.1 response parsing (status line, headers, chunked bodies). C89. */

#include "igcore.h"

static const char* find_header_end(const char* data, unsigned int len)
{
    unsigned int i;
    for (i = 0; i + 3 < len; i++)
        if (data[i] == '\r' && data[i + 1] == '\n' && data[i + 2] == '\r' && data[i + 3] == '\n')
            return data + i + 4;
    /* tolerate bare LF line ends */
    for (i = 0; i + 1 < len; i++)
        if (data[i] == '\n' && data[i + 1] == '\n') return data + i + 2;
    return 0;
}

/* Finds the nth header line (case-insensitive name) inside the header block; returns a pointer
 * to the value (after ": " and spaces) and its length without CR/LF. */
static const char* find_header(const char* data, unsigned int headerLen, const char* name, int nth,
                               unsigned int* valueLen)
{
    unsigned int n = ig_strlen(name);
    unsigned int i = 0;
    /* skip the status line */
    while (i < headerLen && data[i] != '\n') i++;
    i++;
    while (i < headerLen) {
        unsigned int lineStart = i;
        unsigned int lineEnd = i;
        while (lineEnd < headerLen && data[lineEnd] != '\n') lineEnd++;
        if (lineEnd - lineStart > n && data[lineStart + n] == ':' &&
            ig_strieq_n(data + lineStart, name, n)) {
            if (nth == 0) {
                unsigned int v = lineStart + n + 1;
                unsigned int e = lineEnd;
                while (v < e && (data[v] == ' ' || data[v] == '\t')) v++;
                while (e > v && (data[e - 1] == '\r' || data[e - 1] == ' ')) e--;
                *valueLen = e - v;
                return data + v;
            }
            nth--;
        }
        i = lineEnd + 1;
    }
    return 0;
}

int ig_http_parse_head(const char* data, unsigned int len, IgHttpInfo* info)
{
    const char* end;
    const char* v;
    unsigned int vl;
    ig_memset(info, 0, sizeof(IgHttpInfo));
    info->contentLength = -1;
    end = find_header_end(data, len);
    if (!end) return 0;
    info->headersDone = 1;
    info->headerLen = (unsigned int)(end - data);
    /* "HTTP/1.1 200 OK" */
    if (len >= 12 && ig_memcmp(data, "HTTP/", 5) == 0) {
        unsigned int i = 5;
        while (i < info->headerLen && data[i] != ' ') i++;
        while (i < info->headerLen && data[i] == ' ') i++;
        if (i + 3 <= info->headerLen) {
            char t[4];
            ig_memcpy(t, data + i, 3);
            t[3] = 0;
            info->status = ig_atoi(t);
        }
        if (len >= 8 && data[7] == '0') info->closeAfter = 1;   /* HTTP/1.0 */
    }
    v = find_header(data, info->headerLen, "Transfer-Encoding", 0, &vl);
    if (v && vl >= 7 && ig_strieq_n(v, "chunked", 7)) info->chunked = 1;
    v = find_header(data, info->headerLen, "Content-Encoding", 0, &vl);
    if (v && vl >= 4 && ig_strieq_n(v, "gzip", 4)) info->gzip = 1;
    if (v && vl >= 6 && ig_strieq_n(v, "x-gzip", 6)) info->gzip = 1;
    v = find_header(data, info->headerLen, "Content-Length", 0, &vl);
    if (v) {
        char t[16];
        unsigned int n = vl < 15 ? vl : 15;
        ig_memcpy(t, v, n);
        t[n] = 0;
        info->contentLength = ig_atoi(t);
    }
    v = find_header(data, info->headerLen, "Connection", 0, &vl);
    if (v && vl >= 5 && ig_strieq_n(v, "close", 5)) info->closeAfter = 1;
    return 1;
}

char* ig_http_header_dup(const char* data, unsigned int headerLen, const char* name, int nth)
{
    unsigned int vl;
    const char* v = find_header(data, headerLen, name, nth, &vl);
    if (!v) return 0;
    return ig_strndup(v, vl);
}

char* ig_http_cookie_dup(const char* data, unsigned int headerLen, const char* cookieName)
{
    unsigned int n = ig_strlen(cookieName);
    int nth;
    for (nth = 0; nth < 64; nth++) {
        unsigned int vl;
        const char* v = find_header(data, headerLen, "Set-Cookie", nth, &vl);
        if (!v) break;
        if (vl > n && v[n] == '=' && ig_memcmp(v, cookieName, n) == 0) {
            unsigned int e = n + 1;
            while (e < vl && v[e] != ';') e++;
            return ig_strndup(v + n + 1, e - n - 1);
        }
    }
    return 0;
}

/* Walks the chunked body; returns 1 if the terminating zero chunk is present. */
static int chunked_complete(const char* body, unsigned int len)
{
    unsigned int i = 0;
    for (;;) {
        unsigned int size = 0, digits = 0;
        while (i < len && body[i] != '\r' && body[i] != '\n' && body[i] != ';') {
            char c = body[i];
            unsigned int h;
            if (c >= '0' && c <= '9') h = (unsigned int)(c - '0');
            else if (c >= 'a' && c <= 'f') h = (unsigned int)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') h = (unsigned int)(c - 'A' + 10);
            else return 0;      /* corrupt: treat as incomplete (read until close) */
            size = (size << 4) | h;
            digits++;
            i++;
        }
        if (!digits) return 0;
        while (i < len && body[i] != '\n') i++;
        if (i >= len) return 0;
        i++;
        if (size == 0) return 1;
        if (i + size + 2 > len) return 0;
        i += size;
        while (i < len && (body[i] == '\r' || body[i] == '\n')) i++;
    }
}

int ig_http_body_complete(const char* data, unsigned int len, const IgHttpInfo* info)
{
    if (!info->headersDone) return 0;
    if (info->chunked) return chunked_complete(data + info->headerLen, len - info->headerLen);
    if (info->contentLength >= 0) return (int)(len - info->headerLen) >= info->contentLength;
    return 0;
}

unsigned int ig_http_dechunk(char* body, unsigned int len)
{
    unsigned int i = 0, o = 0;
    for (;;) {
        unsigned int size = 0, digits = 0;
        while (i < len && body[i] != '\r' && body[i] != '\n' && body[i] != ';') {
            char c = body[i];
            unsigned int h;
            if (c >= '0' && c <= '9') h = (unsigned int)(c - '0');
            else if (c >= 'a' && c <= 'f') h = (unsigned int)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') h = (unsigned int)(c - 'A' + 10);
            else return o;
            size = (size << 4) | h;
            digits++;
            i++;
        }
        if (!digits) return o;
        while (i < len && body[i] != '\n') i++;
        if (i >= len) return o;
        i++;
        if (size == 0) return o;
        if (size > len - i) size = len - i;     /* truncated last chunk: keep what there is */
        if (size) {
            unsigned int k;
            for (k = 0; k < size; k++) body[o + k] = body[i + k];
            o += size;
            i += size;
        }
        while (i < len && (body[i] == '\r' || body[i] == '\n')) i++;
    }
}
