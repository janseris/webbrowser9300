/* Browser 9300: the JavaScript engine's own allocator. On the phone User::Alloc is first fit over
 * the whole app heap's free list, so every one of the engine's many small allocations walked a
 * list as long as the heap was fragmented. Here: blocks up to BWP_MAX bytes in size classes of 8,
 * each class a free list, carved from chunks of BWP_CHUNK; bigger blocks from the system. All of
 * it goes back to the system with bwpool_free (the engine's state is one page's). */
#include "bwpool.h"

#define BWP_STEP 8
#define BWP_MAX 512
#define BWP_CLASSES (BWP_MAX / BWP_STEP)
#define BWP_CHUNK (32768 - 64)    /* (under a power of two, for the system allocator's sake) */
#define BWP_LARGE 0x80000000u

typedef struct BwSmall { unsigned int pad, size; } BwSmall;                  /* before a small block */
typedef struct BwLarge { struct BwLarge *prev, *next; unsigned int pad, size; } BwLarge; /* before a big one */
typedef struct BwChunk { struct BwChunk* next; unsigned int pad[3]; } BwChunk;  /* (16 bytes on the phone) */

struct BwPool
    {
    void* (*sa)(unsigned int);
    void* (*sr)(void*, unsigned int);
    void (*sf)(void*);
    char* cur;
    char* end;
    BwChunk* chunks;
    BwLarge* large;
    unsigned int taken;      /* the bytes taken from the system */
    void* freel[BWP_CLASSES];
    };

static void cpy(char* d, const char* s, unsigned int n) { while (n--) *d++ = *s++; }

BwPool* bwpool_new(void* (*sysalloc)(unsigned int), void* (*sysrealloc)(void*, unsigned int), void (*sysfree)(void*))
{
    BwPool* p = (BwPool*)sysalloc(sizeof(BwPool));
    int i;
    if (!p) return 0;
    p->sa = sysalloc; p->sr = sysrealloc; p->sf = sysfree;
    p->cur = p->end = 0;
    p->chunks = 0;
    p->large = 0;
    p->taken = 0;
    for (i = 0; i < BWP_CLASSES; i++) p->freel[i] = 0;
    return p;
}

unsigned int bwpool_size(void* q)
{
    return ((unsigned int*)q)[-1] & ~BWP_LARGE;
}

static void* small_alloc(BwPool* p, unsigned int n)
{
    unsigned int c = (n + BWP_STEP - 1) / BWP_STEP - 1, bytes = (c + 1) * BWP_STEP;
    BwSmall* h;
    unsigned int d;
    void* q;
    for (d = c; d < BWP_CLASSES && d <= c + 4; d++) {  /* (a free one a little bigger will do) */
        q = p->freel[d];
        if (q) {
            p->freel[d] = *(void**)q;
            return q;
        }
    }
    if ((unsigned int)(p->end - p->cur) < sizeof(BwSmall) + bytes) {
        BwChunk* k = (BwChunk*)p->sa(BWP_CHUNK);
        if (!k) return 0;
        p->taken += BWP_CHUNK;
        k->next = p->chunks;
        p->chunks = k;
        p->cur = (char*)(k + 1);
        p->end = (char*)k + BWP_CHUNK;
    }
    h = (BwSmall*)p->cur;
    h->size = bytes;
    p->cur += sizeof(BwSmall) + bytes;
    return h + 1;
}

static void* large_alloc(BwPool* p, unsigned int n)
{
    BwLarge* h = (BwLarge*)p->sa(sizeof(BwLarge) + n);
    if (!h) return 0;
    p->taken += n;
    h->size = n | BWP_LARGE;
    h->prev = 0;
    h->next = p->large;
    if (p->large) p->large->prev = h;
    p->large = h;
    return h + 1;
}

static void large_unlink(BwPool* p, BwLarge* h)
{
    if (h->prev) h->prev->next = h->next; else p->large = h->next;
    if (h->next) h->next->prev = h->prev;
}

static void block_free(BwPool* p, void* q)
{
    unsigned int s = ((unsigned int*)q)[-1];
    if (s & BWP_LARGE) {
        BwLarge* h = (BwLarge*)q - 1;
        large_unlink(p, h);
        p->taken -= s & ~BWP_LARGE;
        p->sf(h);
    } else {
        unsigned int c = s / BWP_STEP - 1;
        *(void**)q = p->freel[c];
        p->freel[c] = q;
    }
}

void* bwpool_realloc(BwPool* p, void* q, unsigned int n)
{
    unsigned int old;
    void* r;
    if (n == 0) {
        if (q) block_free(p, q);
        return 0;
    }
    if (!q) return n <= BWP_MAX ? small_alloc(p, n) : large_alloc(p, n);
    old = ((unsigned int*)q)[-1];
    if (old & BWP_LARGE) {
        if (n > BWP_MAX && p->sr) {     /* stays big: the system resizes it */
            BwLarge* h = (BwLarge*)q - 1;
            BwLarge* g;
            large_unlink(p, h);
            g = (BwLarge*)p->sr(h, sizeof(BwLarge) + n);
            if (!g) {                    /* (the old block is still there) */
                h->prev = 0; h->next = p->large;
                if (p->large) p->large->prev = h;
                p->large = h;
                return 0;
            }
            p->taken += n - (old & ~BWP_LARGE);
            g->size = n | BWP_LARGE;
            g->prev = 0; g->next = p->large;
            if (p->large) p->large->prev = g;
            p->large = g;
            return g + 1;
        }
        old &= ~BWP_LARGE;
    } else if (n <= old) {
        return q;                        /* (a small block keeps its class) */
    }
    r = n <= BWP_MAX ? small_alloc(p, n) : large_alloc(p, n);
    if (!r) return 0;
    cpy((char*)r, (const char*)q, old < n ? old : n);
    block_free(p, q);
    return r;
}

unsigned int bwpool_taken(BwPool* p)
{
    return p->taken;
}

void bwpool_free(BwPool* p)
{
    if (!p) return;
    while (p->large) {
        BwLarge* h = p->large;
        p->large = h->next;
        p->sf(h);
    }
    while (p->chunks) {
        BwChunk* k = p->chunks;
        p->chunks = k->next;
        p->sf(k);
    }
    p->sf(p);
}
