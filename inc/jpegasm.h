/* Maps 9300 (1.12): the JPEG decoder's hot loops (jpegdec.cpp) in ARM assembly on the phone, C
 * elsewhere (jpegasm.c). The same results to the bit (checked in the emulator: test/arm/emujpg.py). */
#ifndef JPEGASM_H
#define JPEGASM_H

#ifdef __cplusplus
extern "C" {
#endif

/* half size: an 8x8 block (raster order, dequantized, the level shift in the DC; aRows: bit r = row r
 * may be non-zero) into 4x4 samples at aDest (aStride bytes a row, 4-byte aligned); the block is all
 * zero again afterwards */
void jpeg_idct_half(unsigned char* aDest, int aStride, int* aBlock, int aRows);

/* YCbCr -> RGB565 for pixel pairs that share their chroma: aPairs pairs from py (2 a pair), pb, pr
 * (1 a pair) into aOut (2 pixels a word). aTabs: the decoder's crR[256], cbB[256], crG[256], cbG[256],
 * then r565[768], g565[768], b565[768] (Jpeg::crR on: contiguous) */
typedef struct { unsigned int* out; const unsigned char* py; const unsigned char* pb; const unsigned char* pr;
    int pairs; const int* tabs; } JpegCc;
void jpeg_cc_pairs(const JpegCc* aP);

/* full size: the same into 8x8 samples (aDest 8-byte aligned, aStride a multiple of 8) */
void jpeg_idct_put(unsigned char* aDest, int aStride, int* aBlock, int aRows);

/* 1.13: one block's Huffman codes (baseline: the DC difference, the AC run/values to the end of block)
 * while every code is in the 9-bit tables and the data has no marker: the coefficients dequantized into
 * b (b[0] with the level shift), bits/nbits/p and pred advanced, rows (bit r: row r may be non-zero).
 * Returns 1; 0: not done (a longer code, a marker, the data's end, a bad value: the caller decodes the
 * block its usual way from the unchanged bits/nbits/p/pred; coefficients already put in b are ones it
 * puts there again). The bit buffer as jpegdec.cpp's: the low nbits bits of bits. ztab[k]: the
 * zigzag position z | 1 << (8 + z / 8). Offsets: bits 0, nbits 4, p 8, end 12, dcfast 16, acfastac 20,
 * acfast 24, q 28, b 32, ztab 36, pred 40, rows 44. */
typedef struct { unsigned int bits; int nbits; const unsigned char* p; const unsigned char* end;
    const unsigned int* dcfast; const int* acfastac; const unsigned int* acfast; const int* q; int* b;
    const int* ztab; int pred; int rows; } JpegBlk;
int jpeg_block(JpegBlk* aP);

void jpeg_idct_put_c(unsigned char* aDest, int aStride, int* aBlock, int aRows);
void jpeg_idct_half_c(unsigned char* aDest, int aStride, int* aBlock, int aRows);
void jpeg_cc_pairs_c(const JpegCc* aP);

#ifdef __cplusplus
}
#endif

#endif
