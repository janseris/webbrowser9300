/* Shared plain C core (from Instagram 9300): memory, strings, buffers, UTF-8, HTTP response parsing. C89. *
 *
 * Written in C89 so it compiles with the phone's GCC 2.9 (Symbian 7.0s, EKA1) and with any
 * host compiler (test/coretest.c runs it under AddressSanitizer). No libc is used: memory
 * comes from ig_malloc/ig_free, which the Symbian side implements with User::Alloc/Free
 * (src/port.cpp). No writable static data (EKA1 DLL rule). */

#ifndef IGCORE_H
#define IGCORE_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- memory, provided by the port */
void* ig_malloc(unsigned int size);      /* NULL on failure */
void  ig_free(void* p);

/* ---------------------------------------------------------------- small libc replacements */
/* On the phone (ARM state) ig_memcpy, ig_memset and ig_strlen are the assembly ones in igarm.c */
#if defined(__MARM__) && !defined(__MARM_THUMB__) && !defined(IG_NO_ASM)
#define IG_ASM
#endif
void  ig_memcpy(void* dst, const void* src, unsigned int n);
void  ig_memset(void* dst, int c, unsigned int n);
int   ig_memcmp(const void* a, const void* b, unsigned int n);
unsigned int ig_strlen(const char* s);
int   ig_streq(const char* a, const char* b);                 /* 1 if equal */
int   ig_strieq_n(const char* a, const char* b, unsigned int n);  /* case-insensitive, n bytes */
char* ig_strdup(const char* s);                               /* ig_malloc'ed copy, NULL on failure */
char* ig_strndup(const char* s, unsigned int n);
int   ig_atoi(const char* s);                                 /* decimal, optional '-' */
void  ig_itoa(int v, char* out);                              /* out: at least 12 bytes */

/* ---------------------------------------------------------------- growable byte buffer */
typedef struct {
    char* data;
    unsigned int len;
    unsigned int cap;
    int failed;              /* set when an allocation failed; data may be short */
} IgBuf;

void ig_buf_init(IgBuf* b);
void ig_buf_free(IgBuf* b);
int  ig_buf_reserve(IgBuf* b, unsigned int extra);   /* 1 ok */
int  ig_buf_append(IgBuf* b, const void* p, unsigned int n);
int  ig_buf_puts(IgBuf* b, const char* s);
int  ig_buf_putc(IgBuf* b, char c);
int  ig_buf_put_int(IgBuf* b, int v);
int  ig_buf_put_urlenc(IgBuf* b, const char* s, unsigned int n);   /* form-urlencoding of UTF-8 bytes */
int  ig_buf_put_base64(IgBuf* b, const char* s, unsigned int n);
char* ig_buf_detach(IgBuf* b);   /* NUL-terminated, caller frees; NULL on failure */

/* ---------------------------------------------------------------- UTF-8 <-> UTF-16 */
/* Returns the number of UTF-16 units written (surrogate pairs for > U+FFFF). out may be NULL to
 * count. Invalid sequences become U+FFFD. */
unsigned int ig_utf8_to_utf16(const char* s, unsigned int n, unsigned short* out, unsigned int outMax);
/* Returns the number of bytes written; out may be NULL to count. */
unsigned int ig_utf16_to_utf8(const unsigned short* s, unsigned int n, char* out, unsigned int outMax);

/* ---------------------------------------------------------------- HTTP response */
typedef struct {
    int status;              /* 200 ...; 0 if the status line could not be parsed */
    int headersDone;         /* 1 once the full header block has been seen */
    unsigned int headerLen;  /* bytes up to and including the blank line */
    int chunked;
    int gzip;                /* Content-Encoding: gzip */
    int contentLength;       /* -1 if absent */
    int closeAfter;          /* "Connection: close" (or HTTP/1.0) */
} IgHttpInfo;

/* Looks at the start of a response. Returns 1 when the header block is complete. */
int ig_http_parse_head(const char* data, unsigned int len, IgHttpInfo* info);
/* Value of a header (case-insensitive name) copied without the trailing CR/LF, NULL if absent.
 * nth: 0 for the first occurrence (Set-Cookie repeats). */
char* ig_http_header_dup(const char* data, unsigned int headerLen, const char* name, int nth);
/* Value of a cookie from all Set-Cookie headers, NULL if absent. */
char* ig_http_cookie_dup(const char* data, unsigned int headerLen, const char* cookieName);
/* Returns 1 when the body is complete according to info (content-length or chunked terminator);
 * for neither, returns 0 (read until the connection closes). */
int ig_http_body_complete(const char* data, unsigned int len, const IgHttpInfo* info);
/* De-chunks in place; returns the new body length. Tolerates a truncated last chunk. */
unsigned int ig_http_dechunk(char* body, unsigned int len);

#ifdef __cplusplus
}
#endif

#endif
