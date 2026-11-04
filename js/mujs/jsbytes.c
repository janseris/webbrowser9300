/* Browser 9300: ArrayBuffer, the typed arrays and DataView in C. A buffer is userdata owning its
 * bytes (BwAB); a view is userdata pointing at a buffer (BwView) and keeps it alive through its
 * "buffer" property. Elements are read and written through the userdata index hooks (geti /
 * puti: no property names), little-endian as on the PC and the phone, doubles in IEEE byte order
 * (the phone's FPA keeps a double's two words the other way round). The methods that need no C
 * are generic JavaScript in es.js (its //@lazy ArrayBuffer block, which calls %bytes first). */

#include "jsi.h"

typedef struct BwAB { unsigned char *p; unsigned int n; } BwAB;
typedef struct { BwAB *ab; unsigned int off, len; int kind, track; } BwView;	/* track: the length follows the buffer's */

enum { K_I8, K_U8, K_U8C, K_I16, K_U16, K_I32, K_U32, K_F32, K_F64, K_BI64, K_BU64, K_COUNT };
static const char *const kname[K_COUNT] = { "Int8Array", "Uint8Array", "Uint8ClampedArray", "Int16Array", "Uint16Array",
	"Int32Array", "Uint32Array", "Float32Array", "Float64Array", "BigInt64Array", "BigUint64Array" };
static const unsigned char ksize[K_COUNT] = { 1, 1, 1, 2, 2, 4, 4, 4, 8, 8, 8 };

/* a view's length now (one made without a length follows its buffer: WebAssembly memory grows) */
static unsigned int vlen(BwView *v)
{
	unsigned int s = ksize[v->kind];
	if (v->track) return v->ab->n > v->off ? (v->ab->n - v->off) / s : 0;
	return v->off + v->len * s <= v->ab->n ? v->len : 0;
}

#define TAG_AB "ArrayBuffer"
#define TAG_VIEW "TypedArray"
#define TAG_DV "DataView"

/* ---- bytes <-> numbers (little-endian) */

/* (no unions of double and int here: GCC 2.9 for the phone stops on them, "insn does not
 * satisfy its constraints"; bytes are copied instead) */
static int fpa_order(void)	/* a double's high word first in memory (the phone's FPA) */
{
	double d = 1.0;
	unsigned char b[8];
	memcpy(b, &d, 8);
	return b[3] == 0x3f;
}

static unsigned int rd32(const unsigned char *b)
{
	return (unsigned int)b[0] | ((unsigned int)b[1] << 8) | ((unsigned int)b[2] << 16) | ((unsigned int)b[3] << 24);
}

static void wr32(unsigned char *b, unsigned int v)
{
	b[0] = (unsigned char)v; b[1] = (unsigned char)(v >> 8); b[2] = (unsigned char)(v >> 16); b[3] = (unsigned char)(v >> 24);
}

double jsB_rdf64(const unsigned char *b)
{
	double d;
	unsigned char t[8];
	if (fpa_order()) { memcpy(t, b + 4, 4); memcpy(t + 4, b, 4); memcpy(&d, t, 8); }
	else memcpy(&d, b, 8);
	return d;
}

void jsB_wrf64(unsigned char *b, double d)
{
	unsigned char t[8];
	memcpy(t, &d, 8);
	if (fpa_order()) { memcpy(b, t + 4, 4); memcpy(b + 4, t, 4); }
	else memcpy(b, t, 8);
}

float jsB_rdf32(const unsigned char *b)
{
	float f;
	memcpy(&f, b, 4);
	return f;
}

void jsB_wrf32(unsigned char *b, float f)
{
	memcpy(b, &f, 4);
}

/* an element as a number (b: its first byte; le: little-endian, for DataView) */
static double getel(int kind, const unsigned char *b, int le)
{
	unsigned char t[8];
	int i, s = ksize[kind];
	if (!le) { for (i = 0; i < s; ++i) t[i] = b[s - 1 - i]; b = t; }
	switch (kind) {
	case K_I8: return (signed char)b[0];
	case K_U8: case K_U8C: return b[0];
	case K_I16: return (short)(b[0] | (b[1] << 8));
	case K_U16: return (unsigned short)(b[0] | (b[1] << 8));
	case K_I32: return (int)rd32(b);
	case K_U32: return rd32(b);
	case K_F32: return jsB_rdf32(b);
	case K_F64: return jsB_rdf64(b);
	case K_BI64: return (double)(int)rd32(b + 4) * 4294967296.0 + rd32(b);
	default: return (double)rd32(b + 4) * 4294967296.0 + rd32(b);
	}
}

static unsigned int by_touint32(double d)
{
	if (!(d == d) || d == 1 / 0.0 || d == -1 / 0.0) return 0;
	d = d < 0 ? -floor(-d) : floor(d);
	d = fmod(d, 4294967296.0);
	if (d < 0) d += 4294967296.0;
	return (unsigned int)d;
}

static void putel(int kind, unsigned char *b, double v, int le)
{
	unsigned char t[8], *o = le ? b : t;
	int i, s = ksize[kind];
	switch (kind) {
	case K_I8: case K_U8: o[0] = (unsigned char)by_touint32(v); break;
	case K_U8C:
		if (!(v > 0)) o[0] = 0;
		else if (v >= 255) o[0] = 255;
		else { double f = floor(v), r = v - f; unsigned int n = (unsigned int)f; if (r > 0.5 || (r == 0.5 && (n & 1))) n++; o[0] = (unsigned char)n; }
		break;
	case K_I16: case K_U16: { unsigned int n = by_touint32(v); o[0] = (unsigned char)n; o[1] = (unsigned char)(n >> 8); } break;
	case K_I32: case K_U32: wr32(o, by_touint32(v)); break;
	case K_F32: jsB_wrf32(o, (float)v); break;
	case K_F64: jsB_wrf64(o, v); break;
	default: {
		double hi = floor(v / 4294967296.0);
		wr32(o, by_touint32(v - hi * 4294967296.0));
		wr32(o + 4, by_touint32(hi));
	}
	}
	if (!le) for (i = 0; i < s; ++i) b[i] = t[s - 1 - i];
}

/* ---- buffers */

static void ab_free(js_State *J, void *p)
{
	BwAB *ab = p;
	if (ab->p) js_free(J, ab->p);
	js_free(J, ab);
}

static int ab_has(js_State *J, void *p, const char *name)
{
	BwAB *ab = p;
	if (!strcmp(name, "byteLength") || !strcmp(name, "maxByteLength")) { js_pushnumber(J, ab->n); return 1; }
	return 0;
}

static int ab_put(js_State *J, void *p, const char *name)
{
	return !strcmp(name, "byteLength");
}

/* a new buffer of n zero bytes on the stack (its prototype from the constructor ArrayBuffer) */
static BwAB *ab_new(js_State *J, unsigned int n)
{
	BwAB *ab;
	if (n > 0x7fffffff) js_rangeerror(J, "Array buffer allocation failed");
	ab = js_malloc(J, sizeof *ab);
	ab->n = 0;
	ab->p = NULL;
	if (js_try(J)) {
		js_free(J, ab);
		js_throw(J);
	}
	if (n) {
		ab->p = js_malloc(J, n);
		memset(ab->p, 0, n);
	}
	ab->n = n;
	js_endtry(J);
	js_getregistry(J, "%abproto");
	js_newuserdatax(J, TAG_AB, ab, ab_has, ab_put, NULL, ab_free);
	return ab;
}

/* the bytes of an ArrayBuffer (for WebAssembly: memory) */
unsigned char *jsB_bufferbytes(js_State *J, int idx, unsigned int *n)
{
	BwAB *ab;
	if (!js_isuserdata(J, idx, TAG_AB)) return NULL;
	ab = js_touserdata(J, idx, TAG_AB);
	*n = ab->n;
	return ab->p;
}

/* a buffer resized in place (WebAssembly's memory.grow; the views see the new bytes) */
int jsB_bufferresize(js_State *J, int idx, unsigned int n)
{
	BwAB *ab;
	unsigned char *q;
	if (!js_isuserdata(J, idx, TAG_AB)) return 0;
	ab = js_touserdata(J, idx, TAG_AB);
	q = J->alloc(J->actx, ab->p, n);
	if (!q && n) return 0;
	if (n > ab->n) memset(q + ab->n, 0, n - ab->n);
	ab->p = q;
	ab->n = n;
	return 1;
}

void jsB_newbuffer(js_State *J, unsigned int n)
{
	ab_new(J, n);
}

static unsigned int toindex(js_State *J, int idx, const char *what)
{
	double d;
	if (js_isundefined(J, idx)) return 0;
	d = js_tonumber(J, idx);
	if (!(d == d)) return 0;
	if (d < 0 || d > 2147483647.0) js_rangeerror(J, "Invalid %s", what);
	return (unsigned int)d;
}

static void ArrayBuffer_new(js_State *J)
{
	ab_new(J, toindex(J, 1, "array buffer length"));
}

static void ArrayBuffer_call(js_State *J)
{
	js_typeerror(J, "Constructor ArrayBuffer requires 'new'");
}

static BwAB *toab(js_State *J, int idx)
{
	if (!js_isuserdata(J, idx, TAG_AB)) js_typeerror(J, "not an ArrayBuffer");
	return js_touserdata(J, idx, TAG_AB);
}

static void clamp_range(js_State *J, int ib, int ie, unsigned int n, unsigned int *b, unsigned int *e)
{
	double s = js_isundefined(J, ib) ? 0 : js_tointeger(J, ib), f = js_isundefined(J, ie) ? n : js_tointeger(J, ie);
	if (s < 0) s += n;
	if (f < 0) f += n;
	if (s < 0) s = 0;
	if (f < 0) f = 0;
	if (s > n) s = n;
	if (f > n) f = n;
	*b = (unsigned int)s;
	*e = f > s ? (unsigned int)f : (unsigned int)s;
}

static void ArrayBuffer_slice(js_State *J)
{
	BwAB *ab = toab(J, 0), *r;
	unsigned int b, e;
	clamp_range(J, 1, 2, ab->n, &b, &e);
	r = ab_new(J, e - b);
	if (e > b) memcpy(r->p, ab->p + b, e - b);
}

static void ArrayBuffer_isView(js_State *J)
{
	js_pushboolean(J, js_isuserdata(J, 1, TAG_VIEW) || js_isuserdata(J, 1, TAG_DV));
}

/* ---- typed arrays */

static void view_free(js_State *J, void *p)
{
	js_free(J, p);
}

static int view_has(js_State *J, void *p, const char *name)
{
	BwView *v = p;
	unsigned int len = vlen(v);
	switch (name[0]) {
	case 'l': if (!strcmp(name, "length")) { js_pushnumber(J, len); return 1; } break;
	case 'b':
		if (!strcmp(name, "byteLength")) { js_pushnumber(J, len * ksize[v->kind]); return 1; }
		if (!strcmp(name, "byteOffset")) { js_pushnumber(J, v->off); return 1; }
		break;
	}
	return 0;
}

static int view_put(js_State *J, void *p, const char *name)
{
	return !strcmp(name, "length") || !strcmp(name, "byteLength") || !strcmp(name, "byteOffset");
}

static int view_geti(js_State *J, void *p, int k)
{
	BwView *v = p;
	unsigned int at = v->off + (unsigned int)k * ksize[v->kind];
	if (k < 0 || (unsigned int)k >= vlen(v)) { js_pushundefined(J); return 1; }
	js_pushnumber(J, getel(v->kind, v->ab->p + at, 1));
	return 1;
}

static int view_puti(js_State *J, void *p, int k)
{
	BwView *v = p;
	unsigned int at = v->off + (unsigned int)k * ksize[v->kind];
	if (k >= 0 && (unsigned int)k < vlen(v))
		putel(v->kind, v->ab->p + at, js_tonumber(J, -1), 1);
	return 1;
}

/* a view of kind on the buffer at bufidx; pushed. proto: of the kind (from the registry) */
static BwView *view_new(js_State *J, int kind, int bufidx, unsigned int off, unsigned int len)
{
	BwView *v;
	BwAB *ab = toab(J, bufidx);
	bufidx = bufidx < 0 ? js_gettop(J) + bufidx : bufidx;
	v = js_malloc(J, sizeof *v);
	v->ab = ab; v->off = off; v->len = len; v->kind = kind; v->track = 0;
	js_getregistry(J, kname[kind]);
	js_newuserdatax(J, TAG_VIEW, v, view_has, view_put, NULL, view_free);
	js_setuserindex(J, -1, view_geti, view_puti);
	js_copy(J, bufidx);
	js_defproperty(J, -2, "buffer", JS_READONLY | JS_DONTENUM | JS_DONTCONF);
	return v;
}

static BwView *toview(js_State *J, int idx)
{
	if (!js_isuserdata(J, idx, TAG_VIEW)) js_typeerror(J, "not a typed array");
	return js_touserdata(J, idx, TAG_VIEW);
}

/* the kind a constructor makes: its BYTES_PER_ELEMENT kept in a hidden %k */
static int ctor_kind(js_State *J)
{
	int k;
	js_currentfunction(J);
	js_getproperty(J, -1, "%k");
	k = js_tointeger(J, -1);
	js_pop(J, 2);
	if (k < 0 || k >= K_COUNT) js_typeerror(J, "not a typed array constructor");
	return k;
}

/* new kind(len) on a fresh buffer, pushed */
static BwView *view_alloc(js_State *J, int kind, unsigned int len)
{
	BwView *v;
	if (len > 0x7fffffff / ksize[kind]) js_rangeerror(J, "Invalid typed array length: %u", len);
	ab_new(J, len * ksize[kind]);
	v = view_new(J, kind, -1, 0, len);
	js_rot2pop1(J);
	return v;
}

/* values from the array-like at idx into the view v (from index at) */
static void view_fill_from(js_State *J, BwView *v, int idx, unsigned int at, unsigned int n)
{
	unsigned int i;
	for (i = 0; i < n; ++i) {
		js_getindex(J, idx, (int)i);
		if (at + i < vlen(v))
			putel(v->kind, v->ab->p + v->off + (at + i) * ksize[v->kind], js_tonumber(J, -1), 1);
		js_pop(J, 1);
	}
}

static void TypedArray_new(js_State *J)
{
	int kind = ctor_kind(J);
	BwView *v;
	if (js_isuserdata(J, 1, TAG_AB)) {	/* (buffer, byteOffset, length): a view */
		BwAB *ab = toab(J, 1);
		unsigned int off = toindex(J, 2, "offset"), len;
		if (off % ksize[kind]) js_rangeerror(J, "start offset of %s should be a multiple of %d", kname[kind], ksize[kind]);
		if (js_isundefined(J, 3)) {
			if (off > ab->n || (ab->n - off) % ksize[kind]) js_rangeerror(J, "byte length of %s should be a multiple of %d", kname[kind], ksize[kind]);
			len = (ab->n - off) / ksize[kind];
			view_new(J, kind, 1, off, len)->track = 1;
			return;
		} else {
			len = toindex(J, 3, "typed array length");
			if (off + (double)len * ksize[kind] > ab->n) js_rangeerror(J, "Invalid typed array length: %u", len);
		}
		view_new(J, kind, 1, off, len);
		return;
	}
	if (!js_isobject(J, 1)) {	/* (length) */
		view_alloc(J, kind, toindex(J, 1, "typed array length"));
		return;
	}
	if (js_isuserdata(J, 1, TAG_VIEW)) {	/* (typed array): its values */
		BwView *s = toview(J, 1);
		unsigned int i;
		v = view_alloc(J, kind, vlen(s));
		for (i = 0; i < vlen(s); ++i)
			putel(kind, v->ab->p + i * ksize[kind], getel(s->kind, s->ab->p + s->off + i * ksize[s->kind], 1), 1);
		return;
	}
	if (!js_hasproperty(J, 1, "length")) {	/* an iterable: as an array first */
		js_getglobal(J, "%arr");
		js_pushundefined(J);
		js_copy(J, 1);
		js_call(J, 1);
		js_replace(J, 1);
	} else {
		js_pop(J, 1);
	}
	{
		unsigned int n = js_getlength(J, 1);
		v = view_alloc(J, kind, n);
		view_fill_from(J, v, 1, 0, n);
	}
}

static void TypedArray_call(js_State *J)
{
	js_typeerror(J, "Constructor %s requires 'new'", kname[ctor_kind(J)]);
}

/* ta.set(source, offset) */
static void TA_set(js_State *J)
{
	BwView *v = toview(J, 0);
	unsigned int off = toindex(J, 2, "offset"), n;
	if (js_isuserdata(J, 1, TAG_VIEW)) {
		BwView *s = toview(J, 1);
		if (off + (double)vlen(s) > vlen(v)) js_rangeerror(J, "offset is out of bounds");
		if (s->kind == v->kind || (ksize[s->kind] == 1 && ksize[v->kind] == 1 && v->kind != K_U8C))
			memmove(v->ab->p + v->off + off * ksize[v->kind], s->ab->p + s->off, vlen(s) * ksize[s->kind]);
		else {
			unsigned int i;
			if (s->ab == v->ab) {	/* (overlapping: through a copy) */
				double *t = js_malloc(J, vlen(s) * sizeof(double) + 1);
				for (i = 0; i < vlen(s); ++i) t[i] = getel(s->kind, s->ab->p + s->off + i * ksize[s->kind], 1);
				for (i = 0; i < vlen(s); ++i) putel(v->kind, v->ab->p + v->off + (off + i) * ksize[v->kind], t[i], 1);
				js_free(J, t);
			} else {
				for (i = 0; i < vlen(s); ++i)
					putel(v->kind, v->ab->p + v->off + (off + i) * ksize[v->kind], getel(s->kind, s->ab->p + s->off + i * ksize[s->kind], 1), 1);
			}
		}
	} else {
		if (!js_isobject(J, 1)) js_typeerror(J, "invalid source for set");
		n = js_getlength(J, 1);
		if (off + (double)n > vlen(v)) js_rangeerror(J, "offset is out of bounds");
		view_fill_from(J, v, 1, off, n);
	}
	js_pushundefined(J);
}

/* ta.subarray(begin, end): a view of the same buffer */
static void TA_subarray(js_State *J)
{
	BwView *v = toview(J, 0);
	unsigned int b, e;
	clamp_range(J, 1, 2, vlen(v), &b, &e);
	js_getproperty(J, 0, "buffer");
	view_new(J, v->kind, -1, v->off + b * ksize[v->kind], e - b);
}

/* ta.slice(begin, end): a copy */
static void TA_slice(js_State *J)
{
	BwView *v = toview(J, 0), *r;
	unsigned int b, e;
	clamp_range(J, 1, 2, vlen(v), &b, &e);
	r = view_alloc(J, v->kind, e - b);
	if (e > b) memcpy(r->ab->p, v->ab->p + v->off + b * ksize[v->kind], (e - b) * ksize[v->kind]);
}

static void TA_fill(js_State *J)
{
	BwView *v = toview(J, 0);
	unsigned int b, e, i, s = ksize[v->kind];
	double x = js_tonumber(J, 1);
	clamp_range(J, 2, 3, vlen(v), &b, &e);
	if (e > b) {
		unsigned char *p = v->ab->p + v->off;
		putel(v->kind, p + b * s, x, 1);
		for (i = b + 1; i < e; ++i) memcpy(p + i * s, p + b * s, s);
	}
	js_copy(J, 0);
}

static void TA_copyWithin(js_State *J)
{
	BwView *v = toview(J, 0);
	unsigned int t, b, e, s = ksize[v->kind];
	double d = js_tointeger(J, 1);
	if (d < 0) d += vlen(v);
	t = d < 0 ? 0 : d > vlen(v) ? vlen(v) : (unsigned int)d;
	clamp_range(J, 2, 3, vlen(v), &b, &e);
	if (e - b > vlen(v) - t) e = b + (vlen(v) - t);
	if (e > b) memmove(v->ab->p + v->off + t * s, v->ab->p + v->off + b * s, (e - b) * s);
	js_copy(J, 0);
}

/* ---- DataView */

typedef struct { BwAB *ab; unsigned int off, len; } BwDV;

static int dv_has(js_State *J, void *p, const char *name)
{
	BwDV *d = p;
	if (!strcmp(name, "byteLength")) { js_pushnumber(J, d->len); return 1; }
	if (!strcmp(name, "byteOffset")) { js_pushnumber(J, d->off); return 1; }
	return 0;
}

static void DataView_new(js_State *J)
{
	BwAB *ab = toab(J, 1);
	BwDV *d;
	unsigned int off = toindex(J, 2, "DataView offset"), len;
	if (off > ab->n) js_rangeerror(J, "Start offset %u is outside the bounds of the buffer", off);
	len = js_isundefined(J, 3) ? ab->n - off : toindex(J, 3, "DataView length");
	if (off + (double)len > ab->n) js_rangeerror(J, "Invalid DataView length %u", len);
	d = js_malloc(J, sizeof *d);
	d->ab = ab; d->off = off; d->len = len;
	js_getregistry(J, "%dvproto");
	js_newuserdatax(J, TAG_DV, d, dv_has, NULL, NULL, view_free);
	js_copy(J, 1);
	js_defproperty(J, -2, "buffer", JS_READONLY | JS_DONTENUM | JS_DONTCONF);
}

static void DataView_call(js_State *J)
{
	js_typeerror(J, "Constructor DataView requires 'new'");
}

static BwDV *todv(js_State *J, unsigned int *at, int kind)
{
	BwDV *d;
	unsigned int o;
	if (!js_isuserdata(J, 0, TAG_DV)) js_typeerror(J, "not a DataView");
	d = js_touserdata(J, 0, TAG_DV);
	o = toindex(J, 1, "offset");
	if (o + ksize[kind] > d->len || d->off + d->len > d->ab->n) js_rangeerror(J, "Offset is outside the bounds of the DataView");
	*at = d->off + o;
	return d;
}

static void dv_get(js_State *J, int kind)
{
	unsigned int at;
	BwDV *d = todv(J, &at, kind);
	js_pushnumber(J, getel(kind, d->ab->p + at, js_toboolean(J, 2)));
}

static void dv_set(js_State *J, int kind)
{
	unsigned int at;
	BwDV *d = todv(J, &at, kind);
	putel(kind, d->ab->p + at, js_tonumber(J, 2), js_toboolean(J, 3));
	js_pushundefined(J);
}

#define DV(K, N) \
	static void DV_get##N(js_State *J) { dv_get(J, K); } \
	static void DV_set##N(js_State *J) { dv_set(J, K); }
DV(K_I8, Int8) DV(K_U8, Uint8) DV(K_I16, Int16) DV(K_U16, Uint16) DV(K_I32, Int32) DV(K_U32, Uint32)
DV(K_F32, Float32) DV(K_F64, Float64) DV(K_BI64, BigInt64) DV(K_BU64, BigUint64)

/* ---- setup: %bytes() makes the globals (es.js calls it when a script first names one) */

static void method(js_State *J, const char *name, js_CFunction f, int n)
{
	js_newcfunction(J, f, name, n);
	js_defproperty(J, -2, name, JS_DONTENUM);
}

static void ES_bytes(js_State *J)
{
	int k;
	/* ArrayBuffer */
	js_newobject(J);
	method(J, "slice", ArrayBuffer_slice, 2);
	js_copy(J, -1);
	js_setregistry(J, "%abproto");
	js_newcconstructor(J, ArrayBuffer_call, ArrayBuffer_new, "ArrayBuffer", 1);
	method(J, "isView", ArrayBuffer_isView, 1);
	js_defglobal(J, "ArrayBuffer", JS_DONTENUM);

	/* %TypedArray%.prototype: the methods every kind shares */
	js_newobject(J);
	method(J, "set", TA_set, 1);
	method(J, "subarray", TA_subarray, 2);
	method(J, "slice", TA_slice, 2);
	method(J, "fill", TA_fill, 1);
	method(J, "copyWithin", TA_copyWithin, 2);
	js_copy(J, -1);
	js_setregistry(J, "%taproto");
	js_defglobal(J, "%TypedArrayPrototype", JS_DONTENUM);
	for (k = 0; k < K_COUNT; ++k) {
		js_getregistry(J, "%taproto");
		js_newobjectx(J);	/* the kind's prototype, from %TypedArray%.prototype */
		js_pushnumber(J, ksize[k]);
		js_defproperty(J, -2, "BYTES_PER_ELEMENT", JS_READONLY | JS_DONTENUM | JS_DONTCONF);
		js_copy(J, -1);
		js_setregistry(J, kname[k]);
		js_newcconstructor(J, TypedArray_call, TypedArray_new, kname[k], 3);
		js_pushnumber(J, k);
		js_defproperty(J, -2, "%k", JS_READONLY | JS_DONTENUM | JS_DONTCONF);
		js_pushnumber(J, ksize[k]);
		js_defproperty(J, -2, "BYTES_PER_ELEMENT", JS_READONLY | JS_DONTENUM | JS_DONTCONF);
		js_defglobal(J, kname[k], JS_DONTENUM);
	}

	/* DataView */
	js_newobject(J);
#define DVM(N) method(J, "get" #N, DV_get##N, 1); method(J, "set" #N, DV_set##N, 2);
	DVM(Int8) DVM(Uint8) DVM(Int16) DVM(Uint16) DVM(Int32) DVM(Uint32) DVM(Float32) DVM(Float64) DVM(BigInt64) DVM(BigUint64)
	js_copy(J, -1);
	js_setregistry(J, "%dvproto");
	js_newcconstructor(J, DataView_call, DataView_new, "DataView", 1);
	js_defglobal(J, "DataView", JS_DONTENUM);
	js_pushundefined(J);
}

void jsB_initbytes(js_State *J)
{
	js_newcfunction(J, ES_bytes, "%bytes", 0);
	js_defglobal(J, "%bytes", JS_DONTENUM);
}
