// Maps 9300: a baseline JPEG decoder for map tiles (ČÚZK, Mapy.com aerial), straight into a
// 16-bit (RGB565) bitmap.
//
// The phone's own decoder (ICL) took a median 171 ms per JPEG tile, one in ten 3 s or more (0.9
// log). Here: Huffman decoding with a 9-bit lookup table, the 8x8 integer IDCT of mp4player9300
// (FFmpeg's "simple IDCT", algorithm and constants by Michael Niedermayer, LGPL-2.1-or-later; the
// JPEG level shift is added to the DC term), the JFIF YCbCr -> RGB conversion with tables and
// nearest-neighbour chroma. Baseline and extended Huffman JPEGs (SOF0/SOF1), 8-bit, 1 or 3
// components, any sampling up to 2x2, restart intervals. Progressive or arithmetic ones:
// KJpegUnsupported (the caller then uses ICL). Portable C++, tested on a PC against PIL.

#include "jpegdec.h"
#include "jpegasm.h"

// ------------------------------------------------------------------ IDCT
// 1.12: the IDCTs in jpegasm.c (mp4player9300's full one, the half one; ARM assembly on the phone)

// 1.12: IdctHalf: jpeg_idct_half (jpegasm.c: ARM assembly on the phone)

// ------------------------------------------------------------------ the decoder

static const unsigned char KZigzag[64] =
    {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21,
    28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61,
    54, 47, 55, 62, 63
    };
// 1.13: for jpeg_block (jpegasm.c): the zigzag position | 1 << (8 + its row)
static const int KZtab[64] =
    {
    0x100, 0x101, 0x208, 0x410, 0x209, 0x102, 0x103, 0x20a,
    0x411, 0x818, 0x1020, 0x819, 0x412, 0x20b, 0x104, 0x105,
    0x20c, 0x413, 0x81a, 0x1021, 0x2028, 0x4030, 0x2029, 0x1022,
    0x81b, 0x414, 0x20d, 0x106, 0x107, 0x20e, 0x415, 0x81c,
    0x1023, 0x202a, 0x4031, 0x8038, 0x8039, 0x4032, 0x202b, 0x1024,
    0x81d, 0x416, 0x20f, 0x417, 0x81e, 0x1025, 0x202c, 0x4033,
    0x803a, 0x803b, 0x4034, 0x202d, 0x1026, 0x81f, 0x1027, 0x202e,
    0x4035, 0x803c, 0x803d, 0x4036, 0x202f, 0x4037, 0x803e, 0x803f,
    };

struct Huff
    {
    // codes up to 9 bits: fast[code9] = (length << 8) | value, 0 = longer
    unsigned int fast[512];     // int, not short: GCC 2.9 does 16-bit loads as two byte loads
    // 1.12: an AC symbol and its value bits both within 9 bits: (value * 4096) | (run << 8) | the bits
    // used (0 = not so: the usual way); EOB and ZRL never here
    int fastac[512];
    // longer codes, per length: the largest code, the index of its first value
    int maxcode[18];
    int valptr[17];
    int mincode[17];
    unsigned char vals[256];
    bool present;
    };

struct Comp
    {
    int id, h, v, tq, td, ta;
    int pred;
    unsigned char* plane;
    int stride, rows;
    JpegBlk blk;                // 1.13: jpeg_block's tables (set once a scan), its state per block
    };

struct Jpeg
    {
    const unsigned char* d;
    int len, pos;
    unsigned int bits;          // bit buffer (left-aligned in the low `nbits` bits)
    int nbits;
    bool marker;                // reached a marker inside the data: zeros from now on
    int qt[4][64];
    Huff dc[4], ac[4];
    Comp c[3];
    int ncomp, width, height, hmax, vmax, restart;
    int half;                   // 1.11: 1: half size (4x4 a block)
    int block[64];
    int crR[256], cbB[256], crG[256], cbG[256];
    // 1.12: a channel's value (-256..511) straight to its RGB565 bits, clipped: [v + 256]
    int r565[768], g565[768], b565[768];
    };

// The decoder's state lives at the start of the work buffer (an EKA1 DLL may have no writable
// static data), the planes after it.
static const int KStateBytes = (sizeof(Jpeg) + 15) & ~15;

static int Be16(const unsigned char* p) { return (p[0] << 8) | p[1]; }

static bool BuildHuff(Huff& h, const unsigned char* counts, const unsigned char* vals, int nvals)
    {
    for (int i = 0; i < 512; i++) h.fast[i] = 0;
    for (int i = 0; i < nvals && i < 256; i++) h.vals[i] = vals[i];
    int code = 0, k = 0;
    for (int l = 1; l <= 16; l++)
        {
        h.valptr[l] = k;
        h.mincode[l] = code;
        for (int i = 0; i < counts[l - 1]; i++)
            {
            if (k >= nvals) return false;
            if (l <= 9)
                {
                int shift = 9 - l, base = code << shift;
                for (int j = 0; j < (1 << shift); j++) h.fast[base + j] = (unsigned int) ((l << 8) | vals[k]);
                }
            code++;
            k++;
            }
        h.maxcode[l] = counts[l - 1] ? code - 1 : -1;
        code <<= 1;
        }
    h.maxcode[17] = 0x7fffffff;
    for (int i = 0; i < 512; i++)
        {
        unsigned int f = h.fast[i];
        int l = f >> 8, rs = f & 255, sz = rs & 15;
        h.fastac[i] = 0;
        if (f == 0 || sz == 0 || l + sz > 9) continue;
        int v = (i >> (9 - l - sz)) & ((1 << sz) - 1);
        if (v < (1 << (sz - 1))) v = v - (1 << sz) + 1;
        h.fastac[i] = v * 4096 + ((rs >> 4) << 8) + l + sz;
        }
    h.present = true;
    return true;
    }

// One 8x8 block of component aC into its plane at (aX, aY). The bit reader's state is kept in local
// variables here (in registers), not in the Jpeg struct: 0.10-0.13 loaded and stored it for every
// symbol through calls (0.13 log: 218 ms per ČÚZK tile, ~10x the PNG decoder's cost per pixel).
#define JFILL() \
    while (nbits <= 24) \
        { \
        unsigned int b_ = 0; \
        if (!marker && pos < len) \
            { \
            b_ = d[pos]; \
            if (b_ == 0xff) \
                { \
                if (pos + 1 < len && d[pos + 1] == 0) pos += 2;     /* a stuffed 0xFF */ \
                else { marker = true; b_ = 0; }                     /* a marker: stop */ \
                } \
            else pos++; \
            } \
        bits = (bits << 8) | b_; \
        nbits += 8; \
        }

// the next Huffman symbol of table aH into aOut (-1: a bad code)
#define JDECODE(aH, aOut) \
    { \
    if (nbits < 16) { JFILL(); } \
    int look_ = (int) ((bits >> (nbits - 9)) & 511); \
    unsigned int f_ = (aH).fast[look_]; \
    if (f_) { nbits -= f_ >> 8; aOut = (int) (f_ & 255); } \
    else \
        { \
        int code_ = look_, l_ = 9; \
        while (l_ < 16 && code_ > (aH).maxcode[l_]) { l_++; code_ = (code_ << 1) | (int) ((bits >> (nbits - l_)) & 1); } \
        if (code_ > (aH).maxcode[l_]) aOut = -1; \
        else \
            { \
            nbits -= l_; \
            int idx_ = (aH).valptr[l_] + code_ - (aH).mincode[l_]; \
            aOut = idx_ >= 0 && idx_ < 256 ? (aH).vals[idx_] : -1; \
            } \
        } \
    }

// aN (1..16) bits into aOut, sign-extended as JPEG does
#define JRECEIVE(aN, aOut) \
    { \
    if (nbits < (aN)) { JFILL(); } \
    int v_ = (int) ((bits >> (nbits - (aN))) & ((1u << (aN)) - 1)); \
    nbits -= (aN); \
    aOut = v_ < (1 << ((aN) - 1)) ? v_ - (1 << (aN)) + 1 : v_; \
    }

static bool BlockC(Jpeg& j, Comp& aC, int aX, int aY)
    {
    const Huff& dc = j.dc[aC.td];
    const Huff& ac = j.ac[aC.ta];
    const int* q = j.qt[aC.tq];
    int* b = j.block;
    unsigned int bits = j.bits;
    int nbits = j.nbits;
    const unsigned char* d = j.d;
    int pos = j.pos, len = j.len;
    bool marker = j.marker;
    bool ok = true;
    int t;
    JDECODE(dc, t);
    if (t < 0 || t > 11) ok = false;
    else
        {
        int diff = 0;
        if (t) JRECEIVE(t, diff);
        aC.pred += diff;
        b[0] = aC.pred * q[0] + 1024;        // + the level shift (128 x 8)
        int rows = 1;
        for (int k = 1; k < 64;)
            {
            // 1.12: the symbol and its value in one look-up when both fit in 9 bits (most of them)
            if (nbits < 16) { JFILL(); }
            int fa = ac.fastac[(bits >> (nbits - 9)) & 511];
            if (fa)
                {
                nbits -= fa & 255;
                k += (fa >> 8) & 15;
                if (k > 63) { ok = false; break; }
                int z = KZigzag[k];
                b[z] = (fa >> 12) * q[k];
                rows |= 1 << (z >> 3);
                k++;
                continue;
                }
            int rs;
            JDECODE(ac, rs);
            if (rs < 0) { ok = false; break; }
            int r = rs >> 4, sz = rs & 15;
            if (sz == 0)
                {
                if (r == 15) { k += 16; continue; }
                break;                       // end of block
                }
            k += r;
            if (k > 63) { ok = false; break; }
            int z = KZigzag[k];
            int v;
            JRECEIVE(sz, v);
            b[z] = v * q[k];
            rows |= 1 << (z >> 3);
            k++;
            }
        if (ok)
            {
            if (j.half) jpeg_idct_half(aC.plane + (aY >> 1) * aC.stride + (aX >> 1), aC.stride, b, rows);
            else jpeg_idct_put(aC.plane + aY * aC.stride + aX, aC.stride, b, rows);
            }
        else for (int i = 0; i < 64; i++) b[i] = 0;
        }
    j.bits = bits;
    j.nbits = nbits;
    j.pos = pos;
    j.marker = marker;
    return ok;
    }

// 1.13: the usual block (all codes in the 9-bit tables, no marker) in assembly (jpegasm.c), else the C
// above (a small function of its own: GCC 2.9 sets up all of BlockC's registers on entry, ~100
// instructions a block)
static bool Block(Jpeg& j, Comp& aC, int aX, int aY)
    {
    if (!j.marker)
        {
        JpegBlk& jb = aC.blk;
        jb.bits = j.bits; jb.nbits = j.nbits; jb.p = j.d + j.pos; jb.pred = aC.pred;
        if (jpeg_block(&jb))
            {
            j.bits = jb.bits; j.nbits = jb.nbits; j.pos = jb.p - j.d;
            aC.pred = jb.pred;
            if (j.half) jpeg_idct_half(aC.plane + (aY >> 1) * aC.stride + (aX >> 1), aC.stride, jb.b, jb.rows);
            else jpeg_idct_put(aC.plane + aY * aC.stride + aX, aC.stride, jb.b, jb.rows);
            return true;
            }
        }
    return BlockC(j, aC, aX, aY);
    }

int JpegInfo(const unsigned char* aData, int aLen, JpegHeader& aH)
    {
    if (aLen < 4 || aData[0] != 0xff || aData[1] != 0xd8) return KJpegNotJpeg;
    int pos = 2;
    while (pos + 4 <= aLen)
        {
        if (aData[pos] != 0xff) return KJpegCorrupt;
        int m = aData[pos + 1];
        if (m == 0xff) { pos++; continue; }
        int len = Be16(aData + pos + 2);
        if (m == 0xc0 || m == 0xc1)
            {
            if (pos + 4 + 6 > aLen) return KJpegCorrupt;
            const unsigned char* p = aData + pos + 4;
            if (p[0] != 8) return KJpegUnsupported;
            aH.height = Be16(p + 1);
            aH.width = Be16(p + 3);
            aH.components = p[5];
            if (aH.width <= 0 || aH.height <= 0 || aH.width > 4096 || aH.height > 4096) return KJpegCorrupt;
            if (aH.components != 1 && aH.components != 3) return KJpegUnsupported;
            int hmax = 1, vmax = 1;
            for (int i = 0; i < aH.components; i++)
                {
                if (pos + 4 + 6 + i * 3 + 2 >= aLen) return KJpegCorrupt;
                int hv = p[6 + i * 3 + 1];
                int h = hv >> 4, v = hv & 15;
                if (h < 1 || h > 2 || v < 1 || v > 2) return KJpegUnsupported;
                if (h > hmax) hmax = h;
                if (v > vmax) vmax = v;
                }
            int mw = (aH.width + 8 * hmax - 1) / (8 * hmax), mh = (aH.height + 8 * vmax - 1) / (8 * vmax);
            aH.workBytes = KStateBytes;
            for (int i = 0; i < aH.components; i++)
                {
                int hv = p[6 + i * 3 + 1];
                aH.workBytes += (mw * 8 * (hv >> 4)) * (mh * 8 * (hv & 15));
                }
            return KJpegOk;
            }
        if (m >= 0xc2 && m <= 0xcf && m != 0xc4 && m != 0xc8 && m != 0xcc) return KJpegUnsupported;   // progressive, lossless, arithmetic
        if (m == 0xd9 || m == 0xda) return KJpegCorrupt;
        pos += 2 + len;
        }
    return KJpegCorrupt;
    }

int JpegDecode565(const unsigned char* aData, int aLen, unsigned char* aWork, int aWorkLen,
    unsigned short* aOut, int aStride)
    {
    return JpegDecode565Scaled(aData, aLen, aWork, aWorkLen, aOut, aStride, 0);
    }

int JpegDecode565Scaled(const unsigned char* aData, int aLen, unsigned char* aWork, int aWorkLen,
    unsigned short* aOut, int aStride, int aHalf)
    {
    JpegHeader hd;
    int err = JpegInfo(aData, aLen, hd);
    if (err != KJpegOk) return err;
    if (aWorkLen < hd.workBytes) return KJpegNoMemory;
    Jpeg& j = *(Jpeg*) aWork;
    j.d = aData; j.len = aLen; j.restart = 0; j.ncomp = 0; j.half = aHalf ? 1 : 0;
    for (int i = 0; i < 4; i++) { j.dc[i].present = false; j.ac[i].present = false; }
    for (int i = 0; i < 64; i++) j.block[i] = 0;
    int pos = 2;
    bool scanned = false;
    while (pos + 4 <= aLen && !scanned)
        {
        if (aData[pos] != 0xff) return KJpegCorrupt;
        int m = aData[pos + 1];
        if (m == 0xff) { pos++; continue; }
        int len = Be16(aData + pos + 2);
        const unsigned char* p = aData + pos + 4;
        int end = pos + 2 + len;
        if (end > aLen) return KJpegCorrupt;
        if (m == 0xdb)                      // quantization tables
            {
            while (p < aData + end)
                {
                int pq = p[0] >> 4, tq = p[0] & 3;
                p++;
                for (int k = 0; k < 64; k++)
                    {
                    if (pq) { j.qt[tq][k] = Be16(p); p += 2; }
                    else j.qt[tq][k] = *p++;
                    }
                }
            }
        else if (m == 0xc4)                 // Huffman tables
            {
            while (p + 17 <= aData + end)
                {
                int tc = p[0] >> 4, th = p[0] & 3;
                int n = 0;
                for (int i = 1; i <= 16; i++) n += p[i];
                if (p + 17 + n > aData + end) return KJpegCorrupt;
                if (!BuildHuff(tc ? j.ac[th] : j.dc[th], p + 1, p + 17, n)) return KJpegCorrupt;
                p += 17 + n;
                }
            }
        else if (m == 0xdd) j.restart = Be16(p);
        else if (m == 0xc0 || m == 0xc1)
            {
            j.height = Be16(p + 1);
            j.width = Be16(p + 3);
            j.ncomp = p[5];
            j.hmax = j.vmax = 1;
            for (int i = 0; i < j.ncomp; i++)
                {
                Comp& c = j.c[i];
                c.id = p[6 + i * 3];
                c.h = p[6 + i * 3 + 1] >> 4;
                c.v = p[6 + i * 3 + 1] & 15;
                c.tq = p[6 + i * 3 + 2] & 3;
                if (c.h > j.hmax) j.hmax = c.h;
                if (c.v > j.vmax) j.vmax = c.v;
                }
            }
        else if (m == 0xda)                 // the scan
            {
            if (!j.ncomp) return KJpegCorrupt;
            int ns = p[0];
            if (ns != j.ncomp) return KJpegUnsupported;      // one interleaved scan only
            for (int i = 0; i < ns; i++)
                {
                int id = p[1 + i * 2], t = p[2 + i * 2];
                int k = 0;
                while (k < j.ncomp && j.c[k].id != id) k++;
                if (k == j.ncomp) return KJpegCorrupt;
                j.c[k].td = t >> 4 & 3;
                j.c[k].ta = t & 3;
                if (!j.dc[j.c[k].td].present || !j.ac[j.c[k].ta].present) return KJpegCorrupt;
                }
            j.pos = end;
            scanned = true;
            break;
            }
        else if (m == 0xd9) break;
        pos = end;
        }
    if (!scanned) return KJpegCorrupt;

    // the planes, MCU-aligned
    int mcuw = 8 * j.hmax, mcuh = 8 * j.vmax;
    int mx = (j.width + mcuw - 1) / mcuw, my = (j.height + mcuh - 1) / mcuh;
    unsigned char* w = aWork + KStateBytes;
    for (int i = 0; i < j.ncomp; i++)
        {
        Comp& c = j.c[i];
        c.stride = (mx * 8 * c.h) >> j.half;
        c.rows = (my * 8 * c.v) >> j.half;
        c.plane = w;
        w += c.stride * c.rows;
        c.pred = 0;
        JpegBlk& jb = c.blk;
        jb.end = j.d + j.len; jb.dcfast = j.dc[c.td].fast; jb.acfastac = j.ac[c.ta].fastac; jb.acfast = j.ac[c.ta].fast;
        jb.q = j.qt[c.tq]; jb.b = j.block; jb.ztab = KZtab;
        }
    j.bits = 0; j.nbits = 0; j.marker = false;
    int todo = j.restart;
    for (int y = 0; y < my; y++)
        for (int x = 0; x < mx; x++)
            {
            if (j.restart)
                {
                if (todo == 0)
                    {
                    // the restart marker: drop the rest of the byte, find RSTn, reset the predictors
                    j.nbits = 0; j.bits = 0; j.marker = false;
                    while (j.pos + 1 < j.len && !(j.d[j.pos] == 0xff && j.d[j.pos + 1] >= 0xd0 && j.d[j.pos + 1] <= 0xd7)) j.pos++;
                    if (j.pos + 1 >= j.len) return KJpegCorrupt;
                    j.pos += 2;
                    for (int i = 0; i < j.ncomp; i++) j.c[i].pred = 0;
                    todo = j.restart;
                    }
                todo--;
                }
            for (int i = 0; i < j.ncomp; i++)
                {
                Comp& c = j.c[i];
                for (int by = 0; by < c.v; by++)
                    for (int bx = 0; bx < c.h; bx++)
                        if (!Block(j, c, (x * c.h + bx) * 8, (y * c.v + by) * 8)) return KJpegCorrupt;
                }
            }

    // YCbCr (JFIF, full range) -> RGB565, chroma nearest-neighbour
    int* crR = j.crR;
    int* cbB = j.cbB;
    int* crG = j.crG;
    int* cbG = j.cbG;
    for (int i = 0; i < 256; i++)
        {
        int v = i - 128;
        crR[i] = (91881 * v + 32768) >> 16;          // 1.402
        cbB[i] = (116130 * v + 32768) >> 16;         // 1.772
        crG[i] = -46802 * v;                          // 0.71414
        cbG[i] = -22554 * v + 32768;                  // 0.34414
        }
    // 1.11: half size: the image's half (rounded up)
    const int ow = (j.width + j.half) >> j.half, oh = (j.height + j.half) >> j.half;
    if (j.ncomp == 1)
        {
        const Comp& c = j.c[0];
        for (int y = 0; y < oh; y++)
            {
            const unsigned char* py = c.plane + y * c.stride;
            unsigned short* o = aOut + y * aStride;
            for (int x = 0; x < ow; x++) { int g = py[x]; o[x] = (unsigned short) (((g & 0xf8) << 8) | ((g & 0xfc) << 3) | (g >> 3)); }
            }
        return KJpegOk;
        }
    const Comp& cy = j.c[0];
    const Comp& cb = j.c[1];
    const Comp& cr = j.c[2];
    // 1.12: the clipped channels' RGB565 bits from tables (no compares), and when two pixels side by
    // side share their chroma (sampled 2:1 across, the usual JPEG), its terms worked out once for both
    for (int i = 0; i < 768; i++)
        {
        int v = i - 256;
        if (v < 0) v = 0; else if (v > 255) v = 255;
        j.r565[i] = (v & 0xf8) << 8; j.g565[i] = (v & 0xfc) << 3; j.b565[i] = v >> 3;
        }
    const int* tr = j.r565 + 256;
    const int* tg = j.g565 + 256;
    const int* tb = j.b565 + 256;
    // a component's sample for image pixel x: x >> shift (sampling factors are 1 or 2; 0.10 divided
    // here, 3 divisions per pixel, and the ARM9 has no divide instruction: ~0.1 s per tile)
    int bx = j.hmax > cb.h, by = j.vmax > cb.v, rx = j.hmax > cr.h, ry = j.vmax > cr.v;
    int yx = j.hmax > cy.h, yy = j.vmax > cy.v;
    bool pairs = yx == 0 && bx == 1 && rx == 1;
    for (int y = 0; y < oh; y++)
        {
        const unsigned char* py = cy.plane + (y >> yy) * cy.stride;
        const unsigned char* pb = cb.plane + (y >> by) * cb.stride;
        const unsigned char* pr = cr.plane + (y >> ry) * cr.stride;
        unsigned short* o = aOut + y * aStride;
        int x = 0;
        // two pixels per 32-bit store (GCC 2.9 stores a 16-bit value as two bytes)
        if (pairs && ((unsigned long) o & 3) == 0)
            {
            // in assembly on the phone (jpegasm.c)
            JpegCc cc;
            cc.out = (unsigned int*) o; cc.py = py; cc.pb = pb; cc.pr = pr; cc.pairs = ow >> 1; cc.tabs = j.crR;
            jpeg_cc_pairs(&cc);
            x = cc.pairs * 2;
            }
        else if (((unsigned long) o & 3) == 0)
            {
            unsigned int* o2 = (unsigned int*) o;
            for (; x + 1 < ow; x += 2)
                {
                int Y = py[x >> yx], B = pb[x >> bx], R = pr[x >> rx];
                unsigned int p0 = (unsigned int) (tr[Y + crR[R]] | tg[Y + ((cbG[B] + crG[R]) >> 16)] | tb[Y + cbB[B]]);
                Y = py[(x + 1) >> yx]; B = pb[(x + 1) >> bx]; R = pr[(x + 1) >> rx];
                *o2++ = p0 | ((unsigned int) (tr[Y + crR[R]] | tg[Y + ((cbG[B] + crG[R]) >> 16)] | tb[Y + cbB[B]]) << 16);
                }
            }
        for (; x < ow; x++)
            {
            int Y = py[x >> yx], B = pb[x >> bx], R = pr[x >> rx];
            o[x] = (unsigned short) (tr[Y + crR[R]] | tg[Y + ((cbG[B] + crG[R]) >> 16)] | tb[Y + cbB[B]]);
            }
        }
    return KJpegOk;
    }
