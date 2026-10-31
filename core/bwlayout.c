/* Browser 9300 - layout: the doc's ops into positioned boxes for one width.
 *
 * One pass, like a simple flow layout: blocks stack vertically with collapsing margins and
 * an indent; inline content (words, images, controls) fills lines that are aligned on a common
 * baseline and then shifted for text-align. Tables get columns sized from their content (min /
 * max widths, like browsers) when they fit the width; otherwise their rows stack and the cells
 * follow each other with a gap. A table row is one entry of the line table, so painting and
 * hit testing walk lines, not boxes. C89, no static data. */

#include "bwcore.h"

#define MAX_BLOCKS 96
#define CELL_GAP 12

typedef struct {
    BwLayout pub;
} LayoutImpl;

typedef struct {
    int left, right, align, fill, bg;
    int box;                     /* the doc's BwBlockBox, -1 none */
    int oL, oR, top;             /* its border box: left, right, where its border starts */
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
    int tableDepth;              /* tables laid out as columns around this point */
    int measureDepth;            /* tables being measured inside each other (recursion) */
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
    b->bg = -1;
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

static int readable_on(S* s, int color, int bg)
{
    int saved = s->bg, c;
    if (bg < 0) return readable(s, color);
    s->bg = bg;
    c = readable(s, color);
    s->bg = saved;
    return c;
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
    if (grow((void**)&L->pub.lines, &L->pub.lineCap, L->pub.lineCount, sizeof(BwLine))) {
        BwLine* ln = &L->pub.lines[L->pub.lineCount++];
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
    /* nowrap: a run that fits a line was moved to a fresh line when needed (place_text) and is
     * not broken; a longer one wraps as usual */
    int keep = (op->flags & BW_OPF_NOWRAP) && s->lastOp == opIndex && s->x + sw + w <= s->right + 2;
    if (s->x + sw + w > s->right && s->x > s->left && !keep) {
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
            b->color = readable_on(s, op->color, op->bg);
            b->bg = op->bg;
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
        b->color = readable_on(s, op->color, op->bg);
        b->bg = op->bg;
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
    if ((op->flags & BW_OPF_NOWRAP) && s->x > s->left) {
        /* the whole run on a fresh line if it does not fit here but fits there */
        unsigned int a = i, b = end;
        int w, gap;
        while (a < b && pool[a] == ' ') a++;
        while (b > a && pool[b - 1] == ' ') b--;
        w = b > a ? s->m->textWidth(s->m->ctx, op->font, pool + a, (int)(b - a)) : 0;
        gap = (s->pendingSpace || a > i) ? space_w(s, op->font) : 0;
        if (s->x + gap + w > s->right && w <= s->right - s->left) finish_line(s, 0);
    }
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

/* the width a picture takes before it is fitted (for measuring table columns) */
static int image_width(S* s, const BwOp* op)
{
    const BwImage* im = &s->d->images[op->ref];
    int w = im->attrW, h = im->attrH;
    if (im->failed || (!im->natW && !w && !h)) {
        if (!im->altLen) return im->failed ? 0 : 16;
        return s->m->textWidth(s->m->ctx, op->font, s->d->pool + im->altOff, (int)im->altLen) + 6;
    }
    if (im->natW > 0 && im->natH > 0) {
        if (!w && !h) w = im->natW;
        else if (!w) w = h * im->natW / im->natH;
    } else if (!w) w = h;
    return w;
}

static void control_size(S* s, const BwOp* op, int* pw, int* ph, int* pf)
{
    const BwControl* c = &s->d->controls[op->ref];
    int f = BW_FONT_MAKE(BW_FONT_SIZE(op->font) == 0 ? 0 : 1, 0);
    int fh = font_h(s, f);
    int cw;
    int w, h;
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
    *pw = w;
    *ph = h;
    *pf = f;
}

static void place_control(S* s, const BwOp* op)
{
    int w, h, f;
    int maxW = s->right - s->left;
    BwBox* b;
    control_size(s, op, &w, &h, &f);
    if (w > maxW) w = maxW;
    b = place_inline(s, BW_BOX_CONTROL, w, h);
    if (b) { b->ref = op->ref; b->target = op->target; b->font = (unsigned char)f; b->color = readable(s, s->d->textColor); }
}

static int add_fill(S* s, int x, int y, int w, int h, int color, int outline);

static void push_block(S* s, const BwOp* op)
{
    BwLayout* l = &s->L->pub;
    Block* b;
    const BwBlockBox* bx = op->type == BW_OP_BLOCK_START && op->ref >= 0 && (unsigned int)op->ref < s->d->blockCount ? &s->d->blocks[op->ref] : 0;
    int oL, oR;
    finish_line(s, 0);
    if (op->a > s->pendingMargin) s->pendingMargin = op->a;
    if (s->depth >= MAX_BLOCKS) { s->skipDepth++; return; }
    b = &s->blocks[s->depth++];
    b->left = s->left;
    b->right = s->right;
    b->align = s->align;
    b->bg = s->bg;
    b->fill = -1;
    b->box = bx ? op->ref : -1;
    /* the border box: the margins in, then a width / max-width, centred with margin: auto */
    oL = s->left + (bx ? bx->ml : op->type == BW_OP_BLOCK_START ? op->b : 0);
    oR = s->right - (bx ? bx->mr : 0);
    if (bx) {
        int avail = oR - oL, w = 0;
        if (bx->width > 0) w = bx->width;
        else if (bx->width < 0) w = avail * -bx->width / 100;
        if (bx->maxw > 0 && (w == 0 || w > bx->maxw)) w = bx->maxw;
        else if (bx->maxw < 0 && (w == 0 || w > avail * -bx->maxw / 100)) w = avail * -bx->maxw / 100;
        if (w > 20 && w < avail) {
            if (bx->center) oL += (avail - w) / 2;
            oR = oL + w;
        }
    }
    b->oL = oL;
    b->oR = oR;
    if (op->color >= 0 || (bx && (bx->bt || bx->pt || bx->bl || bx->br || bx->bb))) {
        if (s->pendingMargin && s->y > 0) s->y += s->pendingMargin;
        s->pendingMargin = 0;
    }
    b->top = s->y;
    if (op->color >= 0) {
        b->fill = add_fill(s, oL, s->y, oR - oL, 0, op->color, 0);
        if (b->fill >= 0) s->bg = op->color;
    }
    if (bx) {
        if (bx->bt) { add_fill(s, oL, s->y, oR - oL, bx->bt, bx->bc, 0); s->y += bx->bt; }
        s->y += bx->pt;
        s->left = oL + bx->bl + bx->pl;
        s->right = oR - bx->br - bx->pr;
    } else {
        s->left = oL;
        s->right = oR;
    }
    if (s->left > s->right - 40) s->left = s->right - 40;
    if (s->left < 0) s->left = 0;
    s->align = op->align;
    s->x = s->left;
    (void)l;
}

static void pop_block(S* s, const BwOp* op)
{
    BwLayout* l = &s->L->pub;
    Block* b;
    const BwBlockBox* bx;
    finish_line(s, 0);
    if (s->skipDepth) { s->skipDepth--; return; }
    if (!s->depth) return;
    b = &s->blocks[--s->depth];
    bx = b->box >= 0 ? &s->d->blocks[b->box] : 0;
    if (bx) {
        s->y += bx->pb;
        if (bx->bl) add_fill(s, b->oL, b->top, bx->bl, s->y - b->top + bx->bb, bx->bc, 0);
        if (bx->br) add_fill(s, b->oR - bx->br, b->top, bx->br, s->y - b->top + bx->bb, bx->bc, 0);
        if (bx->bb) { add_fill(s, b->oL, s->y, b->oR - b->oL, bx->bb, bx->bc, 0); s->y += bx->bb; }
    }
    if (b->fill >= 0) {
        BwFill* f = &l->fills[b->fill];
        if (!bx || (!bx->pb && !bx->bb)) s->y += 2;      /* a little room under the text */
        f->h = s->y - f->y;
    }
    s->left = b->left;
    s->right = b->right;
    s->align = b->align;
    s->bg = b->bg;
    s->x = s->left;
    if (op && op->a > s->pendingMargin) s->pendingMargin = op->a;
}

static unsigned int layout_table(S* s, unsigned int i0, unsigned int to);

/* lays out the ops [from, to) at the current position */
static void layout_ops(S* s, unsigned int from, unsigned int to)
{
    unsigned int i;
    for (i = from; i < to && !s->L->pub.failed; i++) {
        const BwOp* op = &s->d->ops[i];
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
            if (op->ref >= 0 && (unsigned int)op->ref < s->d->anchorCount)
                s->L->pub.anchorY[op->ref] = s->y + (s->lineContent ? 0 : s->pendingMargin);
            break;
        case BW_OP_MARKER:
            {
                int f = op->font;
                int w = s->m->textWidth(s->m->ctx, f, s->d->pool + op->off, (int)op->len);
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
        case BW_OP_TABLE_START:
            {
                unsigned int end = layout_table(s, i, to);
                if (end) { i = end; break; }
                /* too wide for columns: the rows stack */
                push_block(s, op);
            }
            break;
        case BW_OP_ROW_START:
            {
                BwOp b = *op;
                b.a = 0; b.b = 0;
                push_block(s, &b);
            }
            break;
        case BW_OP_TABLE_END: case BW_OP_ROW_END:
            pop_block(s, op);
            break;
        case BW_OP_CELL_START: case BW_OP_GAP:
            if (s->x > s->left) {
                s->x += CELL_GAP;
                if (s->x >= s->right) finish_line(s, 0);
            }
            s->pendingSpace = 0;
            break;
        case BW_OP_CELL_END:
            break;
        default: break;
        }
    }
}


/* ------------------------------------------------------------------ tables */

#define MAX_COLS 10
#define MIN_WORD_CAP 150         /* a long word (a URL) is broken: it need not widen its column */

typedef struct {
    unsigned int from, to;       /* the cell's content ops */
    const BwOp* op;              /* its CELL_START */
    int row, col, span;
    int mn, mx;
} Cell;

typedef struct {
    Cell* cells;
    unsigned int count, cap;
    int rows, cols;
    const BwOp** rowOps;         /* ROW_START of each row (NULL: an implied row) */
    unsigned int rowCap;
    unsigned int* outside;       /* ops at the table's own level outside cells (a caption) */
    unsigned int outCount, outCap;
    int colMin[MAX_COLS], colMax[MAX_COLS], colW[MAX_COLS];
    int pad, gap, border, width;
} Table;

/* the TABLE_END matching the TABLE_START at i (to if none) */
static unsigned int table_end(const BwDoc* d, unsigned int i, unsigned int to)
{
    int depth = 0;
    for (; i < to; i++) {
        int t = d->ops[i].type;
        if (t == BW_OP_TABLE_START) depth++;
        else if (t == BW_OP_TABLE_END && --depth == 0) return i;
    }
    return to;
}

static void table_free(Table* t)
{
    ig_free(t->cells);
    ig_free((void*)t->rowOps);
    ig_free(t->outside);
}

static int measure_table(S* s, unsigned int i0, unsigned int end, int avail, Table* t);

/* the narrowest and the widest the ops [from, to) can be laid out (min-content / max-content) */
static void measure_range(S* s, unsigned int from, unsigned int to, int* pmn, int* pmx)
{
    const BwDoc* d = s->d;
    int line = 0, best = 0, mn = 0, space = 0;
    int indent = 0, ind[16], depth = 0;
    unsigned int i;
    for (i = from; i < to; i++) {
        const BwOp* op = &d->ops[i];
        switch (op->type) {
        case BW_OP_TEXT:
            {
                unsigned int k = op->off, e = op->off + op->len;
                while (k < e) {
                    unsigned short c = d->pool[k];
                    if (c == ' ') { space = 1; k++; continue; }
                    if (c == '\n') { if (line + indent > best) best = line + indent; line = 0; space = 0; k++; continue; }
                    {
                        unsigned int ws = k;
                        int w;
                        while (k < e && d->pool[k] != ' ' && d->pool[k] != '\n') k++;
                        w = s->m->textWidth(s->m->ctx, op->font, d->pool + ws, (int)(k - ws));
                        if (space && line) line += space_w(s, op->font);
                        line += w;
                        space = 0;
                        if (w > MIN_WORD_CAP) w = MIN_WORD_CAP;
                        if (w + indent > mn) mn = w + indent;
                    }
                }
            }
            break;
        case BW_OP_IMAGE: case BW_OP_CONTROL:
            {
                int w, h, f;
                if (op->type == BW_OP_IMAGE) w = image_width(s, op);
                else control_size(s, op, &w, &h, &f);
                if (space && line) line += space_w(s, BW_FONT_MAKE(1, 0));
                line += w;
                space = 0;
                if (w > MIN_WORD_CAP) w = MIN_WORD_CAP;
                if (w + indent > mn) mn = w + indent;
            }
            break;
        case BW_OP_CELL_START: case BW_OP_GAP:
            if (line) line += CELL_GAP;
            space = 0;
            break;
        case BW_OP_TABLE_START:
            {
                unsigned int end = table_end(d, i, to);
                Table t;
                if (line + indent > best) best = line + indent;
                line = 0;
                s->measureDepth++;
                if (s->tableDepth + s->measureDepth <= 4 && measure_table(s, i, end, 100000, &t)) {
                    int tmn = t.gap * (t.cols - 1) + t.border * 2, tmx = tmn, c;
                    for (c = 0; c < t.cols; c++) { tmn += t.colMin[c]; tmx += t.colMax[c]; }
                    if (tmn + indent > mn) mn = tmn + indent;
                    if (tmx + indent > best) best = tmx + indent;
                    table_free(&t);
                }
                s->measureDepth--;
                i = end;
            }
            break;
        case BW_OP_BLOCK_START:
            if (line + indent > best) best = line + indent;
            line = 0; space = 0;
            if (depth < 16) ind[depth] = op->b;
            depth++;
            indent += op->b;
            break;
        case BW_OP_BLOCK_END:
            if (line + indent > best) best = line + indent;
            line = 0; space = 0;
            if (depth > 0) { depth--; if (depth < 16) indent -= ind[depth]; }
            break;
        case BW_OP_BR: case BW_OP_HR: case BW_OP_ROW_START: case BW_OP_ROW_END: case BW_OP_TABLE_END:
            if (line + indent > best) best = line + indent;
            line = 0; space = 0;
            break;
        default:
            break;
        }
    }
    if (line + indent > best) best = line + indent;
    *pmn = mn;
    *pmx = best > mn ? best : mn;
}

static int add_cell(Table* t, unsigned int from, unsigned int to, const BwOp* op, int row, int col)
{
    Cell* c;
    if (!grow((void**)&t->cells, &t->cap, t->count, sizeof(Cell))) return 0;
    c = &t->cells[t->count++];
    c->from = from;
    c->to = to;
    c->op = op;
    c->row = row;
    c->col = col;
    c->span = op->a < 1 ? 1 : op->a;
    c->mn = c->mx = 0;
    return 1;
}

/* the table's rows and cells, their min / max widths and the column widths for avail;
 * 0 when it is not a table we can lay out as columns */
static int measure_table(S* s, unsigned int i0, unsigned int end, int avail, Table* t)
{
    const BwDoc* d = s->d;
    const BwOp* top = &d->ops[i0];
    unsigned int i;
    int row = -1, col = 0, c, sumMin, sumMax, extra;
    int busyUntil[MAX_COLS];         /* rowspan: a column is taken up to that row */
    ig_memset(t, 0, sizeof(Table));
    for (c = 0; c < MAX_COLS; c++) busyUntil[c] = 0;
    t->border = top->b ? 1 : 0;
    t->pad = top->ref >= 0 ? (top->ref > 8 ? 8 : top->ref) : (t->border ? 3 : 1);
    t->gap = t->border ? 0 : 6;
    for (i = i0 + 1; i < end; i++) {
        const BwOp* op = &d->ops[i];
        if (op->type == BW_OP_ROW_START) {
            row++;
            col = 0;
            if (!grow((void**)&t->rowOps, &t->rowCap, (unsigned int)row, sizeof(BwOp*))) { table_free(t); return 0; }
            t->rowOps[row] = op;
        } else if (op->type == BW_OP_ROW_END) {
            /* (the next cell without a row starts one) */
        } else if (op->type == BW_OP_CELL_START) {
            unsigned int k;
            int depth = 0;
            if (row < 0 || (i > 0 && d->ops[i - 1].type == BW_OP_ROW_END)) {
                row++;
                col = 0;
                if (!grow((void**)&t->rowOps, &t->rowCap, (unsigned int)row, sizeof(BwOp*))) { table_free(t); return 0; }
                t->rowOps[row] = 0;
            }
            /* its CELL_END at this level */
            for (k = i + 1; k < end; k++) {
                int ty = d->ops[k].type;
                if (ty == BW_OP_TABLE_START) depth++;
                else if (ty == BW_OP_TABLE_END) depth--;
                else if (ty == BW_OP_CELL_END && depth == 0) break;
            }
            /* past the columns a cell above still spans */
            while (col < MAX_COLS && busyUntil[col] > row) col++;
            if (!add_cell(t, i + 1, k, op, row, col)) { table_free(t); return 0; }
            {
                int rs = op->ref > 1 ? op->ref : 1, q;
                for (q = col; q < col + t->cells[t->count - 1].span && q < MAX_COLS; q++) busyUntil[q] = row + rs;
            }
            col += t->cells[t->count - 1].span;
            if (col > t->cols) t->cols = col;
            i = k;
        } else if (op->type == BW_OP_TABLE_START) {
            /* a table straight in a table (no cell): outside content */
            unsigned int e = table_end(d, i, end);
            for (; i <= e && i < end; i++) {
                if (!grow((void**)&t->outside, &t->outCap, t->outCount, sizeof(unsigned int))) { table_free(t); return 0; }
                t->outside[t->outCount++] = i;
            }
            i = e;
        } else if (op->type != BW_OP_ANCHOR) {
            if (op->type == BW_OP_TEXT) {
                unsigned int k, real = 0;
                for (k = op->off; k < op->off + op->len; k++) if (d->pool[k] != ' ' && d->pool[k] != '\n') { real = 1; break; }
                if (!real) continue;
            }
            if (!grow((void**)&t->outside, &t->outCap, t->outCount, sizeof(unsigned int))) { table_free(t); return 0; }
            t->outside[t->outCount++] = i;
        }
    }
    t->rows = row + 1;
    if (t->cols < 1 || t->cols > MAX_COLS || !t->count) { table_free(t); return 0; }
    for (c = 0; c < t->cols; c++) { t->colMin[c] = 0; t->colMax[c] = 0; }
    /* single cells first, then the spanning ones widen their columns if they must */
    for (i = 0; i < t->count; i++) {
        Cell* ce = &t->cells[i];
        measure_range(s, ce->from, ce->to, &ce->mn, &ce->mx);
        ce->mn += 2 * t->pad;
        ce->mx += 2 * t->pad;
        /* width="150" / "30%": the column would like that much (only if there is room) */
        if (ce->op->len) {
            int want = ce->op->len >= 100000u ? (avail < 50000 ? avail * (int)(ce->op->len - 100000u) / 100 : 0) : (int)ce->op->len;
            if (want > ce->mx && want <= avail) ce->mx = want;
        }
        if (ce->span == 1 && ce->col < MAX_COLS) {
            if (ce->mn > t->colMin[ce->col]) t->colMin[ce->col] = ce->mn;
            if (ce->mx > t->colMax[ce->col]) t->colMax[ce->col] = ce->mx;
        }
    }
    for (i = 0; i < t->count; i++) {
        Cell* ce = &t->cells[i];
        int k, n = 0, smn = 0, smx = 0;
        if (ce->span == 1) continue;
        for (k = ce->col; k < ce->col + ce->span && k < t->cols; k++) { smn += t->colMin[k]; smx += t->colMax[k]; n++; }
        if (!n) continue;
        smn += t->gap * (n - 1);
        smx += t->gap * (n - 1);
        for (k = ce->col; k < ce->col + ce->span && k < t->cols; k++) {
            if (ce->mn > smn) t->colMin[k] += (ce->mn - smn + n - 1) / n;
            if (ce->mx > smx) t->colMax[k] += (ce->mx - smx + n - 1) / n;
        }
    }
    sumMin = sumMax = t->gap * (t->cols - 1) + 2 * t->border;
    for (c = 0; c < t->cols; c++) {
        if (t->colMax[c] < t->colMin[c]) t->colMax[c] = t->colMin[c];
        sumMin += t->colMin[c];
        sumMax += t->colMax[c];
    }
    if (sumMin > avail) { table_free(t); return 0; }
    if (sumMax <= avail) {
        for (c = 0; c < t->cols; c++) t->colW[c] = t->colMax[c];
        t->width = sumMax;
    } else {
        /* between: each column gets its min plus a share of what is left, by how much more it wants */
        extra = avail - sumMin;
        t->width = sumMin;
        for (c = 0; c < t->cols; c++) {
            int want = t->colMax[c] - t->colMin[c];
            int add = sumMax > sumMin ? (int)((long)extra * want / (sumMax - sumMin)) : 0;
            t->colW[c] = t->colMin[c] + add;
            t->width += add;
        }
    }
    /* <table width=100%> (or a width that is more than there is): the columns share the rest */
    if (top->len && avail < 50000) {
        int tw = top->len >= 100000u ? avail * (int)(top->len - 100000u) / 100 : (int)top->len;
        if (tw > avail) tw = avail;
        if (tw > t->width) {
            int extra2 = tw - t->width, sumW = 0, given = 0;
            for (c = 0; c < t->cols; c++) sumW += t->colW[c];
            for (c = 0; c < t->cols; c++) {
                int add = sumW ? (int)((long)extra2 * t->colW[c] / sumW) : extra2 / t->cols;
                if (c == t->cols - 1) add = extra2 - given;
                t->colW[c] += add;
                given += add;
            }
            t->width = tw;
        }
    }
    return 1;
}

static int add_fill(S* s, int x, int y, int w, int h, int color, int outline)
{
    BwLayout* l = &s->L->pub;
    BwFill* f;
    if (!grow((void**)&l->fills, &l->fillCap, l->fillCount, sizeof(BwFill))) { l->failed = 1; return -1; }
    f = &l->fills[l->fillCount];
    f->x = x; f->y = y; f->w = w; f->h = h; f->color = color; f->outline = outline;
    return (int)l->fillCount++;
}

/* Lays out the table at op i0 as columns; the index of its TABLE_END, or 0 when it does not fit
 * (the caller then stacks the rows) */
static unsigned int layout_table(S* s, unsigned int i0, unsigned int to)
{
    const BwDoc* d = s->d;
    const BwOp* top = &d->ops[i0];
    unsigned int end = table_end(d, i0, to);
    BwLayout* l = &s->L->pub;
    Table t;
    int tx, avail, r, tableTop, tableFill = -1;
    unsigned int ci = 0;
    if (end >= to || s->tableDepth >= 4) return 0;      /* (the phone's stack may be only 8 KB) */
    finish_line(s, 0);
    avail = s->right - s->left;
    if (!measure_table(s, i0, end, avail, &t)) return 0;
    if (top->a > s->pendingMargin) s->pendingMargin = top->a;
    if (s->pendingMargin && s->y > 0) s->y += s->pendingMargin;
    s->pendingMargin = 0;
    /* a caption or other content outside the cells goes above the columns */
    if (t.outCount) {
        unsigned int k;
        for (k = 0; k < t.outCount; k++) layout_ops(s, t.outside[k], t.outside[k] + 1);
        finish_line(s, 0);
        if (s->pendingMargin && s->y > 0) s->y += s->pendingMargin;
        s->pendingMargin = 0;
    }
    tx = s->left;
    if (top->align == BW_ALIGN_CENTER) tx += (avail - t.width) / 2;
    else if (top->align == BW_ALIGN_RIGHT) tx += avail - t.width;
    tableTop = s->y;
    if (top->color >= 0) tableFill = add_fill(s, tx, tableTop, t.width, 0, top->color, 0);
    for (r = 0; r < t.rows; r++) {
        int rowTop = s->y, rowBottom = s->y + 2;
        unsigned int firstBox = l->boxCount, lineMark = l->lineCount, fillMark = l->fillCount, rc;
        const BwOp* rop = t.rowOps ? t.rowOps[r] : 0;
        int rowBg = rop && rop->color >= 0 ? rop->color : top->color >= 0 ? top->color : s->bg;
        int rowFill = rop && rop->color >= 0 ? add_fill(s, tx, rowTop, t.width, 0, rop->color, 0) : -1;
        unsigned int firstCell = ci;
        /* the cells of this row */
        for (; ci < t.count && t.cells[ci].row == r; ci++) {
            Cell* ce = &t.cells[ci];
            int x0 = tx + t.border, w = 0, k;
            int saveLeft = s->left, saveRight = s->right, saveAlign = s->align, saveBg = s->bg, saveDepth = s->depth;
            int saveSkip = s->skipDepth;
            for (k = 0; k < ce->col && k < t.cols; k++) x0 += t.colW[k] + t.gap;
            for (k = ce->col; k < ce->col + ce->span && k < t.cols; k++) w += t.colW[k] + (k > ce->col ? t.gap : 0);
            if (ce->op->color >= 0) { add_fill(s, x0, rowTop, w, 0, ce->op->color, 0); }
            s->left = x0 + t.pad;
            s->right = x0 + w - t.pad;
            if (s->right <= s->left) s->right = s->left + 1;
            s->x = s->left;
            s->y = rowTop + t.pad;
            s->align = ce->op->align;
            s->bg = ce->op->color >= 0 ? ce->op->color : rowBg;
            s->lineStart = l->boxCount;
            s->lineContent = 0;
            s->pendingSpace = 0;
            s->pendingMargin = 0;
            s->lastOp = -1;
            s->tableDepth++;
            layout_ops(s, ce->from, ce->to);
            s->tableDepth--;
            finish_line(s, 0);
            while (s->depth > saveDepth) pop_block(s, 0);
            s->skipDepth = saveSkip;
            if (s->y + t.pad > rowBottom) rowBottom = s->y + t.pad;
            s->left = saveLeft; s->right = saveRight; s->align = saveAlign; s->bg = saveBg;
            s->pendingMargin = 0;
        }
        /* the row's cell backgrounds and borders reach its bottom */
        for (rc = firstCell; rc < ci; rc++) {
            Cell* ce = &t.cells[rc];
            int x0 = tx + t.border, w = 0, k;
            for (k = 0; k < ce->col && k < t.cols; k++) x0 += t.colW[k] + t.gap;
            for (k = ce->col; k < ce->col + ce->span && k < t.cols; k++) w += t.colW[k] + (k > ce->col ? t.gap : 0);
            if (t.border) add_fill(s, x0, rowTop, w + 1, rowBottom - rowTop + 1, 0x999999, 1);
        }
        {
            unsigned int f;
            for (f = fillMark; f < l->fillCount; f++)
                if (l->fills[f].h == 0 && !l->fills[f].outline && l->fills[f].y == rowTop) l->fills[f].h = rowBottom - rowTop;
        }
        (void)rowFill;
        /* the row is one line: its cells' lines go */
        l->lineCount = lineMark;
        if (grow((void**)&l->lines, &l->lineCap, l->lineCount, sizeof(BwLine))) {
            BwLine* ln = &l->lines[l->lineCount++];
            ln->first = firstBox;
            ln->top = rowTop;
            ln->bottom = rowBottom;
        } else l->failed = 1;
        s->y = rowBottom + (t.border ? 0 : 2);
        s->lineStart = l->boxCount;
        s->lineContent = 0;
    }
    if (t.border) { s->y += 1; add_fill(s, tx, tableTop, t.width, s->y - tableTop, 0x777777, 1); }
    if (tableFill >= 0) l->fills[tableFill].h = s->y - tableTop;
    s->x = s->left;
    s->pendingSpace = 0;
    s->lastOp = -1;
    s->pendingMargin = 4;
    table_free(&t);
    return end;
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
    layout_ops(s, 0, d->opCount);
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
    ig_free(L->pub.lines);
    ig_free(L);
}

unsigned int bw_layout_first_line(const BwLayout* l, int y)
{
    unsigned int lo = 0, hi = l->lineCount;
    while (lo < hi) {
        unsigned int mid = (lo + hi) / 2;
        if (l->lines[mid].bottom <= y) lo = mid + 1; else hi = mid;
    }
    return lo;
}

unsigned int bw_layout_first_box(const BwLayout* l, int y)
{
    unsigned int i = bw_layout_first_line(l, y);
    return i < l->lineCount ? l->lines[i].first : l->boxCount;
}

int bw_layout_hit(const BwLayout* l, int x, int y)
{
    unsigned int li = bw_layout_first_line(l, y - 2);
    int best = -1;
    for (; li < l->lineCount && l->lines[li].top <= y + 2; li++) {
        unsigned int i, e = li + 1 < l->lineCount ? l->lines[li + 1].first : l->boxCount;
        for (i = l->lines[li].first; i < e; i++) {
            const BwBox* b = &l->boxes[i];
            if (b->target >= 0 && x >= b->x - 2 && x < b->x + b->w + 2 && y >= b->y - 1 && y < b->y + b->h + 1) best = b->target;
        }
    }
    return best;
}
