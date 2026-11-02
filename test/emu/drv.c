/* The browser's core in the ARM emulator (armemu.py): the page and its stylesheets are written at
 * 0x2000000 by the Python side; parse (as the app: bw_doc_parse_hover) and lay out, counted per call. */
#include "bwcore.h"

static char* const kHeapBase = (char*)0x1000000;
typedef struct { char* top; } Heap;
#define HEAP ((Heap*)0x0FF0000)
void* ig_malloc(unsigned int n)
{
    char* p = HEAP->top;
    unsigned int* h;
    n = (n + 7) & ~7u;
    h = (unsigned int*)p;
    h[0] = n;
    HEAP->top = p + n + 8;
    return p + 8;
}
void ig_free(void* p) { (void)p; }
void* memset(void* d, int c, unsigned int n) { return ig_memset(d, c, n), d; }
void* memcpy(void* d, const void* s, unsigned int n) { ig_memcpy(d, s, n); return d; }

static int tw(void* ctx, int font, const unsigned short* s, int n) { (void)ctx; (void)s; return n * (6 + (font & 3)); }
static int fh(void* ctx, int font) { (void)ctx; return 13 + (font & 3) * 3; }
static int fa(void* ctx, int font) { (void)ctx; return 10 + (font & 3) * 2; }

int emu_init(void) { HEAP->top = kHeapBase; return 0; }

/* args: a block at 0x2000000: [0] html ptr, [1] len, [2] url, [3] ct, [4] css count, [5] css ptrs, [6] css lens */
int emu_parse(unsigned int* a)
{
    BwDoc* d = bw_doc_parse_hover((const char*)a[0], a[1], (const char*)a[2], (const char*)a[3], -1,
                                  (const char* const*)a[5], (const unsigned int*)a[6], a[4], 0, 0, 0);
    return (int)d;
}
int emu_layout(BwDoc* d, int width)
{
    BwMetrics m;
    BwLayout* l;
    m.ctx = 0; m.textWidth = tw; m.fontHeight = fh; m.fontAscent = fa;
    l = bw_layout(d, width, &m);
    return l ? (int)l->boxCount : -1;
}
int emu_ops(BwDoc* d) { return (int)d->opCount; }
int emu_rules(BwDoc* d) { return (int)d->cssRules; }
