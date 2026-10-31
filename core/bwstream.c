/* Browser 9300 - streaming decoders for downloads written to a file as they arrive (bigger than
 * the RAM): HTTP chunked framing, the gzip header. Fed any split of the bytes. C89. */

#include "bwcore.h"

enum { DC_SIZE = 0, DC_EXT, DC_SIZE_LF, DC_DATA, DC_DATA_END, DC_TRAILER, DC_DONE };

void bw_dechunk_init(BwDechunk* d)
{
    ig_memset(d, 0, sizeof(*d));
}

static int hexval(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* the size line ended */
static void size_line_done(BwDechunk* d)
{
    if (!d->digits) { d->error = 1; return; }
    if (d->remain == 0) { d->state = DC_TRAILER; d->lineLen = 0; }
    else d->state = DC_DATA;
}

unsigned int bw_dechunk(BwDechunk* d, const unsigned char* in, unsigned int n, unsigned char* out)
{
    unsigned int i = 0, o = 0;
    while (i < n && !d->error && d->state != DC_DONE) {
        unsigned char c = in[i];
        switch (d->state) {
        case DC_SIZE: {
            int v = hexval(c);
            if (v >= 0) {
                if (d->remain > 0x0FFFFFFFUL) { d->error = 1; break; }    /* > 256 MB a chunk: junk */
                d->remain = d->remain * 16 + (unsigned long)v;
                d->digits++;
            } else if (c == ';' || c == ' ' || c == '\t') d->state = DC_EXT;
            else if (c == '\r') d->state = DC_SIZE_LF;
            else if (c == '\n') size_line_done(d);
            else d->error = 1;
            i++;
            break;
        }
        case DC_EXT:
            if (c == '\n') size_line_done(d);
            i++;
            break;
        case DC_SIZE_LF:
            if (c == '\n') size_line_done(d); else d->error = 1;
            i++;
            break;
        case DC_DATA: {
            unsigned long k = n - i;
            if (k > d->remain) k = d->remain;
            ig_memcpy(out + o, in + i, (unsigned int)k);
            o += (unsigned int)k;
            i += (unsigned int)k;
            d->remain -= k;
            if (!d->remain) d->state = DC_DATA_END;
            break;
        }
        case DC_DATA_END:
            if (c == '\n') { d->state = DC_SIZE; d->remain = 0; d->digits = 0; }
            else if (c != '\r') d->error = 1;
            i++;
            break;
        case DC_TRAILER:
            if (c == '\n') {
                if (d->lineLen == 0) { d->state = DC_DONE; d->done = 1; }
                d->lineLen = 0;
            } else if (c != '\r') d->lineLen++;
            i++;
            break;
        default:
            i++;
            break;
        }
    }
    return o;
}

int bw_gzip_header_len(const unsigned char* d, unsigned int n)
{
    unsigned int pos = 10, flags;
    if (n >= 1 && d[0] != 0x1f) return -1;
    if (n >= 2 && d[1] != 0x8b) return -1;
    if (n >= 3 && d[2] != 8) return -1;              /* deflate */
    if (n < 10) return 0;
    flags = d[3];
    if (flags & 4) {                                  /* FEXTRA */
        if (n < pos + 2) return 0;
        pos += 2 + ((unsigned int)d[pos] | ((unsigned int)d[pos + 1] << 8));
        if (n < pos) return 0;
    }
    if (flags & 8) { while (pos < n && d[pos]) pos++; if (pos >= n) return 0; pos++; }    /* FNAME */
    if (flags & 16) { while (pos < n && d[pos]) pos++; if (pos >= n) return 0; pos++; }   /* FCOMMENT */
    if (flags & 2) { pos += 2; if (n < pos) return 0; }                                   /* FHCRC */
    return (int)pos;
}
