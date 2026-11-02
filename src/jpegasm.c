/* Maps 9300 (1.12): the JPEG decoder's hot loops (jpegasm.h): ARM assembly on the phone, C elsewhere
 * (the reference: the same arithmetic as jpegdec.cpp 1.11). */

#include "fastmem.h"
#include "jpegasm.h"

static unsigned char Clip8(int v) { return (unsigned char) ((v & ~255) ? ((-v) >> 31) & 255 : v); }

void jpeg_idct_half_c(unsigned char* aDest, int aStride, int* aBlock, int aRows)
{
    int r;
    const int* f = aBlock;
    if (aRows == 1 && f[1] == 0 && f[2] == 0 && f[3] == 0)
    {
        int t = (2896 * f[0] + 4096) >> 13, n;
        unsigned char v = Clip8((2896 * t + 4096) >> 13);
        for (n = 0; n < 4; n++) { unsigned char* d = aDest + n * aStride; d[0] = d[1] = d[2] = d[3] = v; }
    }
    else
    {
        int t[16], v, m;
        for (v = 0; v < 4; v++)
        {
            int* tv = t + v * 4;
            const int* fv = aBlock + v * 8;
            int e0, e1, o0, o1;
            if (!(aRows & (1 << v))) { tv[0] = tv[1] = tv[2] = tv[3] = 0; continue; }
            e0 = 2896 * fv[0] + 2676 * fv[2] + 4096; e1 = 2896 * fv[0] - 2676 * fv[2] + 4096;
            o0 = 3711 * fv[1] + 1303 * fv[3]; o1 = 1537 * fv[1] - 3146 * fv[3];
            tv[0] = (e0 + o0) >> 13; tv[3] = (e0 - o0) >> 13; tv[1] = (e1 + o1) >> 13; tv[2] = (e1 - o1) >> 13;
        }
        for (m = 0; m < 4; m++)
        {
            const int* c = t + m;
            unsigned char* d = aDest + m;
            int e0 = 2896 * c[0] + 2676 * c[8] + 4096, e1 = 2896 * c[0] - 2676 * c[8] + 4096;
            int o0 = 3711 * c[4] + 1303 * c[12], o1 = 1537 * c[4] - 3146 * c[12];
            d[0] = Clip8((e0 + o0) >> 13); d[aStride] = Clip8((e1 + o1) >> 13);
            d[2 * aStride] = Clip8((e1 - o1) >> 13); d[3 * aStride] = Clip8((e0 - o0) >> 13);
        }
    }
    for (r = 0; r < 8; r++)
        if (aRows & (1 << r))
        {
            int* p = aBlock + 8 * r;
            p[0] = p[1] = p[2] = p[3] = p[4] = p[5] = p[6] = p[7] = 0;
        }
}


/* 1.12: the full 8x8 IDCT (jpegdec.cpp's IdctRow / IdctCol / IdctPut up to 1.11: mp4player9300's,
 * FFmpeg's "simple IDCT" by Michael Niedermayer, LGPL-2.1-or-later) */
#define W1 22725
#define W2 21407
#define W3 19266
#define W4 16383
#define W5 12873
#define W6 8867
#define W7 4520
#define ROW_SHIFT 11
#define COL_SHIFT 20
#define DC_SHIFT 3

static void IdctRow(int* row)
{
    int a0, a1, a2, a3, b0, b1, b2, b3;
    if (!(row[1] | row[2] | row[3] | row[4] | row[5] | row[6] | row[7]))
    {
        int v = (short) (row[0] * (1 << DC_SHIFT));
        row[0] = row[1] = row[2] = row[3] = row[4] = row[5] = row[6] = row[7] = v;
        return;
    }
    a0 = (W4 * row[0]) + (1 << (ROW_SHIFT - 1));
    a1 = a0; a2 = a0; a3 = a0;
    a0 += W2 * row[2];
    a1 += W6 * row[2];
    a2 -= W6 * row[2];
    a3 -= W2 * row[2];
    b0 = W1 * row[1] + W3 * row[3];
    b1 = W3 * row[1] - W7 * row[3];
    b2 = W5 * row[1] - W1 * row[3];
    b3 = W7 * row[1] - W5 * row[3];
    if (row[4] | row[5] | row[6] | row[7])
    {
        a0 +=  W4 * row[4] + W6 * row[6];
        a1 += -W4 * row[4] - W2 * row[6];
        a2 += -W4 * row[4] + W2 * row[6];
        a3 +=  W4 * row[4] - W6 * row[6];
        b0 +=  W5 * row[5] + W7 * row[7];
        b1 += -W1 * row[5] - W5 * row[7];
        b2 +=  W7 * row[5] + W3 * row[7];
        b3 +=  W3 * row[5] - W1 * row[7];
    }
    row[0] = (short) ((a0 + b0) >> ROW_SHIFT);
    row[7] = (short) ((a0 - b0) >> ROW_SHIFT);
    row[1] = (short) ((a1 + b1) >> ROW_SHIFT);
    row[6] = (short) ((a1 - b1) >> ROW_SHIFT);
    row[2] = (short) ((a2 + b2) >> ROW_SHIFT);
    row[5] = (short) ((a2 - b2) >> ROW_SHIFT);
    row[3] = (short) ((a3 + b3) >> ROW_SHIFT);
    row[4] = (short) ((a3 - b3) >> ROW_SHIFT);
}

static void IdctCol(const int* col, int* out, int hi)
{
    int a0, a1, a2, a3, b0, b1, b2, b3;
    a0 = W4 * (col[8 * 0] + ((1 << (COL_SHIFT - 1)) / W4));
    a1 = a0; a2 = a0; a3 = a0;
    a0 +=  W2 * col[8 * 2];
    a1 +=  W6 * col[8 * 2];
    a2 += -W6 * col[8 * 2];
    a3 += -W2 * col[8 * 2];
    b0 = W1 * col[8 * 1];
    b1 = W3 * col[8 * 1];
    b2 = W5 * col[8 * 1];
    b3 = W7 * col[8 * 1];
    b0 +=  W3 * col[8 * 3];
    b1 += -W7 * col[8 * 3];
    b2 += -W1 * col[8 * 3];
    b3 += -W5 * col[8 * 3];
    if (hi)
    {
        if (col[8 * 4]) { a0 += W4 * col[8 * 4]; a1 -= W4 * col[8 * 4]; a2 -= W4 * col[8 * 4]; a3 += W4 * col[8 * 4]; }
        if (col[8 * 5]) { b0 += W5 * col[8 * 5]; b1 -= W1 * col[8 * 5]; b2 += W7 * col[8 * 5]; b3 += W3 * col[8 * 5]; }
        if (col[8 * 6]) { a0 += W6 * col[8 * 6]; a1 -= W2 * col[8 * 6]; a2 += W2 * col[8 * 6]; a3 -= W6 * col[8 * 6]; }
        if (col[8 * 7]) { b0 += W7 * col[8 * 7]; b1 -= W5 * col[8 * 7]; b2 += W3 * col[8 * 7]; b3 -= W1 * col[8 * 7]; }
    }
    out[0] = (a0 + b0) >> COL_SHIFT;
    out[1] = (a1 + b1) >> COL_SHIFT;
    out[2] = (a2 + b2) >> COL_SHIFT;
    out[3] = (a3 + b3) >> COL_SHIFT;
    out[4] = (a3 - b3) >> COL_SHIFT;
    out[5] = (a2 - b2) >> COL_SHIFT;
    out[6] = (a1 - b1) >> COL_SHIFT;
    out[7] = (a0 - b0) >> COL_SHIFT;
}

void jpeg_idct_put_c(unsigned char* aDest, int aStride, int* aBlock, int aRows)
{
    int o[8], i, k, r;
    for (i = 0; i < 8; i++) if (aRows & (1 << i)) IdctRow(aBlock + i * 8);
    if (aRows == 1)
    {
        for (i = 0; i < 8; i++) o[i] = Clip8((W4 * (aBlock[i] + ((1 << (COL_SHIFT - 1)) / W4))) >> COL_SHIFT);
        for (k = 0; k < 8; k++)
        {
            unsigned char* d = aDest + k * aStride;
            for (i = 0; i < 8; i++) d[i] = (unsigned char) o[i];
        }
    }
    else
    {
        int hi = aRows & 0xF0;
        for (i = 0; i < 8; i++)
        {
            unsigned char* d = aDest + i;
            IdctCol(aBlock + i, o, hi);
            for (k = 0; k < 8; k++) { d[0] = Clip8(o[k]); d += aStride; }
        }
    }
    for (r = 0; r < 8; r++)
        if (aRows & (1 << r))
        {
            int* p = aBlock + 8 * r;
            p[0] = p[1] = p[2] = p[3] = p[4] = p[5] = p[6] = p[7] = 0;
        }
}

void jpeg_cc_pairs_c(const JpegCc* aP)
{
    const int* crR = aP->tabs;
    const int* cbB = crR + 256;
    const int* crG = crR + 512;
    const int* cbG = crR + 768;
    const int* tr = crR + 1024 + 256;
    const int* tg = tr + 768;
    const int* tb = tr + 1536;
    const unsigned char* py = aP->py;
    const unsigned char* pb = aP->pb;
    const unsigned char* pr = aP->pr;
    unsigned int* o = aP->out;
    int n;
    for (n = aP->pairs; n > 0; n--)
    {
        int B = *pb++, R = *pr++;
        int cR = crR[R], cG = (cbG[B] + crG[R]) >> 16, cB = cbB[B];
        int Y0 = py[0], Y1 = py[1];
        py += 2;
        *o++ = (unsigned int) (tr[Y0 + cR] | tg[Y0 + cG] | tb[Y0 + cB])
            | ((unsigned int) (tr[Y1 + cR] | tg[Y1 + cG] | tb[Y1 + cB]) << 16);
    }
}

#ifdef FM_ASM

__asm__(
"	.text\n"
"	.align 2\n"
/* jpeg_idct_half: r0 dest, r1 stride, r2 block, r3 rows; the 4x4 middle values on the stack (64
 * bytes); the constants from .Ljh_k. */
"	.global jpeg_idct_half\n"
"jpeg_idct_half:\n"
"	stmfd	sp!, {r2-r11, lr}\n"         /* block, rows at [sp] (for the zeroing) */
"	sub	sp, sp, #64\n"
"	cmp	r3, #1\n"
"	bne	.Ljh_full\n"
"	ldmia	r2, {r4-r7}\n"
"	orrs	r8, r5, r6\n"
"	orreqs	r8, r8, r7\n"
"	bne	.Ljh_full\n"
/* the DC alone */
"	ldr	r8, .Ljh_k\n"                /* 2896 */
"	mul	r9, r4, r8\n"
"	add	r9, r9, #4096\n"
"	mov	r9, r9, asr #13\n"
"	mul	r4, r9, r8\n"
"	add	r4, r4, #4096\n"
"	mov	r4, r4, asr #13\n"
"	movs	ip, r4, asr #8\n"
"	mvnne	r4, ip, asr #31\n"
"	and	r4, r4, #255\n"
"	orr	r4, r4, r4, lsl #8\n"
"	orr	r4, r4, r4, lsl #16\n"
"	str	r4, [r0]\n"
"	str	r4, [r0, r1]\n"
"	add	r0, r0, r1, lsl #1\n"
"	str	r4, [r0]\n"
"	str	r4, [r0, r1]\n"
"	b	.Ljh_zero\n"
".Ljh_full:\n"
/* the rows: t[v][0..3] for v 0..3 (lr: t, r2 the row, r3 the rows' bits, ip the count) */
"	mov	lr, sp\n"
"	mov	ip, #4\n"
".Ljh_row:\n"
"	tst	r3, #1\n"
"	bne	.Ljh_rowc\n"
"	mov	r4, #0\n"
"	mov	r5, #0\n"
"	mov	r6, #0\n"
"	mov	r7, #0\n"
"	stmia	lr!, {r4-r7}\n"
"	b	.Ljh_rown\n"
".Ljh_rowc:\n"
"	ldmia	r2, {r4-r7}\n"               /* f0 f1 f2 f3 */
"	ldr	r8, .Ljh_k\n"
"	mul	r9, r4, r8\n"                /* f0 * 2896 */
"	ldr	r8, .Ljh_k + 4\n"
"	mul	r10, r6, r8\n"               /* f2 * 2676 */
"	add	r9, r9, #4096\n"
"	add	r11, r9, r10\n"              /* e0 */
"	sub	r9, r9, r10\n"               /* e1 */
"	ldr	r8, .Ljh_k + 8\n"
"	mul	r10, r5, r8\n"               /* f1 * 3711 */
"	ldr	r8, .Ljh_k + 12\n"
"	mla	r10, r7, r8, r10\n"          /* o0 = + f3 * 1303 */
"	ldr	r8, .Ljh_k + 16\n"
"	mul	r4, r5, r8\n"                /* f1 * 1537 */
"	ldr	r8, .Ljh_k + 20\n"
"	mul	r5, r7, r8\n"                /* f3 * 3146 */
"	sub	r4, r4, r5\n"                /* o1 */
"	add	r5, r11, r10\n"
"	mov	r5, r5, asr #13\n"
"	str	r5, [lr]\n"                  /* t0 = e0 + o0 */
"	add	r5, r9, r4\n"
"	mov	r5, r5, asr #13\n"
"	str	r5, [lr, #4]\n"              /* t1 = e1 + o1 */
"	sub	r5, r9, r4\n"
"	mov	r5, r5, asr #13\n"
"	str	r5, [lr, #8]\n"              /* t2 = e1 - o1 */
"	sub	r5, r11, r10\n"
"	mov	r5, r5, asr #13\n"
"	str	r5, [lr, #12]\n"             /* t3 = e0 - o0 */
"	add	lr, lr, #16\n"
".Ljh_rown:\n"
"	add	r2, r2, #32\n"
"	mov	r3, r3, lsr #1\n"
"	subs	ip, ip, #1\n"
"	bne	.Ljh_row\n"
/* the columns: m 0..3 (lr: t + m, r0 dest + m) */
"	mov	lr, sp\n"
"	mov	r3, #4\n"
".Ljh_col:\n"
"	ldr	r4, [lr]\n"
"	ldr	r5, [lr, #16]\n"
"	ldr	r6, [lr, #32]\n"
"	ldr	r7, [lr, #48]\n"
"	ldr	r8, .Ljh_k\n"
"	mul	r9, r4, r8\n"
"	ldr	r8, .Ljh_k + 4\n"
"	mul	r10, r6, r8\n"
"	add	r9, r9, #4096\n"
"	add	r11, r9, r10\n"              /* e0 */
"	sub	r9, r9, r10\n"               /* e1 */
"	ldr	r8, .Ljh_k + 8\n"
"	mul	r10, r5, r8\n"
"	ldr	r8, .Ljh_k + 12\n"
"	mla	r10, r7, r8, r10\n"          /* o0 */
"	ldr	r8, .Ljh_k + 16\n"
"	mul	r4, r5, r8\n"
"	ldr	r8, .Ljh_k + 20\n"
"	mul	r5, r7, r8\n"
"	sub	r4, r4, r5\n"                /* o1 */
"	add	r5, r11, r10\n"
"	mov	r5, r5, asr #13\n"
"	movs	ip, r5, asr #8\n"
"	mvnne	r5, ip, asr #31\n"
"	strb	r5, [r0]\n"
"	add	r5, r9, r4\n"
"	mov	r5, r5, asr #13\n"
"	movs	ip, r5, asr #8\n"
"	mvnne	r5, ip, asr #31\n"
"	strb	r5, [r0, r1]\n"
"	add	r2, r0, r1, lsl #1\n"
"	sub	r5, r9, r4\n"
"	mov	r5, r5, asr #13\n"
"	movs	ip, r5, asr #8\n"
"	mvnne	r5, ip, asr #31\n"
"	strb	r5, [r2]\n"
"	sub	r5, r11, r10\n"
"	mov	r5, r5, asr #13\n"
"	movs	ip, r5, asr #8\n"
"	mvnne	r5, ip, asr #31\n"
"	strb	r5, [r2, r1]\n"
"	add	r0, r0, #1\n"
"	add	lr, lr, #4\n"
"	subs	r3, r3, #1\n"
"	bne	.Ljh_col\n"
/* the rows of the block that were set back to zero */
".Ljh_zero:\n"
"	add	sp, sp, #64\n"
"	ldmfd	sp!, {r2, r3}\n"
"	mov	r4, #0\n"
"	mov	r5, #0\n"
"	mov	r6, #0\n"
"	mov	r7, #0\n"
"	mov	r8, #0\n"
"	mov	r9, #0\n"
"	mov	r10, #0\n"
"	mov	r11, #0\n"
".Ljh_z:\n"
"	tst	r3, #1\n"
"	stmneia	r2, {r4-r11}\n"
"	add	r2, r2, #32\n"
"	movs	r3, r3, lsr #1\n"
"	bne	.Ljh_z\n"
"	ldmfd	sp!, {r4-r11, lr}\n"
"	bx	lr\n"
".Ljh_k:\n"
"	.word	2896, 2676, 3711, 1303, 1537, 3146\n"

/* jpeg_cc_pairs: r0 the parameters (JpegCc: out 0, py 4, pb 8, pr 12, pairs 16, tabs 20).
 * r0 out, r1 py, r2 pb, r3 pr, r4 the count, r5 the chroma tables, r6 r565 (+256; g565 768 words on),
 * r7 b565 (+256); r8 cR, r9 cB, r10 cG, r11 Y, ip, lr temps. */
"	.global jpeg_cc_pairs\n"
"jpeg_cc_pairs:\n"
"	stmfd	sp!, {r4-r11, lr}\n"
"	ldr	r4, [r0, #16]\n"
"	ldr	r5, [r0, #20]\n"
"	ldr	r1, [r0, #4]\n"
"	ldr	r2, [r0, #8]\n"
"	ldr	r3, [r0, #12]\n"
"	ldr	r0, [r0]\n"
"	add	r6, r5, #5120\n"             /* r565 + 256: (1024 + 256) words on */
"	add	r7, r6, #6144\n"             /* b565 + 256: 1536 words on */
"	cmp	r4, #0\n"
"	ble	.Lcc_done\n"
".Lcc_loop:\n"
"	ldrb	r8, [r3], #1\n"              /* Cr */
"	ldrb	r9, [r2], #1\n"              /* Cb */
"	add	r10, r5, r8, lsl #2\n"
"	ldr	r8, [r10]\n"                 /* crR[Cr] */
"	ldr	r10, [r10, #2048]\n"         /* crG[Cr] */
"	add	r11, r5, r9, lsl #2\n"
"	ldr	r9, [r11, #1024]\n"          /* cbB[Cb] */
"	ldr	r11, [r11, #3072]\n"         /* cbG[Cb] */
"	add	r10, r10, r11\n"
"	mov	r10, r10, asr #16\n"         /* the green term */
"	add	r8, r6, r8, lsl #2\n"        /* 1.13: the pair's three tables, each moved by its chroma term */
"	add	r10, r6, r10, lsl #2\n"
"	add	r10, r10, #3072\n"           /* (g565 = r565 + 768 words) */
"	add	r9, r7, r9, lsl #2\n"
"	ldrb	r11, [r1], #1\n"             /* Y0 */
"	ldr	lr, [r8, r11, lsl #2]\n"
"	ldr	ip, [r10, r11, lsl #2]\n"
"	orr	lr, lr, ip\n"
"	ldr	ip, [r9, r11, lsl #2]\n"
"	orr	lr, lr, ip\n"
"	strh	lr, [r0], #2\n"
"	ldrb	r11, [r1], #1\n"             /* Y1 */
"	ldr	lr, [r8, r11, lsl #2]\n"
"	ldr	ip, [r10, r11, lsl #2]\n"
"	orr	lr, lr, ip\n"
"	ldr	ip, [r9, r11, lsl #2]\n"
"	orr	lr, lr, ip\n"
"	strh	lr, [r0], #2\n"
"	subs	r4, r4, #1\n"
"	bne	.Lcc_loop\n"
".Lcc_done:\n"
"	ldmfd	sp!, {r4-r11, lr}\n"
"	bx	lr\n"

/* 1.12: jpeg_idct_put (jpeg_idct_put_c's arithmetic): r0 dest, r1 stride, r2 block, r3 rows. The stack:
 * [sp] dest, [4] stride, [8] block, [12] rows, [16] the columns left, [20] the column's dest, [24] the
 * high half non-zero. A row or column's values 0..7 in r4-r11; b0..b3 in r0, r1, r3, ip; a0..a3 in r5,
 * r7, r9, r11; lr the constant. */
"	.global jpeg_idct_put\n"
"jpeg_idct_put:\n"
"	stmfd	sp!, {r4-r11, lr}\n"
"	sub	sp, sp, #28\n"
"	stmia	sp, {r0, r1, r2, r3}\n"
/* the rows that may be non-zero: r2 the row, r3 the bits left (kept at [20] meanwhile) */
".Lip_rl:\n"
"	tst	r3, #1\n"
"	beq	.Lip_rn\n"
"	str	r3, [sp, #20]\n"
"	ldmia	r2, {r4-r11}\n"
"	orr	ip, r5, r6\n"
"	orr	ip, ip, r7\n"
"	orr	ip, ip, r8\n"
"	orr	ip, ip, r9\n"
"	orr	ip, ip, r10\n"
"	orrs	ip, ip, r11\n"
"	bne	.Lip_rf\n"
"	mov	r4, r4, lsl #19\n"
"	mov	r4, r4, asr #16\n"           /* (short) (row0 << 3) everywhere */
"	mov	r5, r4\n"
"	mov	r6, r4\n"
"	mov	r7, r4\n"
"	mov	r8, r4\n"
"	mov	r9, r4\n"
"	mov	r10, r4\n"
"	mov	r11, r4\n"
"	stmia	r2, {r4-r11}\n"
"	b	.Lip_rd\n"
".Lip_rf:\n"
"	ldr	lr, .Lip_w1\n"
"	mul	r0, r5, lr\n"
"	ldr	lr, .Lip_w3\n"
"	mla	r0, r7, lr, r0\n"
"	mul	r1, r5, lr\n"
"	ldr	lr, .Lip_w7\n"
"	mul	ip, r5, lr\n"
"	mul	r3, r7, lr\n"
"	sub	r1, r1, r3\n"
"	ldr	lr, .Lip_w5\n"
"	mul	r3, r5, lr\n"
"	mul	r5, r7, lr\n"
"	sub	ip, ip, r5\n"
"	ldr	lr, .Lip_w1\n"
"	mul	r5, r7, lr\n"
"	sub	r3, r3, r5\n"
"	orr	r5, r8, r9\n"
"	orr	r5, r5, r10\n"
"	orrs	r5, r5, r11\n"
"	str	r5, [sp, #24]\n"
"	beq	.Lip_ra\n"
"	ldr	lr, .Lip_w5\n"
"	mul	r5, r9, lr\n"
"	add	r0, r0, r5\n"
"	mul	r5, r11, lr\n"
"	sub	r1, r1, r5\n"
"	ldr	lr, .Lip_w7\n"
"	mul	r5, r11, lr\n"
"	add	r0, r0, r5\n"
"	mul	r5, r9, lr\n"
"	add	r3, r3, r5\n"
"	ldr	lr, .Lip_w1\n"
"	mul	r5, r9, lr\n"
"	sub	r1, r1, r5\n"
"	mul	r5, r11, lr\n"
"	sub	ip, ip, r5\n"
"	ldr	lr, .Lip_w3\n"
"	mul	r5, r11, lr\n"
"	add	r3, r3, r5\n"
"	mul	r5, r9, lr\n"
"	add	ip, ip, r5\n"
".Lip_ra:\n"
"	ldr	lr, .Lip_w4\n"
"	mul	r5, r4, lr\n"
"	add	r5, r5, #1024\n"             /* a = W4 row0 + 2^10 */
"	mov	r7, r5\n"
"	mov	r9, r5\n"
"	mov	r11, r5\n"
"	ldr	lr, .Lip_w2\n"
"	mul	r4, r6, lr\n"
"	add	r5, r5, r4\n"
"	sub	r11, r11, r4\n"
"	ldr	lr, .Lip_w6\n"
"	mul	r4, r6, lr\n"
"	add	r7, r7, r4\n"
"	sub	r9, r9, r4\n"
"	ldr	r4, [sp, #24]\n"
"	cmp	r4, #0\n"
"	beq	.Lip_ro\n"
"	ldr	lr, .Lip_w4\n"
"	mul	r4, r8, lr\n"
"	add	r5, r5, r4\n"
"	sub	r7, r7, r4\n"
"	sub	r9, r9, r4\n"
"	add	r11, r11, r4\n"
"	ldr	lr, .Lip_w6\n"
"	mul	r4, r10, lr\n"
"	add	r5, r5, r4\n"
"	sub	r11, r11, r4\n"
"	ldr	lr, .Lip_w2\n"
"	mul	r4, r10, lr\n"
"	sub	r7, r7, r4\n"
"	add	r9, r9, r4\n"
".Lip_ro:\n"
"	add	r4, r5, r0\n"
"	mov	r4, r4, lsl #5\n"
"	mov	r4, r4, asr #16\n"
"	str	r4, [r2, #0]\n"
"	sub	r4, r5, r0\n"
"	mov	r4, r4, lsl #5\n"
"	mov	r4, r4, asr #16\n"
"	str	r4, [r2, #28]\n"
"	add	r4, r7, r1\n"
"	mov	r4, r4, lsl #5\n"
"	mov	r4, r4, asr #16\n"
"	str	r4, [r2, #4]\n"
"	sub	r4, r7, r1\n"
"	mov	r4, r4, lsl #5\n"
"	mov	r4, r4, asr #16\n"
"	str	r4, [r2, #24]\n"
"	add	r4, r9, r3\n"
"	mov	r4, r4, lsl #5\n"
"	mov	r4, r4, asr #16\n"
"	str	r4, [r2, #8]\n"
"	sub	r4, r9, r3\n"
"	mov	r4, r4, lsl #5\n"
"	mov	r4, r4, asr #16\n"
"	str	r4, [r2, #20]\n"
"	add	r4, r11, ip\n"
"	mov	r4, r4, lsl #5\n"
"	mov	r4, r4, asr #16\n"
"	str	r4, [r2, #12]\n"
"	sub	r4, r11, ip\n"
"	mov	r4, r4, lsl #5\n"
"	mov	r4, r4, asr #16\n"
"	str	r4, [r2, #16]\n"
".Lip_rd:\n"
"	ldr	r3, [sp, #20]\n"
".Lip_rn:\n"
"	add	r2, r2, #32\n"
"	movs	r3, r3, lsr #1\n"
"	bne	.Lip_rl\n"
/* the columns */
"	ldr	r3, [sp, #12]\n"
"	cmp	r3, #1\n"
"	beq	.Lip_dc\n"
"	and	r3, r3, #0xf0\n"
"	str	r3, [sp, #24]\n"
"	ldr	r2, [sp, #8]\n"
"	ldr	r0, [sp]\n"
"	str	r0, [sp, #20]\n"
"	mov	r3, #8\n"
"	str	r3, [sp, #16]\n"
".Lip_col:\n"
"	ldr	r4, [r2]\n"
"	ldr	r5, [r2, #32]\n"
"	ldr	r6, [r2, #64]\n"
"	ldr	r7, [r2, #96]\n"
"	ldr	r3, [sp, #24]\n"
"	cmp	r3, #0\n"
"	ldrne	r8, [r2, #128]\n"
"	ldrne	r9, [r2, #160]\n"
"	ldrne	r10, [r2, #192]\n"
"	ldrne	r11, [r2, #224]\n"
"	ldr	lr, .Lip_w1\n"
"	mul	r0, r5, lr\n"
"	ldr	lr, .Lip_w3\n"
"	mla	r0, r7, lr, r0\n"
"	mul	r1, r5, lr\n"
"	ldr	lr, .Lip_w7\n"
"	mul	ip, r5, lr\n"
"	mul	r3, r7, lr\n"
"	sub	r1, r1, r3\n"
"	ldr	lr, .Lip_w5\n"
"	mul	r3, r5, lr\n"
"	mul	r5, r7, lr\n"
"	sub	ip, ip, r5\n"
"	ldr	lr, .Lip_w1\n"
"	mul	r5, r7, lr\n"
"	sub	r3, r3, r5\n"
"	ldr	r5, [sp, #24]\n"
"	cmp	r5, #0\n"
"	beq	.Lip_ca\n"
"	ldr	lr, .Lip_w5\n"
"	mul	r5, r9, lr\n"
"	add	r0, r0, r5\n"
"	mul	r5, r11, lr\n"
"	sub	r1, r1, r5\n"
"	ldr	lr, .Lip_w7\n"
"	mul	r5, r11, lr\n"
"	add	r0, r0, r5\n"
"	mul	r5, r9, lr\n"
"	add	r3, r3, r5\n"
"	ldr	lr, .Lip_w1\n"
"	mul	r5, r9, lr\n"
"	sub	r1, r1, r5\n"
"	mul	r5, r11, lr\n"
"	sub	ip, ip, r5\n"
"	ldr	lr, .Lip_w3\n"
"	mul	r5, r11, lr\n"
"	add	r3, r3, r5\n"
"	mul	r5, r9, lr\n"
"	add	ip, ip, r5\n"
".Lip_ca:\n"
"	add	r4, r4, #32\n"
"	ldr	lr, .Lip_w4\n"
"	mul	r5, r4, lr\n"                /* a = W4 (col0 + 32) */
"	mov	r7, r5\n"
"	mov	r9, r5\n"
"	mov	r11, r5\n"
"	ldr	lr, .Lip_w2\n"
"	mul	r4, r6, lr\n"
"	add	r5, r5, r4\n"
"	sub	r11, r11, r4\n"
"	ldr	lr, .Lip_w6\n"
"	mul	r4, r6, lr\n"
"	add	r7, r7, r4\n"
"	sub	r9, r9, r4\n"
"	ldr	r4, [sp, #24]\n"
"	cmp	r4, #0\n"
"	beq	.Lip_co\n"
"	ldr	lr, .Lip_w4\n"
"	mul	r4, r8, lr\n"
"	add	r5, r5, r4\n"
"	sub	r7, r7, r4\n"
"	sub	r9, r9, r4\n"
"	add	r11, r11, r4\n"
"	ldr	lr, .Lip_w6\n"
"	mul	r4, r10, lr\n"
"	add	r5, r5, r4\n"
"	sub	r11, r11, r4\n"
"	ldr	lr, .Lip_w2\n"
"	mul	r4, r10, lr\n"
"	sub	r7, r7, r4\n"
"	add	r9, r9, r4\n"
".Lip_co:\n"
"	ldr	r6, [sp, #20]\n"
"	ldr	r8, [sp, #4]\n"
"	add	r4, r5, r0\n"
"	mov	r4, r4, asr #20\n"
"	movs	r10, r4, asr #8\n"
"	mvnne	r4, r10, asr #31\n"
"	strb	r4, [r6], r8\n"
"	add	r4, r7, r1\n"
"	mov	r4, r4, asr #20\n"
"	movs	r10, r4, asr #8\n"
"	mvnne	r4, r10, asr #31\n"
"	strb	r4, [r6], r8\n"
"	add	r4, r9, r3\n"
"	mov	r4, r4, asr #20\n"
"	movs	r10, r4, asr #8\n"
"	mvnne	r4, r10, asr #31\n"
"	strb	r4, [r6], r8\n"
"	add	r4, r11, ip\n"
"	mov	r4, r4, asr #20\n"
"	movs	r10, r4, asr #8\n"
"	mvnne	r4, r10, asr #31\n"
"	strb	r4, [r6], r8\n"
"	sub	r4, r11, ip\n"
"	mov	r4, r4, asr #20\n"
"	movs	r10, r4, asr #8\n"
"	mvnne	r4, r10, asr #31\n"
"	strb	r4, [r6], r8\n"
"	sub	r4, r9, r3\n"
"	mov	r4, r4, asr #20\n"
"	movs	r10, r4, asr #8\n"
"	mvnne	r4, r10, asr #31\n"
"	strb	r4, [r6], r8\n"
"	sub	r4, r7, r1\n"
"	mov	r4, r4, asr #20\n"
"	movs	r10, r4, asr #8\n"
"	mvnne	r4, r10, asr #31\n"
"	strb	r4, [r6], r8\n"
"	sub	r4, r5, r0\n"
"	mov	r4, r4, asr #20\n"
"	movs	r10, r4, asr #8\n"
"	mvnne	r4, r10, asr #31\n"
"	strb	r4, [r6], r8\n"
"	ldr	r6, [sp, #20]\n"
"	add	r6, r6, #1\n"
"	str	r6, [sp, #20]\n"
"	add	r2, r2, #4\n"
"	ldr	r3, [sp, #16]\n"
"	subs	r3, r3, #1\n"
"	str	r3, [sp, #16]\n"
"	bne	.Lip_col\n"
"	b	.Lip_zero\n"
/* only row 0: every row of the output the same 8 values */
".Lip_dc:\n"
"	ldr	r2, [sp, #8]\n"
"	ldmia	r2, {r4-r11}\n"
"	ldr	lr, .Lip_w4\n"
"	add	r4, r4, #32\n"
"	mul	r3, r4, lr\n"
"	mov	r3, r3, asr #20\n"
"	movs	ip, r3, asr #8\n"
"	mvnne	r3, ip, asr #31\n"
"	and	r3, r3, #255\n"
"	mov	r0, r3\n"
"	add	r5, r5, #32\n"
"	mul	r3, r5, lr\n"
"	mov	r3, r3, asr #20\n"
"	movs	ip, r3, asr #8\n"
"	mvnne	r3, ip, asr #31\n"
"	and	r3, r3, #255\n"
"	orr	r0, r0, r3, lsl #8\n"
"	add	r6, r6, #32\n"
"	mul	r3, r6, lr\n"
"	mov	r3, r3, asr #20\n"
"	movs	ip, r3, asr #8\n"
"	mvnne	r3, ip, asr #31\n"
"	and	r3, r3, #255\n"
"	orr	r0, r0, r3, lsl #16\n"
"	add	r7, r7, #32\n"
"	mul	r3, r7, lr\n"
"	mov	r3, r3, asr #20\n"
"	movs	ip, r3, asr #8\n"
"	mvnne	r3, ip, asr #31\n"
"	and	r3, r3, #255\n"
"	orr	r0, r0, r3, lsl #24\n"
"	add	r8, r8, #32\n"
"	mul	r3, r8, lr\n"
"	mov	r3, r3, asr #20\n"
"	movs	ip, r3, asr #8\n"
"	mvnne	r3, ip, asr #31\n"
"	and	r3, r3, #255\n"
"	mov	r1, r3\n"
"	add	r9, r9, #32\n"
"	mul	r3, r9, lr\n"
"	mov	r3, r3, asr #20\n"
"	movs	ip, r3, asr #8\n"
"	mvnne	r3, ip, asr #31\n"
"	and	r3, r3, #255\n"
"	orr	r1, r1, r3, lsl #8\n"
"	add	r10, r10, #32\n"
"	mul	r3, r10, lr\n"
"	mov	r3, r3, asr #20\n"
"	movs	ip, r3, asr #8\n"
"	mvnne	r3, ip, asr #31\n"
"	and	r3, r3, #255\n"
"	orr	r1, r1, r3, lsl #16\n"
"	add	r11, r11, #32\n"
"	mul	r3, r11, lr\n"
"	mov	r3, r3, asr #20\n"
"	movs	ip, r3, asr #8\n"
"	mvnne	r3, ip, asr #31\n"
"	and	r3, r3, #255\n"
"	orr	r1, r1, r3, lsl #24\n"
"	ldr	r2, [sp]\n"
"	ldr	r3, [sp, #4]\n"
"	mov	ip, #8\n"
".Lip_dcr:\n"
"	str	r0, [r2]\n"
"	str	r1, [r2, #4]\n"
"	add	r2, r2, r3\n"
"	subs	ip, ip, #1\n"
"	bne	.Lip_dcr\n"
/* the rows of the block that were set back to zero */
".Lip_zero:\n"
"	ldr	r2, [sp, #8]\n"
"	ldr	r3, [sp, #12]\n"
"	mov	r4, #0\n"
"	mov	r5, #0\n"
"	mov	r6, #0\n"
"	mov	r7, #0\n"
"	mov	r8, #0\n"
"	mov	r9, #0\n"
"	mov	r10, #0\n"
"	mov	r11, #0\n"
".Lip_z:\n"
"	tst	r3, #1\n"
"	stmneia	r2, {r4-r11}\n"
"	add	r2, r2, #32\n"
"	movs	r3, r3, lsr #1\n"
"	bne	.Lip_z\n"
"	add	sp, sp, #28\n"
"	ldmfd	sp!, {r4-r11, lr}\n"
"	bx	lr\n"
".Lip_w1:	.word	22725\n"
".Lip_w2:	.word	21407\n"
".Lip_w3:	.word	19266\n"
".Lip_w4:	.word	16383\n"
".Lip_w5:	.word	12873\n"
".Lip_w6:	.word	8867\n"
".Lip_w7:	.word	4520\n"

/* 1.13: jpeg_block. r1 bits, r2 nbits, r3 p, r4 end; the AC part: r5 acfastac, r6 acfast, r7 q, r8 b,
 * r9 ztab, r10 k, r11 the rows (<< 8, and zigzag positions below: ignored); r0, ip temporaries; the
 * parameters at [sp]. JB_FILL: bytes into the bit buffer while nbits <= 24 (a stuffed 0xFF 0x00 is one
 * 0xFF; a marker or the data's end: not done here). */
".macro	JB_FILL\n"
"1:	cmp	r2, #24\n"
"	bgt	3f\n"
"	cmp	r3, r4\n"
"	bhs	.Ljb_fail\n"
"	ldrb	r0, [r3], #1\n"
"	cmp	r0, #0xff\n"
"	beq	2f\n"
"	orr	r1, r0, r1, lsl #8\n"
"	add	r2, r2, #8\n"
"	b	1b\n"
"2:	cmp	r3, r4\n"
"	bhs	.Ljb_fail\n"
"	ldrb	r0, [r3], #1\n"
"	cmp	r0, #0\n"
"	bne	.Ljb_fail\n"
"	mov	r1, r1, lsl #8\n"
"	orr	r1, r1, #0xff\n"
"	add	r2, r2, #8\n"
"	b	1b\n"
"3:\n"
".endm\n"
"	.global	jpeg_block\n"
"jpeg_block:\n"
"	stmfd	sp!, {r0, r4-r11, lr}\n"
"	ldmia	r0, {r1, r2, r3, r4}\n"
"	cmp	r2, #16\n"
"	bge	.Ljb_dc\n"
"	JB_FILL\n"
".Ljb_dc:\n"
"	ldr	r0, [sp]\n"
"	ldr	r6, [r0, #16]\n"                /* dcfast */
"	sub	ip, r2, #9\n"
"	mov	ip, r1, lsr ip\n"
"	mov	ip, ip, lsl #23\n"
"	mov	ip, ip, lsr #21\n"               /* the next 9 bits * 4 */
"	ldr	lr, [r6, ip]\n"
"	cmp	lr, #0\n"
"	beq	.Ljb_fail\n"                     /* a longer code */
"	and	r7, lr, #255\n"                  /* t: the difference's bits */
"	sub	r2, r2, lr, lsr #8\n"
"	cmp	r7, #11\n"
"	bgt	.Ljb_fail\n"
"	movs	ip, r7\n"                        /* the difference: 0 if t = 0 */
"	beq	.Ljb_dc0\n"
"	cmp	r2, r7\n"
"	bge	.Ljb_dcr\n"
"	JB_FILL\n"
".Ljb_dcr:\n"
"	sub	ip, r2, r7\n"
"	mov	ip, r1, lsr ip\n"
"	rsb	r0, r7, #32\n"
"	mov	ip, ip, lsl r0\n"
"	mov	ip, ip, lsr r0\n"               /* t bits */
"	sub	r2, r2, r7\n"
"	mov	r0, #1\n"
"	mov	r0, r0, lsl r7\n"
"	cmp	ip, r0, lsr #1\n"
"	sublt	ip, ip, r0\n"
"	addlt	ip, ip, #1\n"                   /* below 2^(t-1): negative */
".Ljb_dc0:\n"
"	ldr	r0, [sp]\n"
"	ldr	r7, [r0, #40]\n"
"	add	r7, r7, ip\n"
"	str	r7, [r0, #40]\n"                /* pred (the caller keeps it only if done) */
"	mov	ip, r7\n"
"	add	r0, r0, #20\n"
"	ldmia	r0, {r5, r6, r7, r8, r9}\n"     /* acfastac, acfast, q, b, ztab */
"	ldr	r0, [r7]\n"
"	mul	r10, ip, r0\n"
"	add	r10, r10, #1024\n"
"	str	r10, [r8]\n"                    /* b[0] = pred * q[0] + 1024 */
"	mov	r10, #1\n"
"	mov	r11, #0x100\n"
".Ljb_ac:\n"
"	cmp	r2, #16\n"
"	bge	.Ljb_look\n"
"	JB_FILL\n"
".Ljb_look:\n"
"	sub	ip, r2, #9\n"
"	mov	ip, r1, lsr ip\n"
"	mov	ip, ip, lsl #23\n"
"	mov	ip, ip, lsr #21\n"
"	ldr	lr, [r5, ip]\n"                 /* value << 12 | run << 8 | bits, 0: not so */
"	cmp	lr, #0\n"
"	beq	.Ljb_slow\n"
"	and	r0, lr, #255\n"
"	sub	r2, r2, r0\n"
"	mov	r0, lr, lsr #8\n"
"	and	r0, r0, #15\n"
"	add	r10, r10, r0\n"
"	cmp	r10, #63\n"
"	bgt	.Ljb_fail\n"
"	ldr	r0, [r7, r10, lsl #2]\n"
"	mov	lr, lr, asr #12\n"
"	mul	ip, lr, r0\n"
"	ldr	r0, [r9, r10, lsl #2]\n"
"	orr	r11, r11, r0\n"
"	and	r0, r0, #63\n"
"	str	ip, [r8, r0, lsl #2]\n"
"	add	r10, r10, #1\n"
"	cmp	r10, #64\n"
"	blt	.Ljb_ac\n"
"	b	.Ljb_done\n"
".Ljb_slow:\n"                            /* the symbol from the 9-bit table, its value read after */
"	ldr	lr, [r6, ip]\n"
"	cmp	lr, #0\n"
"	beq	.Ljb_fail\n"
"	and	r0, lr, #255\n"                  /* rs */
"	sub	r2, r2, lr, lsr #8\n"
"	ands	lr, r0, #15\n"                  /* the value's bits */
"	bne	.Ljb_val\n"
"	cmp	r0, #0xf0\n"
"	bne	.Ljb_done\n"                     /* end of block */
"	add	r10, r10, #16\n"                 /* 16 zeros */
"	cmp	r10, #64\n"
"	blt	.Ljb_ac\n"
"	b	.Ljb_done\n"
".Ljb_val:\n"
"	add	r10, r10, r0, lsr #4\n"
"	cmp	r10, #63\n"
"	bgt	.Ljb_fail\n"
"	cmp	r2, lr\n"
"	bge	.Ljb_vr\n"
"	JB_FILL\n"
".Ljb_vr:\n"
"	sub	ip, r2, lr\n"
"	mov	ip, r1, lsr ip\n"
"	rsb	r0, lr, #32\n"
"	mov	ip, ip, lsl r0\n"
"	mov	ip, ip, lsr r0\n"
"	sub	r2, r2, lr\n"
"	mov	r0, #1\n"
"	mov	r0, r0, lsl lr\n"
"	cmp	ip, r0, lsr #1\n"
"	sublt	ip, ip, r0\n"
"	addlt	ip, ip, #1\n"
"	ldr	r0, [r7, r10, lsl #2]\n"
"	mul	lr, ip, r0\n"
"	ldr	r0, [r9, r10, lsl #2]\n"
"	orr	r11, r11, r0\n"
"	and	r0, r0, #63\n"
"	str	lr, [r8, r0, lsl #2]\n"
"	add	r10, r10, #1\n"
"	cmp	r10, #64\n"
"	blt	.Ljb_ac\n"
".Ljb_done:\n"
"	ldr	r0, [sp]\n"
"	stmia	r0, {r1, r2, r3}\n"
"	mov	r11, r11, lsr #8\n"
"	and	r11, r11, #255\n"
"	str	r11, [r0, #44]\n"
"	mov	r0, #1\n"
"	ldmfd	sp!, {r1, r4-r11, lr}\n"
"	bx	lr\n"
".Ljb_fail:\n"
"	mov	r0, #0\n"
"	ldmfd	sp!, {r1, r4-r11, lr}\n"
"	bx	lr\n"
);

#else

void jpeg_idct_half(unsigned char* aDest, int aStride, int* aBlock, int aRows) { jpeg_idct_half_c(aDest, aStride, aBlock, aRows); }
void jpeg_cc_pairs(const JpegCc* aP) { jpeg_cc_pairs_c(aP); }
void jpeg_idct_put(unsigned char* aDest, int aStride, int* aBlock, int aRows) { jpeg_idct_put_c(aDest, aStride, aBlock, aRows); }
int jpeg_block(JpegBlk* aP) { (void) aP; return 0; }      /* the PC: jpegdec.cpp's Block does it all */

#endif
