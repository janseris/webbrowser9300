/* Browser 9300: the JavaScript engine's own allocator (js/bwpool.c). Plain C, no libc headers. */
#ifndef BW_POOL_H
#define BW_POOL_H
#ifdef __cplusplus
extern "C" {
#endif

typedef struct BwPool BwPool;

/* sysalloc / sysrealloc / sysfree: where the chunks and the big blocks come from (NULL: none) */
BwPool* bwpool_new(void* (*sysalloc)(unsigned int n), void* (*sysrealloc)(void* p, unsigned int n), void (*sysfree)(void* p));
/* js_Alloc's contract: p NULL allocates, n 0 frees, else resizes; NULL when there is no memory */
void* bwpool_realloc(BwPool* pool, void* p, unsigned int n);
/* the bytes a block takes (what it counts against a limit) */
unsigned int bwpool_size(void* p);
/* the bytes the pool has taken from the system (chunks and big blocks) */
unsigned int bwpool_taken(BwPool* pool);
/* everything back to the system at once (blocks still allocated too) */
void bwpool_free(BwPool* pool);

#ifdef __cplusplus
}
#endif
#endif
