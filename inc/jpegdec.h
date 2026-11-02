// Maps 9300: a baseline JPEG decoder for map tiles into RGB565 (see jpegdec.cpp). Portable C++.

#ifndef JPEGDEC_H
#define JPEGDEC_H

enum { KJpegOk = 0, KJpegNotJpeg = -1, KJpegCorrupt = -2, KJpegUnsupported = -3, KJpegNoMemory = -4 };

struct JpegHeader
    {
    int width, height, components;
    int workBytes;              // the work buffer JpegDecode565 needs (the components' planes)
    };

// Reads the frame header. KJpegOk, KJpegNotJpeg, KJpegCorrupt or KJpegUnsupported (progressive...).
int JpegInfo(const unsigned char* aData, int aLen, JpegHeader& aH);

// Decodes into aOut (aStride pixels per row, at least width x height). aWork: JpegHeader::workBytes.
int JpegDecode565(const unsigned char* aData, int aLen, unsigned char* aWork, int aWorkLen,
    unsigned short* aOut, int aStride);

// 1.11: aHalf: at half the size ((width + 1) / 2 x (height + 1) / 2), each 8x8 block straight into
// 4x4 (the 2x2 averages from the lowest frequencies): ~2x faster, aWork as for the full size.
int JpegDecode565Scaled(const unsigned char* aData, int aLen, unsigned char* aWork, int aWorkLen,
    unsigned short* aOut, int aStride, int aHalf);

#endif
