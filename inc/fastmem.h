/* Fast memory helpers: ARM assembly on Symbian ARM (armi) builds, C everywhere else. C89, no libc,
 * no writable static data (fine in an EKA1 .app / DLL).
 *
 *   fastmem.c      the assembly (compiled to nothing unless FM_ASM)
 *   fastmem_ref.c  the C versions (compiled to nothing when FM_ASM)
 * Add both to the .mmp (and to the PC test build). -DFM_NO_ASM forces the C versions. */

#ifndef FASTMEM_H
#define FASTMEM_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__MARM__) && !defined(__MARM_THUMB__) && !defined(FM_NO_ASM)
#define FM_ASM
#endif

/* forward copy; overlapping is fine only when dst < src (like removing from an array) */
void fm_memcpy(void* dst, const void* src, unsigned int n);
/* n bytes of (c & 255) */
void fm_memset(void* dst, int c, unsigned int n);
/* 0 for NULL */
unsigned int fm_strlen(const char* s);
/* count 16-bit values (dst 2-byte aligned): a 16 bpp screen, a z-buffer */
void fm_fill16(unsigned short* dst, unsigned int value, unsigned int count);
/* count 32-bit values (dst 4-byte aligned) */
void fm_fill32(unsigned int* dst, unsigned int value, unsigned int count);

#ifdef __cplusplus
}
#endif

#endif
