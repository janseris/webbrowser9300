/* Browser 9300 - layout: the doc's ops into positioned boxes for one width.
 *
 * One pass, like a simple flow layout: blocks stack vertically with collapsing margins and
 * an indent; inline content (words, images, controls) fills lines that are aligned on a common
 * baseline and then shifted for text-align. Table cells are inline with a gap (rows are blocks),
 * which reads well on a 640 px screen. C89, no static data. */

#include "bwcore.h"

#define MAX_BLOCKS 96
#define CELL_GAP 12

typedef struct {
    unsigned int first;      /* first box of the line */
    int top, bottom;
} BwLine;

/* the layout keeps its line table after the public part */
typedef struct {
    BwLayout pub;
    BwLine* lines;
    unsigned int lineCount, lineCap;
} LayoutImpl;

typedef struct {
    int left, right, align, fill, bg;
} Block;

typedef struct {
    const BwDoc* d;
    const BwMetrics* m;
    LayoutImpl* L;
    int y, x;
    int left, right, align;
    unsigned int lineStart;
    int lineContent;
    int pendingSpace;            /* spaces waiting before the next word (pre: a count) */
    int pendingMargin;
    int bg;                      /* the effective background here */
    Block blocks[MAX_BLOCKS];
    int depth;
    int skipDepth;               /* blocks deeper than MAX_BLOCKS */
    int spaceW[BW_FONT_COUNT];
    int fontH[BW_FONT_COUNT];
    int fontA[BW_FONT_COUNT];
    /* merging consecutive words into one box */
    int lastOp;
    unsigned int lastWordEnd;
    unsigned char* asc;          /* per box: its ascent within the line (parallel to boxes) */
    unsigned int ascCap;
} S;

static int font_h(S* s, int f)
{
    if (s->fontH[f] < 0) s->fontH[f] = s->m->fontHeight(s->m->ctx, f);
    return s->fontH[f];
}

static int font_a(S* s, int f)
{
    if (s->fontA[f] < 0) s->fontA[f] = s->m->fontAscent(s->m->ctx, f);
    return s->fontA[f];
}

static int space_w(S* s, int f)
{
    if (s->spaceW[f] < 0) {
        unsigned short sp = ' ';
        s->spaceW[f] = s->m->textWidth(s->m->ctx, f, &sp, 1);
        if (s->spaceW[f] < 1) s->spaceW[f] = 3;
    }
    return s->spaceW[f];
}

static int grow(void** arr, unsigned int* cap, unsigned int count, unsigned int size)
{
    void* na;
    unsigned int nc;
    if (count < *cap) return 1;
    nc = *cap ? *cap * 2 : 64;
    na = ig_malloc(nc * size);
    if (!na) return 0;
    if (*arr) { ig_memcpy(na, *arr, count * size); ig_free(*arr); }
    *arr = na;
    *cap = nc;
    return 1;
}

static BwBox* new_box(S* s, int kind, int asc)
{
    BwLayout* l = &s->L->pub;
    BwBox* b;
    if (!grow((void**)&l->boxes, &l->boxCap, l->boxCount, sizeof(BwBox))) { l->failed = 1; return 0; }
    if (l->boxCount >= s->ascCap) {
        unsigned int nc = s->ascCap ? s->ascCap * 2 : 64;
        unsigned char* na = (unsigned char*)ig_malloc(nc);
        if (!na) { l->failed = 1; return 0; }
        if (s->asc) { ig_memcpy(na, s->asc, l->boxCount); ig_free(s->asc); }
        s->asc = na;
        s->ascCap = nc;
    }
    if (!s->lineContent) {
        /* the first thing on a line: the margin waiting above it goes in now */
        if (s->pendingMargin && s->y > 0) s->y += s->pendingMargin;
        s->pendingMargin = 0;
        s->lineContent = 1;
    }
    b = &l->boxes[l->boxCount];
    ig_memset(b, 0, sizeof(BwBox));
    b->kind = (unsigned char)kind;
    b->target = -1;
    b->ref = -1;
    b->y = s->y;
    s->asc[l->boxCount] = (unsigned char)(asc > 255 ? 255 : asc < 0 ? 0 : asc);
    l->boxCount++;
    return b;
}

static int lum(int c)
{
    return (((c >> 16) & 255) * 299 + ((c >> 8) & 255) * 587 + (c & 255) * 114) / 1000;
}

/* text that would be invisible on the background gets black or white */
static int readable(S* s, int color)
{
    int lb = lum(s->bg), lt = lum(color);
    int diff = lb > lt ? lb - lt : lt - lb;
    if (diff >= 60) return color;
    return lb > 128 ? 0x000000 : 0xFFFFFF;
}

static void finish_line(S* s, int force)
{
    LayoutImpl* L = s->L;
    BwLayout* l = &L->pub;
    unsigned int i;
    int maxAsc = 0, maxDesc = 0, h, dx = 0;
    if (l->boxCount == s->lineStart) {
        if (force) {
            int f = BW_FONT_MAKE(1, 0);
            if (s->pendingMargin && s->y > 0) { s->y += s->pendingMargin; s->pendingMargin = 0; }
            s->y += font_h(s, f);
        }
        s->x = s->left;
        s->pendingSpace = 0;
        s->lineContent = 0;
        s->lastOp = -1;
        return;
    }
    for (i = s->lineStart; i < l->boxCount; i++) {
        BwBox* b = &l->boxes[i];
        int a = s->asc[i];
        if (a > maxAsc) maxAsc = a;
        if (b->h - a > maxDesc) maxDesc = b->h - a;
    }
    h = maxAsc + maxDesc;
    if (s->align == BW_ALIGN_CENTER && s->x < s->right) dx = (s->right - s->x) / 2;
    else if (s->align == BW_ALIGN_RIGHT && s->x < s->right) dx = s->right - s->x;
    for (i = s->lineStart; i < l->boxCount; i++) {
        BwBox* b = &l->boxes[i];
        b->y = s->y + maxAsc - s->asc[i];
        b->x += dx;
        if (b->target >= 0 && (unsigned int)b->target < s->d->targetCount) {
            BwTargetPos* t = &l->targets[b->target];
            if (t->y0 < 0) { t->x0 = b->x; t->y0 = b->y; t->x1 = b->x + b->w; t->y1 = b->y + b->h; }
            else {
                if (b->x < t->x0) t->x0 = b->x;
                if (b->y < t->y0) t->y0 = b->y;
                if (b->x + b->w > t->x1) t->x1 = b->x + b->w;
                if (b->y + b->h > t->y1) t->y1 = b->y + b->h;
            }
        }
    }
    if (grow((void**)&L->lines, &L->lineCap, L->lineCount, sizeof(BwLine))) {
        BwLine* ln = &L->lines[L->lineCount++];
        ln->first = s->lineStart;
        ln->top = s->y;
        ln->bottom = s->y + h;
    } else l->failed = 1;
    s->y += h;
    s->x = s->left;
    s->lineStart = l->boxCount;
    s->pendingSpace = 0;
    s->lineContent = 0;
    s->lastOp = -1;
}

/* places one word s->d->pool[ws..we) of op */
static void place_word(S* s, const BwOp* op, int opIndex, unsigned int ws, unsigned int we, int gap)
{
    const unsigned short* pool = s->d->pool;
    BwLayout* l = &s->L->pub;
    int f = op->font;
    int w = s->m->textWidth(s->m->ctx, f, pool + ws, (int)(we - ws));
    int sw = gap > 0 && (s->x > s->left || (op->flags & BW_OPF_PRE)) ? gap * space_w(s, f) : 0;
    int avail = s->right - s->left;
    if (s->x + sw + w > s->right && s->x > s->left) {
        finish_line(s, 0);
        sw = (op->flags & BW_OPF_PRE) && 0 ? sw : 0;
    }
    if (w > avail) {
        /* a word longer than the line: break it where it fills the line */
        while (ws < we) {
            unsigned int k = ws + 1;
            int room = s->right - s->x;
            int pw = s->m->textWidth(s->m->ctx, f, pool + ws, 1);
            BwBox* b;
            while (k < we) {
                int nw = s->m->textWidth(s->m->ctx, f, pool + ws, (int)(k + 1 - ws));
                if (nw > room) break;
                pw = nw;
                k++;
            }
            b = new_box(s, BW_BOX_TEXT, font_a(s, f));
            if (!b) return;
            b->font = (unsigned char)f;
            b->flags = op->flags & (BW_OPF_UNDERLINE | BW_OPF_STRIKE);
            b->color = readable(s, op->color);
            b->target = op->target;
            b->x = s->x;
            b->w = pw;
            b->h = font_h(s, f);
            b->off = ws;
            b->len = k - ws;
            s->x += pw;
            ws = k;
            if (ws < we) finish_line(s, 0);
        }
        s->lastOp = -1;
        return;
    }
    /* the same op, right after the previous word, on the same line: one box */
    if (s->lastOp == opIndex && l->boxCount > s->lineStart && s->lastWordEnd + (unsigned int)gap == ws &&
        l->boxes[l->boxCount - 1].off + l->boxes[l->boxCount - 1].len == s->lastWordEnd) {
        BwBox* b = &l->boxes[l->boxCount - 1];
        b->len = we - b->off;
        b->w += sw + w;
        s->x += sw + w;
        s->lastWordEnd = we;
        return;
    }
    {
        BwBox* b = new_box(s, BW_BOX_TEXT, font_a(s, f));
        if (!b) return;
        b->font = (unsigned char)f;
        b->flags = op->flags & (BW_OPF_UNDERLINE | BW_OPF_STRIKE);
        b->color = readable(s, op->color);
        b->target = op->target;
        b->x = s->x + sw;
        b->w = w;
        b->h = font_h(s, f);
        b->off = ws;
        b->len = we - ws;
        s->x += sw + w;
        s->lastOp = opIndex;
        s->lastWordEnd = we;
    }
}

static void place_text(S* s, const BwOp* op, int opIndex)
{
    const unsigned short* pool = s->d->pool;
    unsigned int i = op->off, end = op->off + op->len;
    int pre = (op->flags & BW_OPF_PRE) != 0;
    while (i < end) {
        unsigned short c = pool[i];
        if (c == ' ') {
            unsigned int k = i;
            while (k < end && pool[k] == ' ') k++;
            if (pre) s->pendingSpace += (int)(k - i);
            else s->pendingSpace = 1;
            i = k;
            continue;
        }
        if (c == '\n') {
            finish_line(s, 1);
            i++;
            continue;
        }
        {
            unsigned int k = i;
            int gap = s->pendingSpace;
            while (k < end && pool[k] != ' ' && pool[k] != '\n') k++;
            if (!pre && gap > 1) gap = 1;
            place_word(s, op, opIndex, i, k, gap);
            s->pendingSpace = 0;
            i = k;
        }
    }
}

/* an inline box (image, control) of w x h sitting on the baseline */
static BwBox* place_inline(S* s, int kind, int w, int h)
{
    int sw = s->pendingSpace && s->x > s->left ? space_w(s, BW_FONT_MAKE(1, 0)) : 0;
    BwBox* b;
    if (s->x + sw + w > s->right && s->x > s->left) { finish_line(s, 0); sw = 0; }
    b = new_box(s, kind, h);
    if (!b) return 0;
    b->x = s->x + sw;
    b->w = w;
    b->h = h;
    s->x += sw + w;
    s->pendingSpace = 0;
    s->lastOp = -1;
    return b;
}

static void place_image(S* s, const BwOp* op)
{
    BwImage* im = &s->d->images[op->ref];
    int w = im->attrW, h = im->attrH;
    int maxW = s->right - s->left;
    BwBox* b;
    if (im->failed || (!im->natW && !w && !h)) {
        /* not loaded (yet / at all): the alt text, or a small frame while it loads */
        if (im->altLen) {
            BwOp t = *op;
            t.type = BW_OP_TEXT;
            t.off = im->altOff;
            t.len = im->altLen;
            t.flags = op->target >= 0 ? BW_OPF_UNDERLINE : 0;
            if (!im->failed) {
                /* framed alt text: one box */
                int f = op->font;
                int tw = s->m->textWidth(s->m->ctx, f, s->d->pool + im->altOff, (int)im->altLen) + 6;
                if (tw > maxW) tw = maxW;
                b = place_inline(s, BW_BOX_IMAGE, tw, font_h(s, f) + 4);
                if (b) { b->ref = op->ref; b->target = op->target; b->font = (unsigned char)f; b->color = readable(s, op->color); }
                return;
            }
            place_text(s, &t, -2);
            return;
        }
        if (im->failed) return;
        w = h = 16;
    } else if (im->natW > 0 && im->natH > 0) {
        if (!w && !h) { w = im->natW; h = im->natH; }
        else if (!w) w = h * im->natW / im->natH;
        else if (!h) h = w * im->natH / im->natW;
    } else {
        if (!w) w = h;
        if (!h) h = w;
    }
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (w > maxW) { h = h * maxW / w; w = maxW; if (h < 1) h = 1; }
    if (h > 2000) h = 2000;
    b = place_inline(s, BW_BOX_IMAGE, w, h);
    if (b) { b->ref = op->ref; b->target = op->target; b->font = op->font; b->color = op->color; }
}

static void place_control(S* s, const BwOp* op)
{
    const BwControl* c = &s->d->controls[op->ref];
    int f = BW_FONT_MAKE(BW_FONT_SIZE(op->font) == 0 ? 0 : 1, 0);
    int fh = font_h(s, f);
    int cw;
    int maxW = s->right - s->left;
    int w, h;
    BwBox* b;
    {
        unsigned short zero = '0';
        cw = s->m->textWidth(s->m->ctx, f, &zero, 1);
        if (cw < 4) cw = 4;
    }
    switch (c->type) {
    case BW_CTL_CHECKBOX: case BW_CTL_RADIO:
        w = h = fh - 2 > 10 ? fh - 2 : 10;
        break;
    case BW_CTL_SUBMIT: case BW_CTL_BUTTON: case BW_CTL_RESET: case BW_CTL_IMAGE: case BW_CTL_FILE:
        {
            unsigned int n;
            const unsigned short* t = bw_control_text(c, &n);
            w = s->m->textWidth(s->m->ctx, f, t, (int)n) + 14;
            h = fh + 6;
        }
        break;
    case BW_CTL_SELECT:
        {
            unsigned int i;
            int mw = 30;
            for (i = 0; i < c->optionCount && i < 60; i++) {
                int ow = s->m->textWidth(s->m->ctx, f, c->options[i].label, (int)c->options[i].labelLen);
                if (ow > mw) mw = ow;
            }
            w = mw + 22;
            h = fh + 6;
        }
        break;
    case BW_CTL_TEXTAREA:
        {
            int rows = c->size < 1 ? 1 : c->size > 5 ? 5 : c->size;
            int cols = c->cols < 10 ? 10 : c->cols;
            w = cols * cw + 8;
            h = rows * fh + 6;
        }
        break;
    default:
        {
            int size = c->size < 4 ? 4 : c->size > 60 ? 60 : c->size;
            w = size * cw + 8;
            h = fh + 6;
        }
        break;
    }
    if (w < 12) w = 12;
    if (w > maxW) w = maxW;
    b = place_inline(s, BW_BOX_CONTROL, w, h);
    if (b) { b->ref = op->ref; b->target = op->target; b->font = (unsigned char)f; b->color = readable(s, s->d->textColor); }
}

static void push_block(S* s, const BwOp* op)
{
    BwLayout* l = &s->L->pub;
    Block* b;
    finish_line(s, 0);
    if (op->a > s->pendingMargin) s->pendingMargin = op->a;
    if (s->depth >= MAX_BLOCKS) { s->skipDepth++; return; }
    b = &s->blocks[s->depth++];
    b->left = s->left;
    b->right = s->right;
    b->align = s->align;
    b->bg = s->bg;
    b->fill = -1;
    if (op->color >= 0) {
        if (grow((void**)&l->fills, &l->fillCap, l->fillCount, sizeof(BwFill))) {
            BwFill* f = &l->fills[l->fillCount];
            if (s->pendingMargin && s->y > 0) s->y += s->pendingMargin;
            s->pendingMargin = 0;
            f->x = s->left;
            f->w = s->right - s->left;
            f->y = s->y;
            f->h = 0;
            f->color = op->color;
            b->fill = (int)l->fillCount++;
            s->bg = op->color;
        }
    }
    s->left += op->b;
    if (s->left > s->right - 40) s->left = s->right - 40;
    if (s->left < 0) s->left = 0;
    s->align = op->align;
    s->x = s->left;
}

static void pop_block(S* s, const BwOp* op)
{
    BwLayout* l = &s->L->pub;
    Block* b;
    finish_line(s, 0);
    if (s->skipDepth) { s->skipDepth--; return; }
    if (!s->depth) return;
    b = &s->blocks[--s->depth];
    if (b->fill >= 0) {
        BwFill* f = &l->fills[b->fill];
        f->h = s->y - f->y + 2;
        s->y += 2;
    }
    s->left = b->left;
    s->right = b->right;
    s->align = b->align;
    s->bg = b->bg;
    s->x = s->left;
    if (op && op->a > s->pendingMargin) s->pendingMargin = op->a;
}

BwLayout* bw_layout(const BwDoc* d, int width, const BwMetrics* m)
{
    LayoutImpl* L;
    S* s;
    unsigned int i;
    L = (LayoutImpl*)ig_malloc(sizeof(LayoutImpl));
    if (!L) return 0;
    ig_memset(L, 0, sizeof(LayoutImpl));
    s = (S*)ig_malloc(sizeof(S));
    if (!s) { ig_free(L); return 0; }
    ig_memset(s, 0, sizeof(S));
    L->pub.width = width;
    if (d->targetCount) {
        L->pub.targets = (BwTargetPos*)ig_malloc(d->targetCount * sizeof(BwTargetPos));
        if (!L->pub.targets) { ig_free(s); ig_free(L); return 0; }
        for (i = 0; i < d->targetCount; i++) { L->pub.targets[i].y0 = -1; L->pub.targets[i].x0 = L->pub.targets[i].x1 = L->pub.targets[i].y1 = 0; }
    }
    if (d->anchorCount) {
        L->pub.anchorY = (int*)ig_malloc(d->anchorCount * sizeof(int));
        if (!L->pub.anchorY) { ig_free(L->pub.targets); ig_free(s); ig_free(L); return 0; }
        for (i = 0; i < d->anchorCount; i++) L->pub.anchorY[i] = 0;
    }
    for (i = 0; i < BW_FONT_COUNT; i++) { s->spaceW[i] = -1; s->fontH[i] = -1; s->fontA[i] = -1; }
    s->d = d;
    s->m = m;
    s->L = L;
    s->left = 4;
    s->right = width - 4;
    if (s->right < s->left + 40) s->right = s->left + 40;
    s->x = s->left;
    s->y = 2;
    s->bg = d->bgColor;
    s->lastOp = -1;
    for (i = 0; i < d->opCount && !L->pub.failed; i++) {
        const BwOp* op = &d->ops[i];
        switch (op->type) {
        case BW_OP_TEXT: place_text(s, op, (int)i); break;
        case BW_OP_BLOCK_START: push_block(s, op); break;
        case BW_OP_BLOCK_END: pop_block(s, op); break;
        case BW_OP_BR: finish_line(s, s->lineContent ? 0 : 1); break;
        case BW_OP_HR:
            {
                BwBox* b;
                finish_line(s, 0);
                if (s->pendingMargin && s->y > 0) s->y += s->pendingMargin;
                s->pendingMargin = 0;
                s->y += 4;
                b = new_box(s, BW_BOX_RULE, 2);
                if (b) {
                    b->x = s->left; b->w = s->right - s->left; b->h = 2;
                    b->color = lum(s->bg) > 128 ? 0x999999 : 0x777777;
                }
                s->x = s->right;
                finish_line(s, 0);
                s->y += 4;
            }
            break;
        case BW_OP_IMAGE: place_image(s, op); break;
        case BW_OP_CONTROL: place_control(s, op); break;
        case BW_OP_ANCHOR:
            if (op->ref >= 0 && (unsigned int)op->ref < d->anchorCount)
                L->pub.anchorY[op->ref] = s->y + (s->lineContent ? 0 : s->pendingMargin);
            break;
        case BW_OP_MARKER:
            {
                int f = op->font;
                int w = m->textWidth(m->ctx, f, d->pool + op->off, (int)op->len);
                int mx = s->left - w - 6;
                BwBox* b = new_box(s, BW_BOX_TEXT, font_a(s, f));
                if (b) {
                    b->font = (unsigned char)f;
                    b->color = readable(s, op->color);
                    b->off = op->off;
                    b->len = op->len;
                    b->w = w;
                    b->h = font_h(s, f);
                    if (mx >= 0 && s->x == s->left) b->x = mx;
                    else { b->x = s->x; s->x += w + space_w(s, f); }
                }
                s->pendingSpace = 0;
                s->lastOp = -1;
            }
            break;
        case BW_OP_CELL:
            if (s->x > s->left) {
                s->x += CELL_GAP;
                if (s->x >= s->right) finish_line(s, 0);
            }
            s->pendingSpace = 0;
            break;
        default: break;
        }
    }
    finish_line(s, 0);
    while (s->depth) pop_block(s, 0);
    L->pub.height = s->y + 6;
    ig_free(s->asc);
    ig_free(s);
    return &L->pub;
}

void bw_layout_free(BwLayout* l)
{
    LayoutImpl* L = (LayoutImpl*)l;
    if (!l) return;
    ig_free(l->boxes);
    ig_free(l->fills);
    ig_free(l->targets);
    ig_free(l->anchorY);
    ig_free(L->lines);
    ig_free(L);
}

unsigned int bw_layout_first_box(const BwLayout* l, int y)
{
    const LayoutImpl* L = (const LayoutImpl*)l;
    unsigned int lo = 0, hi = L->lineCount;
    while (lo < hi) {
        unsigned int mid = (lo + hi) / 2;
        if (L->lines[mid].bottom <= y) lo = mid + 1; else hi = mid;
    }
    if (lo >= L->lineCount) return l->boxCount;
    return L->lines[lo].first;
}

int bw_layout_hit(const BwLayout* l, int x, int y)
{
    unsigned int i = bw_layout_first_box(l, y - 2);
    int best = -1;
    for (; i < l->boxCount; i++) {
        const BwBox* b = &l->boxes[i];
        if (b->y > y + 40) break;
        if (b->target >= 0 && x >= b->x - 2 && x < b->x + b->w + 2 && y >= b->y - 1 && y < b->y + b->h + 1) best = b->target;
    }
    return best;
}
