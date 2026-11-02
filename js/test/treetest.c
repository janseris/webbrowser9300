/* The page tree's round trip: each page parsed into js/bwdom.c's tree and written back out must
 * give the browser's renderer (core/bwhtml.c) the same document: the same ops (type, font, flags,
 * text), links and pictures. ./treetest page.html ... */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bwcore.h"
#include "bwdom.h"
void* ig_malloc(unsigned int n) { return malloc(n ? n : 1); }
void ig_free(void* p) { free(p); }
static char* rd(const char* f, long* n) { FILE* p = fopen(f, "rb"); char* b; if (!p) return 0; fseek(p, 0, SEEK_END); *n = ftell(p); rewind(p); b = malloc(*n + 1); fread(b, 1, *n, p); b[*n] = 0; fclose(p); return b; }
static int same_op(const BwDoc* a, const BwOp* x, const BwDoc* b, const BwOp* y)
{
    if (x->type != y->type || x->font != y->font || x->flags != y->flags) return 0;
    if (x->type == BW_OP_TEXT || x->type == BW_OP_MARKER)
        return x->len == y->len && memcmp(a->pool + x->off, b->pool + y->off, x->len * 2) == 0;
    return 1;
}
static void show(const BwDoc* d, unsigned int i)
{
    unsigned int k, j;
    for (k = i > 2 ? i - 2 : 0; k < i + 3 && k < d->opCount; k++) {
        const BwOp* o = &d->ops[k];
        printf("    %s%u type %d font %d flags %d '", k == i ? ">" : " ", k, o->type, o->font, o->flags);
        if (o->type == BW_OP_TEXT) for (j = 0; j < o->len && j < 50; j++) { unsigned short c = d->pool[o->off + j]; putchar(c < 128 ? c : '?'); }
        printf("'\n");
    }
}
int main(int argc, char** argv)
{
    int f, bad = 0;
    for (f = 1; f < argc; f++) {
        long n; char* html = rd(argv[f], &n); BwTree* t; BwBuf out; BwDoc *a, *b; unsigned int i, same = 0, first = (unsigned)-1;
        if (!html) { printf("%s: missing\n", argv[f]); continue; }
        t = bwd_new(200000);
        bwd_parse_document(t, html, (unsigned)n);
        memset(&out, 0, sizeof out);
        bwd_serialize(t->doc, 0, &out);
        a = bw_doc_parse(html, (unsigned)n, "https://example.cz/", "text/html; charset=utf-8", BW_CS_UTF8);
        b = bw_doc_parse(out.p, out.n, "https://example.cz/", "text/html; charset=utf-8", BW_CS_UTF8);
        for (i = 0; i < a->opCount && i < b->opCount; i++) { if (same_op(a, &a->ops[i], b, &b->ops[i])) same++; else if (first == (unsigned)-1) first = i; }
        printf("%-44s %7ld B -> %7u B, %6u nodes%s | ops %u / %u, same %u (%.1f%%), links %u/%u, images %u/%u\n", argv[f], n, out.n, t->count,
               t->oom ? " OOM" : "", a->opCount, b->opCount, same, a->opCount ? 100.0 * same / a->opCount : 100.0, a->linkCount, b->linkCount, a->imageCount, b->imageCount);
        if (first != (unsigned)-1 || a->opCount != b->opCount || a->linkCount != b->linkCount || a->imageCount != b->imageCount) {
            bad++;
            if (first == (unsigned)-1) first = a->opCount < b->opCount ? a->opCount : b->opCount;
            printf("  first difference at op %u:\n  original:\n", first); show(a, first); printf("  round trip:\n"); show(b, first);
        }
        if (argc > 2 && getenv("DUMP")) { FILE* o = fopen(getenv("DUMP"), "wb"); fwrite(out.p, 1, out.n, o); fclose(o); }
        bw_doc_free(a); bw_doc_free(b); free(out.p); bwd_free(t); free(html);
    }
    printf(bad ? "%d DIFFERENT\n" : "all the same\n", bad);
    return bad != 0;
}
