// Maps 9300: a PNG decoder for map tiles into RGB565 (see pngdec.cpp). Portable C++.

#ifndef PNGDEC_H
#define PNGDEC_H

enum { KPngOk = 0, KPngNotPng = -1, KPngCorrupt = -2, KPngUnsupported = -3, KPngNoMemory = -4 };

struct PngHeader
    {
    int width, height, depth, colorType, interlace;
    int bitsPerPixel, rowBytes;
    int workBytes;              // the work buffer PngDecode565 needs: (rowBytes + 1) * height
    };

// Reads the header (IHDR). KPngOk, KPngNotPng (not a PNG at all), KPngCorrupt or KPngUnsupported.
int PngInfo(const unsigned char* aData, int aLen, PngHeader& aH);

// Decodes into aOut (aStride pixels per row, at least width x height). aWork: PngHeader::workBytes.
// Transparent pixels are blended over aBg (0xRRGGBB).
int PngDecode565(const unsigned char* aData, int aLen, unsigned char* aWork, int aWorkLen,
    unsigned short* aOut, int aStride, unsigned int aBg);

#endif
