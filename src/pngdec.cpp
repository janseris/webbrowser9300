// Maps 9300: a PNG decoder for map tiles, straight into a 16-bit (RGB565) bitmap.
//
// The phone's own decoder (ICL) took a median 0.41 s per 256x256 PNG tile from the cache, of
// which only ~15 ms is its setup (0.7 log); here: zlib's inflate (the phone's ezlib) + undoing the
// row filters + one table lookup per pixel. Portable C++ (no Symbian types), so it is tested on a
// PC against PIL (test/pngtest.cpp). Non-interlaced PNGs of every colour type and bit depth;
// transparency is blended over the map's background colour. Interlaced ones: KPngUnsupported
// (the caller then uses ICL).

#include "pngdec.h"
#include <zlib.h>

static unsigned int Be32(const unsigned char* p) { return ((unsigned int) p[0] << 24) | ((unsigned int) p[1] << 16) | ((unsigned int) p[2] << 8) | p[3]; }

static unsigned short Rgb565(int r, int g, int b) { return (unsigned short) (((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3)); }

// t / 255 rounded, without a division (the ARM9 has none: a library call per division)
static inline int Div255(int t) { t += 128; return (t + (t >> 8)) >> 8; }

// blends a colour with alpha a (0..255) over the background (aBg: 0xRRGGBB)
static unsigned short Blend(int r, int g, int b, int a, unsigned int aBg)
    {
    if (a == 255) return Rgb565(r, g, b);
    int br = (aBg >> 16) & 255, bg = (aBg >> 8) & 255, bb = aBg & 255;
    r = Div255(r * a + br * (255 - a));
    g = Div255(g * a + bg * (255 - a));
    b = Div255(b * a + bb * (255 - a));
    return Rgb565(r, g, b);
    }

static int Paeth(int a, int b, int c)
    {
    int p = a + b - c;
    int pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p, pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return a;
    return pb <= pc ? b : c;
    }

int PngInfo(const unsigned char* aData, int aLen, PngHeader& aH)
    {
    static const unsigned char sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    if (aLen < 33) return KPngCorrupt;
    for (int i = 0; i < 8; i++) if (aData[i] != sig[i]) return KPngNotPng;
    if (Be32(aData + 8) != 13 || aData[12] != 'I' || aData[13] != 'H' || aData[14] != 'D' || aData[15] != 'R') return KPngCorrupt;
    aH.width = (int) Be32(aData + 16);
    aH.height = (int) Be32(aData + 20);
    aH.depth = aData[24];
    aH.colorType = aData[25];
    aH.interlace = aData[28];
    int channels;
    switch (aH.colorType)
        {
        case 0: channels = 1; break;            // grey
        case 2: channels = 3; break;            // RGB
        case 3: channels = 1; break;            // palette
        case 4: channels = 2; break;            // grey + alpha
        case 6: channels = 4; break;            // RGBA
        default: return KPngCorrupt;
        }
    if (aH.width <= 0 || aH.height <= 0 || aH.width > 4096 || aH.height > 4096) return KPngCorrupt;
    if (aH.depth != 1 && aH.depth != 2 && aH.depth != 4 && aH.depth != 8 && aH.depth != 16) return KPngCorrupt;
    if (aH.interlace != 0) return KPngUnsupported;
    aH.bitsPerPixel = channels * aH.depth;
    aH.rowBytes = (aH.width * aH.bitsPerPixel + 7) / 8;
    aH.workBytes = (aH.rowBytes + 1) * aH.height;
    return KPngOk;
    }

int PngDecode565(const unsigned char* aData, int aLen, unsigned char* aWork, int aWorkLen,
    unsigned short* aOut, int aStride, unsigned int aBg)
    {
    PngHeader h;
    int err = PngInfo(aData, aLen, h);
    if (err != KPngOk) return err;
    if (aWorkLen < h.workBytes) return KPngNoMemory;

    // the chunks: palette, transparency, the compressed data (inflated as it comes)
    unsigned int pal[256];      // int, not short: the phone's GCC 2.9 turns 16-bit loads into byte loads
    unsigned char palR[256], palG[256], palB[256], palA[256];
    for (int i = 0; i < 256; i++) { palR[i] = palG[i] = palB[i] = 0; palA[i] = 255; }
    int palCount = 0;
    int trnsGrey = -1, trnsR = -1, trnsG = -1, trnsB = -1;      // a colour that's transparent (types 0, 2)
    z_stream zs;
    zs.zalloc = Z_NULL; zs.zfree = Z_NULL; zs.opaque = Z_NULL;
    zs.next_in = Z_NULL; zs.avail_in = 0;
    if (inflateInit(&zs) != Z_OK) return KPngNoMemory;
    zs.next_out = aWork;
    zs.avail_out = (unsigned int) h.workBytes;
    int pos = 8, zerr = Z_OK;
    bool ended = false;
    while (pos + 12 <= aLen && !ended)
        {
        int len = (int) Be32(aData + pos);
        const unsigned char* type = aData + pos + 4;
        const unsigned char* body = aData + pos + 8;
        if (len < 0 || pos + 12 + len > aLen) { inflateEnd(&zs); return KPngCorrupt; }
        if (type[0] == 'P' && type[1] == 'L' && type[2] == 'T' && type[3] == 'E')
            {
            palCount = len / 3;
            if (palCount > 256) palCount = 256;
            for (int i = 0; i < palCount; i++) { palR[i] = body[i * 3]; palG[i] = body[i * 3 + 1]; palB[i] = body[i * 3 + 2]; }
            }
        else if (type[0] == 't' && type[1] == 'R' && type[2] == 'N' && type[3] == 'S')
            {
            if (h.colorType == 3) for (int i = 0; i < len && i < 256; i++) palA[i] = body[i];
            else if (h.colorType == 0 && len >= 2) trnsGrey = (body[0] << 8) | body[1];
            else if (h.colorType == 2 && len >= 6) { trnsR = (body[0] << 8) | body[1]; trnsG = (body[2] << 8) | body[3]; trnsB = (body[4] << 8) | body[5]; }
            }
        else if (type[0] == 'I' && type[1] == 'D' && type[2] == 'A' && type[3] == 'T')
            {
            zs.next_in = (Bytef*) body;
            zs.avail_in = (unsigned int) len;
            while (zs.avail_in > 0 && zerr == Z_OK)
                {
                zerr = inflate(&zs, Z_NO_FLUSH);
                if (zerr == Z_STREAM_END) break;
                if (zerr == Z_BUF_ERROR && zs.avail_out == 0) break;     // more data than the image: ignored
                if (zerr != Z_OK) { inflateEnd(&zs); return KPngCorrupt; }
                }
            }
        else if (type[0] == 'I' && type[1] == 'E' && type[2] == 'N' && type[3] == 'D') ended = true;
        pos += 12 + len;
        }
    int got = h.workBytes - (int) zs.avail_out;
    inflateEnd(&zs);
    if (got < h.workBytes) return KPngCorrupt;
    if (h.colorType == 3 && palCount == 0) return KPngCorrupt;

    // the row filters, in place: each row is a filter byte, then rowBytes
    int bpp = h.bitsPerPixel >= 8 ? h.bitsPerPixel / 8 : 1;
    int rb = h.rowBytes;
    unsigned char* prev = 0;
    for (int y = 0; y < h.height; y++)
        {
        unsigned char* row = aWork + y * (rb + 1);
        int f = row[0];
        unsigned char* p = row + 1;
        switch (f)
            {
            case 0: break;
            case 1: for (int i = bpp; i < rb; i++) p[i] = (unsigned char) (p[i] + p[i - bpp]); break;
            case 2: if (prev) for (int i = 0; i < rb; i++) p[i] = (unsigned char) (p[i] + prev[i]); break;
            case 3:
                for (int i = 0; i < rb; i++)
                    {
                    int a = i >= bpp ? p[i - bpp] : 0, b = prev ? prev[i] : 0;
                    p[i] = (unsigned char) (p[i] + ((a + b) >> 1));
                    }
                break;
            case 4:
                for (int i = 0; i < rb; i++)
                    {
                    int a = i >= bpp ? p[i - bpp] : 0, b = prev ? prev[i] : 0, c = (prev && i >= bpp) ? prev[i - bpp] : 0;
                    p[i] = (unsigned char) (p[i] + Paeth(a, b, c));
                    }
                break;
            default: return KPngCorrupt;
            }
        prev = p;
        }

    // the pixels into RGB565
    if (h.colorType == 3)
        for (int i = 0; i < 256; i++) pal[i] = Blend(palR[i], palG[i], palB[i], palA[i], aBg);
    for (int y = 0; y < h.height; y++)
        {
        const unsigned char* p = aWork + y * (rb + 1) + 1;
        unsigned short* o = aOut + y * aStride;
        int w = h.width;
        if (h.colorType == 3)
            {
            if (h.depth == 8)
                {
                // two pixels per 32-bit store (GCC 2.9 stores a 16-bit value as two bytes); rows
                // of the bitmap start 4-byte aligned
                int x = 0;
                if (((unsigned long) o & 3) == 0)
                    for (; x + 1 < w; x += 2) *(unsigned int*) (o + x) = pal[p[x]] | (pal[p[x + 1]] << 16);
                for (; x < w; x++) o[x] = (unsigned short) pal[p[x]];
                }
            else
                {
                int d = h.depth, mask = (1 << d) - 1, ps = d == 1 ? 3 : d == 2 ? 2 : 1, per = 1 << ps;
                for (int x = 0; x < w; x++)
                    o[x] = (unsigned short) pal[(p[x >> ps] >> ((per - 1 - (x & (per - 1))) * d)) & mask];
                }
            }
        else if (h.colorType == 0)
            {
            for (int x = 0; x < w; x++)
                {
                int v, raw;
                if (h.depth == 16) { raw = (p[x * 2] << 8) | p[x * 2 + 1]; v = p[x * 2]; }
                else if (h.depth == 8) { raw = v = p[x]; }
                else
                    {
                    int d = h.depth, per = 8 / d;
                    raw = (p[x / per] >> ((per - 1 - x % per) * d)) & ((1 << d) - 1);
                    v = d == 1 ? raw * 255 : d == 2 ? raw * 85 : raw * 17;
                    }
                o[x] = Blend(v, v, v, raw == trnsGrey ? 0 : 255, aBg);
                }
            }
        else if (h.colorType == 2)
            {
            if (h.depth == 8)
                for (int x = 0; x < w; x++)
                    {
                    const unsigned char* q = p + x * 3;
                    bool t = q[0] == trnsR && q[1] == trnsG && q[2] == trnsB;
                    o[x] = t ? Blend(0, 0, 0, 0, aBg) : Rgb565(q[0], q[1], q[2]);
                    }
            else
                for (int x = 0; x < w; x++)
                    {
                    const unsigned char* q = p + x * 6;
                    bool t = ((q[0] << 8) | q[1]) == trnsR && ((q[2] << 8) | q[3]) == trnsG && ((q[4] << 8) | q[5]) == trnsB;
                    o[x] = t ? Blend(0, 0, 0, 0, aBg) : Rgb565(q[0], q[2], q[4]);
                    }
            }
        else if (h.colorType == 4)
            {
            int s = h.depth == 16 ? 4 : 2, ao = h.depth == 16 ? 2 : 1;
            for (int x = 0; x < w; x++) { const unsigned char* q = p + x * s; o[x] = Blend(q[0], q[0], q[0], q[ao], aBg); }
            }
        else
            {
            if (h.depth == 8)
                for (int x = 0; x < w; x++) { const unsigned char* q = p + x * 4; o[x] = Blend(q[0], q[1], q[2], q[3], aBg); }
            else
                for (int x = 0; x < w; x++) { const unsigned char* q = p + x * 8; o[x] = Blend(q[0], q[2], q[4], q[6], aBg); }
            }
        }
    return KPngOk;
    }
