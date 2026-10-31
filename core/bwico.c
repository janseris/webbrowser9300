/* Browser 9300 - .ico files (favicons) read here instead of by the phone's image decoder, which
 * crashed the app (KERN-EXEC 3) on modern icons. C89, bounds-checked everywhere.
 *
 * An .ico file: ICONDIR (reserved 0, type 1 or 2, count), then count 16-byte entries
 * (width, height (0 = 256), colours, reserved, planes, bit count, bytes, offset). Each image is
 * either a PNG (since Vista) or a DIB: BITMAPINFOHEADER (height = 2 x the icon's: the colour
 * bitmap then a 1-bit AND mask), a palette for 1/4/8 bpp, rows bottom-up padded to 4 bytes. */

#include "bwcore.h"

static unsigned int rd16(const unsigned char* p) { return (unsigned int)p[0] | ((unsigned int)p[1] << 8); }
static unsigned long rd32(const unsigned char* p)
{
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

int bw_ico_is(const unsigned char* d, unsigned int n)
{
    return n >= 6 && d[0] == 0 && d[1] == 0 && (d[2] == 1 || d[2] == 2) && d[3] == 0 && rd16(d + 4) > 0;
}

int bw_ico_pick(const unsigned char* d, unsigned int n, int want, BwIcoImage* out)
{
    unsigned int count, i, best = 0;
    long bestScore = -1;
    ig_memset(out, 0, sizeof(*out));
    if (!bw_ico_is(d, n)) return BW_ICO_NONE;
    count = rd16(d + 4);
    if (count > 256 || 6 + count * 16 > n) return BW_ICO_NONE;
    /* the entry nearest the wanted size from above (it scales down cleanly), then the most bits */
    for (i = 0; i < count; i++) {
        const unsigned char* e = d + 6 + i * 16;
        int w = e[0] ? e[0] : 256;
        int bits = (int)rd16(e + 6);
        unsigned long size = rd32(e + 8), off = rd32(e + 12);
        long score;
        if (off >= n || size > n - off || size < 8) continue;
        if (w >= want) score = 100000L - (long)(w - want) * 100;
        else score = 50000L - (long)(want - w) * 100;
        score += bits > 32 ? 0 : bits;
        if (score > bestScore) { bestScore = score; best = i; }
    }
    if (bestScore < 0) return BW_ICO_NONE;
    {
        const unsigned char* e = d + 6 + best * 16;
        unsigned long size = rd32(e + 8), off = rd32(e + 12);
        const unsigned char* img = d + off;
        static const unsigned char kPng[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
        long w, h, hh;
        unsigned int bpp, comp, hdr, palN, rowBytes, maskRow, x, y;
        unsigned long need;
        const unsigned char* pal;
        const unsigned char* bits;
        const unsigned char* mask;
        unsigned long* px;
        int anyAlpha = 0;
        if (size >= 8 && ig_memcmp(img, kPng, 8) == 0) {
            out->kind = BW_ICO_PNG;
            out->pngOff = (unsigned int)off;
            out->pngLen = (unsigned int)size;
            return BW_ICO_PNG;
        }
        if (size < 40) return BW_ICO_NONE;
        hdr = (unsigned int)rd32(img);
        if (hdr < 40 || hdr > size) return BW_ICO_NONE;
        w = (long)rd32(img + 4);
        hh = (long)rd32(img + 8);
        bpp = rd16(img + 14);
        comp = (unsigned int)rd32(img + 16);
        palN = (unsigned int)rd32(img + 32);
        if (w <= 0 || w > 256 || hh <= 0 || hh > 512 || comp != 0) return BW_ICO_NONE;
        h = hh / 2;                                       /* the colour part; the AND mask follows */
        if (h <= 0) return BW_ICO_NONE;
        if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32) return BW_ICO_NONE;
        if (bpp <= 8) { if (palN == 0 || palN > (1u << bpp)) palN = 1u << bpp; }
        else palN = 0;
        rowBytes = (unsigned int)(((unsigned long)w * bpp + 31) / 32 * 4);
        maskRow = (unsigned int)((w + 31) / 32 * 4);
        need = hdr + palN * 4 + (unsigned long)rowBytes * h;
        if (need > size) return BW_ICO_NONE;
        pal = img + hdr;
        bits = pal + palN * 4;
        mask = need + (unsigned long)maskRow * h <= size ? bits + (unsigned long)rowBytes * h : 0;
        px = (unsigned long*)ig_malloc((unsigned int)(w * h * sizeof(unsigned long)));
        if (!px) return BW_ICO_NONE;
        for (y = 0; y < (unsigned int)h; y++) {
            const unsigned char* row = bits + (unsigned long)rowBytes * ((unsigned long)h - 1 - y);   /* bottom-up */
            for (x = 0; x < (unsigned int)w; x++) {
                unsigned long c;
                unsigned int idx;
                switch (bpp) {
                case 32: c = rd32(row + x * 4); if (c >> 24) anyAlpha = 1; break;
                case 24: c = 0xFF000000UL | (unsigned long)row[x * 3] | ((unsigned long)row[x * 3 + 1] << 8) | ((unsigned long)row[x * 3 + 2] << 16); break;
                default:
                    if (bpp == 8) idx = row[x];
                    else if (bpp == 4) idx = (row[x / 2] >> ((x & 1) ? 0 : 4)) & 15;
                    else idx = (row[x / 8] >> (7 - (x & 7))) & 1;
                    c = idx < palN ? (0xFF000000UL | (rd32(pal + idx * 4) & 0xFFFFFF)) : 0xFF000000UL;
                    break;
                }
                px[y * w + x] = c;                          /* 0xAARRGGBB (a DIB stores B, G, R) */
            }
        }
        /* the AND mask makes pixels transparent (32 bpp icons with alpha already say it) */
        if (mask && !anyAlpha) {
            for (y = 0; y < (unsigned int)h; y++) {
                const unsigned char* row = mask + (unsigned long)maskRow * ((unsigned long)h - 1 - y);
                for (x = 0; x < (unsigned int)w; x++)
                    if ((row[x / 8] >> (7 - (x & 7))) & 1) px[y * w + x] &= 0x00FFFFFFUL;
            }
        } else if (bpp == 32 && !anyAlpha) {
            for (x = 0; x < (unsigned int)(w * h); x++) px[x] |= 0xFF000000UL;   /* no alpha at all: opaque */
        }
        out->kind = BW_ICO_PIXELS;
        out->width = (int)w;
        out->height = (int)h;
        out->pixels = px;
        return BW_ICO_PIXELS;
    }
}
