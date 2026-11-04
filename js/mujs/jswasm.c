/* Browser 9300: a WebAssembly interpreter (the MVP, with sign extension, saturating
 * conversions, bulk memory and multi-value blocks). A module's binary is decoded into WMod; each
 * function's blocks are matched once (its first call) into a table of where each block's else and
 * end are; the instructions are then interpreted from the binary as they are, with one value
 * stack and one frame stack (no C recursion between wasm functions; a call to an imported
 * JavaScript function and back is the only nesting). Memory is an ArrayBuffer (jsbytes.c, the
 * same unit through one.c): JavaScript's views of it see the same bytes. The JavaScript API
 * (WebAssembly.Module, Instance, Memory, ...) is es.js's //@lazy WebAssembly block over %wasm*. */

#include "jsi.h"

typedef long long i64;
typedef unsigned long long u64;

typedef union { int i32; i64 i64; float f32; double f64; } WVal;

enum { T_I32 = 0x7f, T_I64 = 0x7e, T_F32 = 0x7d, T_F64 = 0x7c, T_FUNCREF = 0x70, T_EXTERNREF = 0x6f };

typedef struct { int np, nr; unsigned char p[16], r[8]; } WType;
typedef struct { const char *mod, *name; int kind; unsigned int idx; unsigned char gtype, gmut; } WImport;	/* kind 0 func 1 table 2 mem 3 global */
typedef struct { const char *name; int kind; unsigned int idx; } WExport;
typedef struct { unsigned char type, mut; const unsigned char *init; } WGlobal;
typedef struct { unsigned int pos, elsepos, end; } WCtl;
typedef struct {
	unsigned int type;		/* its type */
	const unsigned char *code, *end;	/* the body after the locals */
	int nlocals;			/* (beyond the parameters) */
	unsigned char *ltypes;
	WCtl *ctl; int nctl;		/* its blocks (when first called) */
} WFunc;
typedef struct { const unsigned char *offset; unsigned int n; unsigned int *funcs; int passive; } WElem;
typedef struct { const unsigned char *offset; const unsigned char *bytes; unsigned int n; int passive; } WData;

typedef struct {
	unsigned char *bin; unsigned int binlen;	/* the module's bytes (a copy) */
	WType *types; unsigned int ntypes;
	WImport *imports; unsigned int nimports;
	unsigned int nfimports, ngimports;		/* imported functions and globals come first */
	WFunc *funcs; unsigned int nfuncs;		/* all, imported ones too (no code) */
	int hastable; unsigned int tmin, tmax;
	int hasmem; unsigned int mmin, mmax;
	WGlobal *globals; unsigned int nglobals;
	WExport *exports; unsigned int nexports;
	int start;
	WElem *elems; unsigned int nelems;
	WData *datas; unsigned int ndatas;
} WMod;

typedef struct {
	WMod *m;			/* (the module's userdata is kept alive by the instance's %module) */
	BwAB *mem;			/* the memory's bytes (its ArrayBuffer kept in the instance's %memory) */
	unsigned int mmax;
	WVal *globals;
	int *table; unsigned int ntable;	/* function indices, -1: none */
	unsigned char *dropped;		/* data segments dropped */
	js_State *J;
} WInst;

#define TAG_MOD "WebAssembly.Module"
#define TAG_INST "WebAssembly.Instance"

/* ---- errors */

static void wtrap(js_State *J, const char *msg)
{
	js_getglobal(J, "%wasmTrap");
	js_pushundefined(J);
	js_pushstring(J, msg);
	js_call(J, 1);	/* (throws a WebAssembly.RuntimeError) */
	js_error(J, "%s", msg);
}

static void wbad(js_State *J, const char *msg)
{
	js_getglobal(J, "%wasmCompileError");
	js_pushundefined(J);
	js_pushstring(J, msg);
	js_call(J, 1);
	js_error(J, "%s", msg);
}

/* ---- decoding */

typedef struct { js_State *J; const unsigned char *p, *e; } WRd;

static unsigned int rd_u8(WRd *r)
{
	if (r->p >= r->e) wbad(r->J, "unexpected end of the module");
	return *r->p++;
}

static unsigned int rd_u32(WRd *r)
{
	unsigned int v = 0, s = 0, b;
	do {
		b = rd_u8(r);
		if (s < 32) v |= (b & 0x7f) << s;
		s += 7;
	} while (b & 0x80);
	return v;
}

static int lebs32(const unsigned char **pp)
{
	const unsigned char *p = *pp;
	unsigned int v = 0, s = 0, b;
	do {
		b = *p++;
		if (s < 32) v |= (b & 0x7f) << s;
		s += 7;
	} while (b & 0x80);
	if (s < 32 && (b & 0x40)) v |= ~0u << s;
	*pp = p;
	return (int)v;
}

static i64 lebs64(const unsigned char **pp)
{
	const unsigned char *p = *pp;
	u64 v = 0;
	unsigned int s = 0, b;
	do {
		b = *p++;
		if (s < 64) v |= (u64)(b & 0x7f) << s;
		s += 7;
	} while (b & 0x80);
	if (s < 64 && (b & 0x40)) v |= ~(u64)0 << s;
	*pp = p;
	return (i64)v;
}

static unsigned int lebu32(const unsigned char **pp)
{
	const unsigned char *p = *pp;
	unsigned int v = 0, s = 0, b;
	do {
		b = *p++;
		if (s < 32) v |= (b & 0x7f) << s;
		s += 7;
	} while (b & 0x80);
	*pp = p;
	return v;
}

static const char *rd_name(WRd *r)
{
	unsigned int n = rd_u32(r);
	char *s;
	if ((unsigned int)(r->e - r->p) < n) wbad(r->J, "unexpected end of the module");
	s = js_malloc(r->J, n + 1);
	memcpy(s, r->p, n);
	s[n] = 0;
	r->p += n;
	return s;
}

static void rd_limits(WRd *r, unsigned int *mn, unsigned int *mx)
{
	unsigned int f = rd_u8(r);
	*mn = rd_u32(r);
	*mx = (f & 1) ? rd_u32(r) : 0xffffffffu;
}

/* past a constant expression (its start is kept: evaluated when instantiating) */
static const unsigned char *rd_const(WRd *r)
{
	const unsigned char *s = r->p;
	for (;;) {
		unsigned int op = rd_u8(r);
		if (op == 0x0b) return s;
		switch (op) {
		case 0x41: lebs32(&r->p); break;
		case 0x42: lebs64(&r->p); break;
		case 0x43: r->p += 4; break;
		case 0x44: r->p += 8; break;
		case 0x23: case 0xd2: rd_u32(r); break;
		case 0xd0: rd_u8(r); break;
		case 0x6a: case 0x6b: case 0x6c: case 0x7c: case 0x7d: case 0x7e: break;	/* (extended constants) */
		default: wbad(r->J, "unsupported constant expression");
		}
		if (r->p > r->e) wbad(r->J, "unexpected end of the module");
	}
}

static void mod_free(js_State *J, void *p)
{
	WMod *m = p;
	unsigned int i;
	if (!m) return;
	for (i = 0; i < m->nimports; ++i) { js_free(J, (void *)m->imports[i].mod); js_free(J, (void *)m->imports[i].name); }
	for (i = 0; i < m->nexports; ++i) js_free(J, (void *)m->exports[i].name);
	for (i = 0; i < m->nfuncs; ++i) { js_free(J, m->funcs[i].ltypes); js_free(J, m->funcs[i].ctl); }
	for (i = 0; i < m->nelems; ++i) js_free(J, m->elems[i].funcs);
	js_free(J, m->types); js_free(J, m->imports); js_free(J, m->funcs); js_free(J, m->globals);
	js_free(J, m->exports); js_free(J, m->elems); js_free(J, m->datas); js_free(J, m->bin);
	js_free(J, m);
}

static void *zalloc(js_State *J, unsigned int n)
{
	void *p = js_malloc(J, n ? n : 1);
	memset(p, 0, n ? n : 1);
	return p;
}

static void rd_valtype(WRd *r, unsigned char *t)
{
	unsigned int v = rd_u8(r);
	if (v != T_I32 && v != T_I64 && v != T_F32 && v != T_F64 && v != T_FUNCREF && v != T_EXTERNREF)
		wbad(r->J, "unsupported value type");
	*t = (unsigned char)v;
}

static void decode(js_State *J, WMod *m)
{
	WRd r;
	unsigned int nfdecl = 0, *fdecl = NULL;
	r.J = J;
	r.p = m->bin;
	r.e = m->bin + m->binlen;
	if (m->binlen < 8 || memcmp(m->bin, "\0asm", 4) || m->bin[4] != 1 || m->bin[5] || m->bin[6] || m->bin[7])
		wbad(J, "not a WebAssembly module (version 1)");
	r.p += 8;
	m->start = -1;
	while (r.p < r.e) {
		unsigned int id = rd_u8(&r), len = rd_u32(&r), i, j, n;
		const unsigned char *end = r.p + len;
		if (end > r.e) wbad(J, "a section past the end of the module");
		switch (id) {
		case 1:	/* types */
			n = rd_u32(&r);
			m->types = zalloc(J, n * sizeof *m->types);
			m->ntypes = n;
			for (i = 0; i < n; ++i) {
				WType *t = &m->types[i];
				if (rd_u8(&r) != 0x60) wbad(J, "bad function type");
				t->np = rd_u32(&r);
				if (t->np > 16) wbad(J, "too many parameters (16)");
				for (j = 0; j < (unsigned int)t->np; ++j) rd_valtype(&r, &t->p[j]);
				t->nr = rd_u32(&r);
				if (t->nr > 8) wbad(J, "too many results (8)");
				for (j = 0; j < (unsigned int)t->nr; ++j) rd_valtype(&r, &t->r[j]);
			}
			break;
		case 2:	/* imports */
			n = rd_u32(&r);
			m->imports = zalloc(J, n * sizeof *m->imports);
			for (i = 0; i < n; ++i) {
				WImport *im = &m->imports[i];
				m->nimports = i + 1;
				im->mod = rd_name(&r);
				im->name = rd_name(&r);
				im->kind = rd_u8(&r);
				switch (im->kind) {
				case 0: im->idx = rd_u32(&r); if (im->idx >= m->ntypes) wbad(J, "bad type index"); m->nfimports++; break;
				case 1: rd_u8(&r); rd_limits(&r, &m->tmin, &m->tmax); m->hastable = 2; break;
				case 2: rd_limits(&r, &m->mmin, &m->mmax); m->hasmem = 2; break;
				case 3: rd_valtype(&r, &im->gtype); im->gmut = rd_u8(&r); m->ngimports++; break;
				default: wbad(J, "bad import kind");
				}
			}
			break;
		case 3:	/* functions: their types */
			nfdecl = rd_u32(&r);
			fdecl = zalloc(J, nfdecl * sizeof *fdecl);
			for (i = 0; i < nfdecl; ++i) { fdecl[i] = rd_u32(&r); if (fdecl[i] >= m->ntypes) wbad(J, "bad type index"); }
			break;
		case 4:	/* table */
			n = rd_u32(&r);
			if (n > 1 || (n && m->hastable)) wbad(J, "more than one table");
			if (n) { rd_u8(&r); rd_limits(&r, &m->tmin, &m->tmax); m->hastable = 1; }
			break;
		case 5:	/* memory */
			n = rd_u32(&r);
			if (n > 1 || (n && m->hasmem)) wbad(J, "more than one memory");
			if (n) { rd_limits(&r, &m->mmin, &m->mmax); m->hasmem = 1; }
			break;
		case 6:	/* globals */
			n = rd_u32(&r);
			m->globals = zalloc(J, (n + m->ngimports) * sizeof *m->globals);
			for (i = 0, j = 0; i < m->nimports; ++i)
				if (m->imports[i].kind == 3) { m->globals[j].type = m->imports[i].gtype; m->globals[j].mut = m->imports[i].gmut; j++; }
			for (i = 0; i < n; ++i) {
				WGlobal *g = &m->globals[m->ngimports + i];
				rd_valtype(&r, &g->type);
				g->mut = rd_u8(&r);
				g->init = rd_const(&r);
			}
			m->nglobals = m->ngimports + n;
			break;
		case 7:	/* exports */
			n = rd_u32(&r);
			m->exports = zalloc(J, n * sizeof *m->exports);
			for (i = 0; i < n; ++i) {
				m->nexports = i + 1;
				m->exports[i].name = rd_name(&r);
				m->exports[i].kind = rd_u8(&r);
				m->exports[i].idx = rd_u32(&r);
			}
			break;
		case 8: m->start = rd_u32(&r); break;
		case 9:	/* elements (function indices) */
			n = rd_u32(&r);
			m->elems = zalloc(J, n * sizeof *m->elems);
			for (i = 0; i < n; ++i) {
				WElem *el = &m->elems[i];
				unsigned int f = rd_u32(&r), k;
				m->nelems = i + 1;
				if (f > 7) wbad(J, "bad element segment");
				if (f & 1) el->passive = 1;	/* (passive or declarative) */
				if (!(f & 1)) {
					if (f & 2) rd_u32(&r);	/* (table index) */
					el->offset = rd_const(&r);
				} else if (f & 2) {
					/* (declarative) */
				}
				if (f & 4) {	/* expressions */
					if (f & 3) rd_u8(&r);
					el->n = rd_u32(&r);
					el->funcs = zalloc(J, el->n * sizeof *el->funcs);
					for (k = 0; k < el->n; ++k) {
						unsigned int op = rd_u8(&r);
						if (op == 0xd2) el->funcs[k] = rd_u32(&r);
						else { el->funcs[k] = 0xffffffffu; if (op == 0xd0) rd_u8(&r); }
						if (rd_u8(&r) != 0x0b) wbad(J, "bad element expression");
					}
				} else {
					if (f & 3) rd_u8(&r);	/* (elemkind) */
					el->n = rd_u32(&r);
					el->funcs = zalloc(J, el->n * sizeof *el->funcs);
					for (k = 0; k < el->n; ++k) el->funcs[k] = rd_u32(&r);
				}
			}
			break;
		case 10:	/* code */
			n = rd_u32(&r);
			if (n != nfdecl) wbad(J, "the function and code sections differ");
			m->nfuncs = m->nfimports + n;
			m->funcs = zalloc(J, m->nfuncs * sizeof *m->funcs);
			for (i = 0, j = 0; i < m->nimports; ++i)
				if (m->imports[i].kind == 0) m->funcs[j++].type = m->imports[i].idx;
			for (i = 0; i < n; ++i) {
				WFunc *f = &m->funcs[m->nfimports + i];
				unsigned int size = rd_u32(&r), groups, g, cnt, k;
				const unsigned char *fend = r.p + size, *save;
				unsigned char t;
				if (fend > end) wbad(J, "a function past its section");
				f->type = fdecl[i];
				groups = rd_u32(&r);
				save = r.p;
				for (g = 0, cnt = 0; g < groups; ++g) { cnt += rd_u32(&r); rd_valtype(&r, &t); }
				if (cnt > 50000) wbad(J, "too many locals");
				f->ltypes = zalloc(J, cnt);
				f->nlocals = (int)cnt;
				r.p = save;
				for (g = 0, k = 0; g < groups; ++g) {
					unsigned int c = rd_u32(&r);
					rd_valtype(&r, &t);
					while (c--) f->ltypes[k++] = t;
				}
				f->code = r.p;
				f->end = fend;
				r.p = fend;
			}
			break;
		case 11:	/* data */
			n = rd_u32(&r);
			m->datas = zalloc(J, n * sizeof *m->datas);
			for (i = 0; i < n; ++i) {
				WData *d = &m->datas[i];
				unsigned int f = rd_u32(&r);
				m->ndatas = i + 1;
				if (f == 1) d->passive = 1;
				else {
					if (f == 2) rd_u32(&r);
					d->offset = rd_const(&r);
				}
				d->n = rd_u32(&r);
				if ((unsigned int)(r.e - r.p) < d->n) wbad(J, "a data segment past the end");
				d->bytes = r.p;
				r.p += d->n;
			}
			break;
		default:	/* custom (0), data count (12) and the rest: skipped */
			break;
		}
		r.p = end;
	}
	if (nfdecl && !m->funcs) wbad(J, "functions without code");
	if (!m->funcs) { m->nfuncs = m->nfimports; m->funcs = zalloc(J, (m->nfuncs + 1) * sizeof *m->funcs);
		{ unsigned int i, j; for (i = 0, j = 0; i < m->nimports; ++i) if (m->imports[i].kind == 0) m->funcs[j++].type = m->imports[i].idx; } }
	if (!m->globals && m->ngimports) {
		unsigned int i, j;
		m->globals = zalloc(J, m->ngimports * sizeof *m->globals);
		for (i = 0, j = 0; i < m->nimports; ++i)
			if (m->imports[i].kind == 3) { m->globals[j].type = m->imports[i].gtype; m->globals[j].mut = m->imports[i].gmut; j++; }
		m->nglobals = m->ngimports;
	}
	js_free(J, fdecl);
}

/* %wasmCompile(bytes): the module (userdata) */
static void W_compile(js_State *J)
{
	WMod *m;
	unsigned int n = 0, i;
	const unsigned char *b = NULL;
	if (js_isuserdata(J, 1, TAG_AB)) {
		BwAB *ab = js_touserdata(J, 1, TAG_AB);
		b = ab->p; n = ab->n;
	} else if (js_isuserdata(J, 1, TAG_VIEW)) {
		BwView *v = js_touserdata(J, 1, TAG_VIEW);
		b = v->ab->p + v->off; n = vlen(v) * ksize[v->kind];
	} else
		js_typeerror(J, "WebAssembly.Module(): Argument 0 must be a buffer source");
	m = zalloc(J, sizeof *m);
	if (js_try(J)) {
		mod_free(J, m);
		js_throw(J);
	}
	m->bin = js_malloc(J, n ? n : 1);
	m->binlen = n;
	if (n) memcpy(m->bin, b, n);
	decode(J, m);
	for (i = 0; i < m->nexports; ++i)
		if ((m->exports[i].kind == 0 && m->exports[i].idx >= m->nfuncs) || (m->exports[i].kind == 3 && m->exports[i].idx >= m->nglobals))
			wbad(J, "an export of nothing");
	js_endtry(J);
	js_pushundefined(J);
	js_newuserdata(J, TAG_MOD, m, mod_free);
}

static WMod *tomod(js_State *J, int idx)
{
	if (!js_isuserdata(J, idx, TAG_MOD)) js_typeerror(J, "not a WebAssembly.Module");
	return js_touserdata(J, idx, TAG_MOD);
}

static const char *const kindname[4] = { "function", "table", "memory", "global" };

/* %wasmImports(module): [{ module, name, kind }] */
static void W_imports(js_State *J)
{
	WMod *m = tomod(J, 1);
	unsigned int i;
	js_newarray(J);
	for (i = 0; i < m->nimports; ++i) {
		js_newobject(J);
		js_pushstring(J, m->imports[i].mod); js_setproperty(J, -2, "module");
		js_pushstring(J, m->imports[i].name); js_setproperty(J, -2, "name");
		js_pushstring(J, kindname[m->imports[i].kind & 3]); js_setproperty(J, -2, "kind");
		js_setindex(J, -2, i);
	}
}

/* %wasmExports(module): [{ name, kind, index, params }] */
static void W_exports(js_State *J)
{
	WMod *m = tomod(J, 1);
	unsigned int i;
	js_newarray(J);
	for (i = 0; i < m->nexports; ++i) {
		js_newobject(J);
		js_pushstring(J, m->exports[i].name); js_setproperty(J, -2, "name");
		js_pushstring(J, kindname[m->exports[i].kind & 3]); js_setproperty(J, -2, "kind");
		js_pushnumber(J, m->exports[i].idx); js_setproperty(J, -2, "index");
		if (m->exports[i].kind == 0) { js_pushnumber(J, m->types[m->funcs[m->exports[i].idx].type].np); js_setproperty(J, -2, "params"); }
		js_setindex(J, -2, i);
	}
}

/* %wasmMemoryInfo(module): [min, max] pages of its own or imported memory, or null */
static void W_meminfo(js_State *J)
{
	WMod *m = tomod(J, 1);
	if (!m->hasmem) { js_pushnull(J); return; }
	js_newarray(J);
	js_pushnumber(J, m->mmin); js_setindex(J, -2, 0);
	js_pushnumber(J, m->mmax == 0xffffffffu ? 65536 : m->mmax); js_setindex(J, -2, 1);
	js_pushboolean(J, m->hasmem == 2); js_setindex(J, -2, 2);
}

/* ---- values to and from JavaScript */

static void pushval(js_State *J, int t, WVal v)
{
	switch (t) {
	case T_I32: js_pushnumber(J, v.i32); break;
	case T_I64: js_pushnumber(J, (double)v.i64); break;
	case T_F32: js_pushnumber(J, v.f32); break;
	case T_F64: js_pushnumber(J, v.f64); break;
	default: if (v.i32 < 0) js_pushnull(J); else js_pushnumber(J, v.i32); break;
	}
}

static double wtrunc(double x);

/* a whole double below 2^63 in magnitude as i64 (without libgcc's __fixdfdi: the phone has none) */
static i64 d2i64(double x)
{
	int neg = x < 0;
	double hi, lo;
	u64 r;
	if (neg) x = -x;
	hi = floor(x / 4294967296.0);
	lo = x - hi * 4294967296.0;
	r = ((u64)(unsigned int)hi << 32) | (u64)(unsigned int)lo;
	return neg ? -(i64)r : (i64)r;
}

static i64 dtoi64(double d)
{
	if (!(d == d)) return 0;
	if (d >= 9223372036854775807.0) return (i64)(((u64)1 << 63) - 1);
	if (d <= -9223372036854775808.0) return (i64)((u64)1 << 63);
	return d2i64(wtrunc(d));
}

static WVal toval(js_State *J, int idx, int t)
{
	WVal v;
	v.i64 = 0;
	switch (t) {
	case T_I32: v.i32 = js_toint32(J, idx); break;
	case T_I64: v.i64 = dtoi64(js_tonumber(J, idx)); break;
	case T_F32: v.f32 = (float)js_tonumber(J, idx); break;
	case T_F64: v.f64 = js_tonumber(J, idx); break;
	default: v.i32 = js_isnull(J, idx) || js_isundefined(J, idx) ? -1 : js_toint32(J, idx); break;
	}
	return v;
}

/* ---- instantiation */

static WVal evalconst(js_State *J, WInst *in, const unsigned char *p)
{
	WVal st[8];
	int sp = 0;
	for (;;) {
		unsigned int op = *p++;
		if (op == 0x0b) break;
		if (sp >= 8) wbad(J, "constant expression too deep");
		switch (op) {
		case 0x41: st[sp++].i32 = lebs32(&p); break;
		case 0x42: st[sp++].i64 = lebs64(&p); break;
		case 0x43: st[sp].i64 = 0; st[sp++].f32 = jsB_rdf32(p); p += 4; break;
		case 0x44: st[sp++].f64 = jsB_rdf64(p); p += 8; break;
		case 0x23: { unsigned int g = lebu32(&p); if (g >= in->m->nglobals) wbad(J, "bad global"); st[sp++] = in->globals[g]; } break;
		case 0xd2: st[sp].i64 = 0; st[sp++].i32 = (int)lebu32(&p); break;
		case 0xd0: p++; st[sp].i64 = 0; st[sp++].i32 = -1; break;
		case 0x6a: sp--; st[sp - 1].i32 += st[sp].i32; break;
		case 0x6b: sp--; st[sp - 1].i32 -= st[sp].i32; break;
		case 0x6c: sp--; st[sp - 1].i32 *= st[sp].i32; break;
		case 0x7c: sp--; st[sp - 1].i64 += st[sp].i64; break;
		case 0x7d: sp--; st[sp - 1].i64 -= st[sp].i64; break;
		case 0x7e: sp--; st[sp - 1].i64 *= st[sp].i64; break;
		}
	}
	if (sp != 1) wbad(J, "bad constant expression");
	return st[0];
}

static void inst_free(js_State *J, void *p)
{
	WInst *in = p;
	js_free(J, in->globals);
	js_free(J, in->table);
	js_free(J, in->dropped);
	js_free(J, in);
}

/* %wasmInstantiate(module, imports, memory): the instance. imports: in the module's order (the
 * functions callable, the globals numbers); memory: an ArrayBuffer (the Memory's) or null */
static void W_instantiate(js_State *J)
{
	WMod *m = tomod(J, 1);
	WInst *in;
	unsigned int i, g = 0;
	in = zalloc(J, sizeof *in);
	in->m = m;
	in->J = J;
	if (js_try(J)) {
		inst_free(J, in);
		js_throw(J);
	}
	in->globals = zalloc(J, (m->nglobals + 1) * sizeof *in->globals);
	in->dropped = zalloc(J, m->ndatas + 1);
	for (i = 0; i < m->nimports; ++i)
		if (m->imports[i].kind == 3) {
			js_getindex(J, 2, i);
			in->globals[g] = toval(J, -1, m->imports[i].gtype);
			js_pop(J, 1);
			g++;
		}
	for (i = m->ngimports; i < m->nglobals; ++i)
		in->globals[i] = evalconst(J, in, m->globals[i].init);
	if (m->hasmem) {
		if (!js_isuserdata(J, 3, TAG_AB)) js_typeerror(J, "WebAssembly.Instance(): memory missing");
		in->mem = js_touserdata(J, 3, TAG_AB);
		in->mmax = m->mmax;
	}
	if (m->hastable) {
		in->ntable = m->tmin;
		in->table = js_malloc(J, (in->ntable + 1) * sizeof(int));
		for (i = 0; i < in->ntable; ++i) in->table[i] = -1;
	}
	js_endtry(J);
	js_pushundefined(J);
	js_newuserdata(J, TAG_INST, in, inst_free);
	js_copy(J, 1); js_defproperty(J, -2, "%module", JS_DONTENUM | JS_READONLY);
	js_copy(J, 2); js_defproperty(J, -2, "%imports", JS_DONTENUM | JS_READONLY);
	js_copy(J, 3); js_defproperty(J, -2, "%memory", JS_DONTENUM | JS_READONLY);
	/* the active segments: elements, then data (out of bounds: a trap, as linking fails) */
	for (i = 0; i < m->nelems; ++i) {
		WElem *el = &m->elems[i];
		unsigned int off, k;
		if (el->passive || !el->offset) continue;
		off = (unsigned int)evalconst(J, in, el->offset).i32;
		if (off + (double)el->n > in->ntable) wtrap(J, "table index out of bounds (element segment)");
		for (k = 0; k < el->n; ++k) in->table[off + k] = el->funcs[k] == 0xffffffffu ? -1 : (int)el->funcs[k];
	}
	for (i = 0; i < m->ndatas; ++i) {
		WData *d = &m->datas[i];
		unsigned int off;
		if (d->passive) continue;
		off = (unsigned int)evalconst(J, in, d->offset).i32;
		if (!in->mem || off + (double)d->n > in->mem->n) wtrap(J, "data segment does not fit");
		memcpy(in->mem->p + off, d->bytes, d->n);
		in->dropped[i] = 1;
	}
}

static WInst *toinst(js_State *J, int idx)
{
	if (!js_isuserdata(J, idx, TAG_INST)) js_typeerror(J, "not a WebAssembly.Instance");
	return js_touserdata(J, idx, TAG_INST);
}

/* ---- the blocks of a function: where each block / loop / if has its else and end */

static void skipimm(js_State *J, const unsigned char **pp, unsigned int op)
{
	const unsigned char *p = *pp;
	unsigned int n;
	switch (op) {
	case 0x02: case 0x03: case 0x04: lebs64(&p); break;	/* block type: s33 */
	case 0x0c: case 0x0d: case 0x10: case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26:
	case 0xd2: lebu32(&p); break;
	case 0x0e: n = lebu32(&p); while (n-- + 1) lebu32(&p); break;
	case 0x11: lebu32(&p); lebu32(&p); break;
	case 0x1c: n = lebu32(&p); p += n; break;
	case 0x3f: case 0x40: p++; break;
	case 0x41: lebs32(&p); break;
	case 0x42: lebs64(&p); break;
	case 0x43: p += 4; break;
	case 0x44: p += 8; break;
	case 0xd0: p++; break;
	case 0xfc:
		n = lebu32(&p);
		switch (n) {
		case 8: lebu32(&p); p++; break;	/* memory.init */
		case 9: lebu32(&p); break;	/* data.drop */
		case 10: p += 2; break;	/* memory.copy */
		case 11: p++; break;	/* memory.fill */
		case 12: case 14: lebu32(&p); lebu32(&p); break;	/* table.init, table.copy */
		case 13: case 15: case 16: case 17: lebu32(&p); break;
		}
		break;
	default:
		if (op >= 0x28 && op <= 0x3e) { lebu32(&p); lebu32(&p); }
		else if (op > 0xd2 && op != 0xfc) js_error(J, "WebAssembly: unsupported instruction 0x%02x", op);
		break;
	}
	*pp = p;
}

static void matchblocks(js_State *J, WFunc *f)
{
	const unsigned char *p = f->code, *base = f->code;
	unsigned int cap = 16, n = 0, depth = 0, *open;
	WCtl *c = js_malloc(J, cap * sizeof *c);
	open = js_malloc(J, 1024 * sizeof *open);
	while (p < f->end) {
		const unsigned char *at = p;
		unsigned int op = *p++;
		if (op == 0x02 || op == 0x03 || op == 0x04) {
			if (n == cap) { cap *= 2; c = js_realloc(J, c, cap * sizeof *c); }
			if (depth >= 1024) js_error(J, "WebAssembly: blocks nested too deep");
			c[n].pos = (unsigned int)(at - base); c[n].elsepos = 0; c[n].end = 0;
			open[depth++] = n++;
		} else if (op == 0x05) {
			if (depth) c[open[depth - 1]].elsepos = (unsigned int)(p - base);
		} else if (op == 0x0b) {
			if (depth) c[open[--depth]].end = (unsigned int)(p - base);
		}
		skipimm(J, &p, op);
	}
	js_free(J, open);
	f->ctl = c;
	f->nctl = (int)n;
}

static WCtl *findctl(WFunc *f, unsigned int pos)
{
	int lo = 0, hi = f->nctl - 1;
	while (lo <= hi) {
		int mid = (lo + hi) >> 1;
		if (f->ctl[mid].pos == pos) return &f->ctl[mid];
		if (f->ctl[mid].pos < pos) lo = mid + 1; else hi = mid - 1;
	}
	return NULL;
}

/* ---- the interpreter */

typedef struct { const unsigned char *cont; unsigned int sp; unsigned short arity; unsigned char loop; } WLabel;
typedef struct { unsigned int fi; const unsigned char *pc; unsigned int locals, labels, sp0; } WFrame;

#define WSTACK 16384
#define WLABELS 4096
#define WFRAMES 1024

typedef struct { WVal *st; WLabel *lb; WFrame *fr; } WRun;

static void blocktype(WMod *m, const unsigned char **pp, int *np, int *nr)
{
	const unsigned char *p = *pp;
	if (*p == 0x40) { *np = 0; *nr = 0; p++; }
	else if (*p == T_I32 || *p == T_I64 || *p == T_F32 || *p == T_F64 || *p == T_FUNCREF || *p == T_EXTERNREF) { *np = 0; *nr = 1; p++; }
	else { i64 t = lebs64(&p); *np = m->types[(unsigned int)t].np; *nr = m->types[(unsigned int)t].nr; }
	*pp = p;
}

static double nearest(double x)
{
	double f;
	if (!(x == x) || x == 1 / 0.0 || x == -1 / 0.0) return x;
	f = floor(x);
	if (x - f > 0.5 || (x - f == 0.5 && fmod(f, 2.0) != 0)) f += 1;
	if (f == 0 && x < 0) return -0.0;
	return f;
}

static double wtrunc(double x) { return x < 0 ? -floor(-x) : floor(x); }

static double fmin_w(double a, double b)
{
	if (a != a || b != b) return 0.0 / 0.0;
	if (a == 0 && b == 0) return (1 / a < 0 || 1 / b < 0) ? -0.0 : 0.0;
	return a < b ? a : b;
}

static double fmax_w(double a, double b)
{
	if (a != a || b != b) return 0.0 / 0.0;
	if (a == 0 && b == 0) return (1 / a > 0 || 1 / b > 0) ? 0.0 : -0.0;
	return a > b ? a : b;
}

static unsigned int f32bits(float f) { unsigned char b[4]; jsB_wrf32(b, f); return rd32(b); }
static float bitsf32(unsigned int u) { unsigned char b[4]; wr32(b, u); return jsB_rdf32(b); }
static u64 f64bits(double d) { unsigned char b[8]; jsB_wrf64(b, d); return (u64)(b[0] | (b[1] << 8) | (b[2] << 16) | ((unsigned int)b[3] << 24)) | ((u64)(b[4] | (b[5] << 8) | (b[6] << 16) | ((unsigned int)b[7] << 24)) << 32); }
static double bitsf64(u64 u) { unsigned char b[8]; int i; for (i = 0; i < 8; ++i) b[i] = (unsigned char)(u >> (8 * i)); return jsB_rdf64(b); }

static int clz32(unsigned int x) { int n = 0; if (!x) return 32; while (!(x & 0x80000000u)) { x <<= 1; n++; } return n; }
static int ctz32(unsigned int x) { int n = 0; if (!x) return 32; while (!(x & 1)) { x >>= 1; n++; } return n; }
static int pop32(unsigned int x) { int n = 0; while (x) { n += x & 1; x >>= 1; } return n; }
static int clz64(u64 x) { return (unsigned int)(x >> 32) ? clz32((unsigned int)(x >> 32)) : 32 + clz32((unsigned int)x); }
static int ctz64(u64 x) { return (unsigned int)x ? ctz32((unsigned int)x) : 32 + ctz32((unsigned int)(x >> 32)); }

static int trunc_i32(js_State *J, double x, int sat, int uns)
{
	if (x != x) { if (sat) return 0; wtrap(J, "float unrepresentable in integer range"); }
	x = wtrunc(x);
	if (uns) {
		if (x < 0 || x >= 4294967296.0) { if (!sat) wtrap(J, "float unrepresentable in integer range"); return x < 0 ? 0 : (int)0xffffffffu; }
		return (int)(unsigned int)x;
	}
	if (x < -2147483648.0 || x >= 2147483648.0) { if (!sat) wtrap(J, "float unrepresentable in integer range"); return x < 0 ? (int)0x80000000u : 0x7fffffff; }
	return (int)x;
}

static i64 trunc_i64(js_State *J, double x, int sat, int uns)
{
	if (x != x) { if (sat) return 0; wtrap(J, "float unrepresentable in integer range"); }
	x = wtrunc(x);
	if (uns) {
		if (x < 0 || x >= 18446744073709551616.0) { if (!sat) wtrap(J, "float unrepresentable in integer range"); return x < 0 ? 0 : (i64)~(u64)0; }
		if (x >= 9223372036854775808.0) return (i64)((u64)d2i64(x - 9223372036854775808.0) + ((u64)1 << 63));
		return d2i64(x);
	}
	if (x < -9223372036854775808.0 || x >= 9223372036854775808.0) { if (!sat) wtrap(J, "float unrepresentable in integer range"); return x < 0 ? (i64)((u64)1 << 63) : (i64)(((u64)1 << 63) - 1); }
	return d2i64(x);
}

static double u64tod(u64 u) { return (double)(unsigned int)(u >> 32) * 4294967296.0 + (double)(unsigned int)u; }

/* a call to an imported (JavaScript) function: its arguments at st[sp - np .. sp) */
static void callhost(js_State *J, WInst *in, unsigned int fi, WVal *args, WVal *res)
{
	WType *t = &in->m->types[in->m->funcs[fi].type];
	unsigned int i, k = 0, imp = 0;
	int j;
	for (i = 0; i < in->m->nimports; ++i)
		if (in->m->imports[i].kind == 0 && k++ == fi) { imp = i; break; }
	js_getregistry(J, "%wasmCurrent");
	js_getproperty(J, -1, "%imports");
	js_getindex(J, -1, imp);
	js_rot3pop2(J);
	js_pushundefined(J);
	for (j = 0; j < t->np; ++j) pushval(J, t->p[j], args[j]);
	js_call(J, t->np);
	if (t->nr == 1) res[0] = toval(J, -1, t->r[0]);
	else if (t->nr > 1) {	/* (multiple results: an array) */
		for (j = 0; j < t->nr; ++j) { js_getindex(J, -1, j); res[j] = toval(J, -1, t->r[j]); js_pop(J, 1); }
	}
	js_pop(J, 1);
}

#define MEMCHK(EA, SZ) if ((EA) + (u64)(SZ) > (u64)mem->n) wtrap(J, "memory access out of bounds")
#define WPOP() (st[--sp])
#define WTOP() (st[sp - 1])
#define PUSHI32(v) do { st[sp].i64 = 0; st[sp++].i32 = (v); } while (0)

static unsigned int rd32le(const unsigned char *b) { return (unsigned int)b[0] | ((unsigned int)b[1] << 8) | ((unsigned int)b[2] << 16) | ((unsigned int)b[3] << 24); }
static void wr32le(unsigned char *b, unsigned int v) { b[0] = (unsigned char)v; b[1] = (unsigned char)(v >> 8); b[2] = (unsigned char)(v >> 16); b[3] = (unsigned char)(v >> 24); }

/* runs function fi of the instance at the top of J's registry %wasmCurrent with args; results to res */
static void wrun(js_State *J, WInst *in, unsigned int fi, WVal *args, WVal *res)
{
	WMod *m = in->m;
	WRun *R;
	WVal *st;
	WLabel *lb;
	WFrame *fr;
	unsigned int sp = 0, nl = 0, nf = 0, budget = 0, d = 0;
	const unsigned char *pc, *cbase;
	WFunc *f;
	BwAB *mem = in->mem;
	int i;

	if (fi < m->nfimports) { callhost(J, in, fi, args, res); return; }
	R = js_malloc(J, sizeof *R);
	R->st = NULL; R->lb = NULL; R->fr = NULL;
	if (js_try(J)) {
		js_free(J, R->st); js_free(J, R->lb); js_free(J, R->fr); js_free(J, R);
		js_throw(J);
	}
	st = R->st = js_malloc(J, WSTACK * sizeof *st);
	lb = R->lb = js_malloc(J, WLABELS * sizeof *lb);
	fr = R->fr = js_malloc(J, WFRAMES * sizeof *fr);

	/* the first frame: the arguments as its first locals */
#define ENTER(FI) do { \
		WType *t_ = &m->types[m->funcs[FI].type]; \
		f = &m->funcs[FI]; \
		if (!f->ctl) matchblocks(J, f); \
		if (nf >= WFRAMES) wtrap(J, "call stack exhausted"); \
		if (sp + f->nlocals + 64 > WSTACK) wtrap(J, "call stack exhausted"); \
		fr[nf].fi = FI; fr[nf].locals = sp - t_->np; fr[nf].labels = nl; \
		for (i = 0; i < f->nlocals; ++i) st[sp++].i64 = 0; \
		fr[nf].sp0 = sp; \
		if (nl >= WLABELS) wtrap(J, "call stack exhausted"); \
		lb[nl].cont = f->end; lb[nl].sp = sp; lb[nl].arity = (unsigned short)t_->nr; lb[nl].loop = 0; nl++; \
		pc = f->code; cbase = f->code; nf++; \
	} while (0)

	{
		WType *t0 = &m->types[m->funcs[fi].type];
		for (i = 0; i < t0->np; ++i) st[sp++] = args[i];
	}
	ENTER(fi);

	for (;;) {
		unsigned int op;
		if (++budget >= 4096) {	/* (the script's instruction budget: a wasm instruction counts 1/40) */
			budget = 0;
			if (J->runlimit > 0) {
				if (J->runlimit <= 100) js_runlimit(J);
				J->runlimit -= 100;
			}
		}
		if (pc >= f->end) op = 0x0b; else op = *pc++;
		switch (op) {
		case 0x00: wtrap(J, "unreachable");
		case 0x01: break;
		case 0x02: case 0x03: {	/* block, loop */
			const unsigned char *at = pc - 1;
			WCtl *c = findctl(f, (unsigned int)(at - cbase));
			int np, nr;
			blocktype(m, &pc, &np, &nr);
			if (!c) wtrap(J, "bad block");
			if (nl >= WLABELS) wtrap(J, "blocks nested too deep");
			lb[nl].sp = sp - np;
			if (op == 0x03) { lb[nl].cont = at; lb[nl].arity = (unsigned short)np; lb[nl].loop = 1; }
			else { lb[nl].cont = cbase + c->end; lb[nl].arity = (unsigned short)nr; lb[nl].loop = 0; }
			nl++;
			break;
		}
		case 0x04: {	/* if */
			const unsigned char *at = pc - 1;
			WCtl *c = findctl(f, (unsigned int)(at - cbase));
			int np, nr, cond;
			blocktype(m, &pc, &np, &nr);
			if (!c) wtrap(J, "bad block");
			cond = WPOP().i32;
			if (nl >= WLABELS) wtrap(J, "blocks nested too deep");
			lb[nl].sp = sp - np; lb[nl].cont = cbase + c->end; lb[nl].arity = (unsigned short)nr; lb[nl].loop = 0;
			if (cond) nl++;
			else if (c->elsepos) { nl++; pc = cbase + c->elsepos; }
			else pc = cbase + c->end;
			break;
		}
		case 0x05:	/* else: the then part done, past the end */
			pc = lb[nl - 1].cont;
			nl--;
			break;
		case 0x0b:	/* end */
			if (nl - 1 == fr[nf - 1].labels) {	/* the function's: return */
				goto doreturn;
			}
			nl--;
			break;
		case 0x0c: d = lebu32(&pc); goto branch;	/* br */
		case 0x0d: d = lebu32(&pc); if (!WPOP().i32) break; goto branch;	/* br_if */
		case 0x0e: {	/* br_table */
			unsigned int n = lebu32(&pc), idx = (unsigned int)WPOP().i32, k;
			d = 0;
			for (k = 0; k <= n; ++k) {
				unsigned int x = lebu32(&pc);
				if (k == idx || k == n) { d = x; break; }
			}
			goto branch;
		}
		case 0x0f: goto doreturn;
		case 0x10: case 0x11: {	/* call, call_indirect */
			unsigned int callee;
			WType *t;
			if (op == 0x10) callee = lebu32(&pc);
			else {
				unsigned int ty = lebu32(&pc), e;
				WType *a, *b;
				lebu32(&pc);
				e = (unsigned int)WPOP().i32;
				if (e >= in->ntable) wtrap(J, "table index is out of bounds");
				if (in->table[e] < 0) wtrap(J, "null function or function signature mismatch");
				callee = (unsigned int)in->table[e];
				a = &m->types[ty]; b = &m->types[m->funcs[callee].type];
				if (a->np != b->np || a->nr != b->nr || memcmp(a->p, b->p, a->np) || memcmp(a->r, b->r, a->nr))
					wtrap(J, "null function or function signature mismatch");
			}
			if (callee >= m->nfuncs) wtrap(J, "bad function index");
			t = &m->types[m->funcs[callee].type];
			if (callee < m->nfimports) {
				WVal r[8];
				unsigned int k;
				callhost(J, in, callee, st + sp - t->np, r);
				mem = in->mem;
				sp -= t->np;
				for (k = 0; k < (unsigned int)t->nr; ++k) st[sp++] = r[k];
				break;
			}
			fr[nf - 1].pc = pc;
			ENTER(callee);
			break;
		}
		case 0x1a: sp--; break;
		case 0x1b: case 0x1c: {	/* select */
			int c;
			if (op == 0x1c) { unsigned int n = lebu32(&pc); pc += n; }
			c = WPOP().i32;
			sp--;
			if (!c) st[sp - 1] = st[sp];
			break;
		}
		case 0x20: { unsigned int x = lebu32(&pc); st[sp++] = st[fr[nf - 1].locals + x]; break; }
		case 0x21: { unsigned int x = lebu32(&pc); st[fr[nf - 1].locals + x] = st[--sp]; break; }
		case 0x22: { unsigned int x = lebu32(&pc); st[fr[nf - 1].locals + x] = st[sp - 1]; break; }
		case 0x23: { unsigned int x = lebu32(&pc); st[sp++] = in->globals[x]; break; }
		case 0x24: { unsigned int x = lebu32(&pc); in->globals[x] = st[--sp]; break; }

		/* memory */
		case 0x28: case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x2f:
		case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: {
			unsigned int off, ea;
			const unsigned char *b;
			WVal v;
			lebu32(&pc);
			off = lebu32(&pc);
			if (!mem) wtrap(J, "no memory");
			ea = (unsigned int)st[sp - 1].i32;
			{
				static const unsigned char sz[14] = { 4, 8, 4, 8, 1, 1, 2, 2, 1, 1, 2, 2, 4, 4 };
				MEMCHK((u64)ea + off, sz[op - 0x28]);
			}
			b = mem->p + ea + off;
			v.i64 = 0;
			switch (op) {
			case 0x28: v.i32 = (int)rd32le(b); break;
			case 0x29: v.i64 = (i64)((u64)rd32le(b) | ((u64)rd32le(b + 4) << 32)); break;
			case 0x2a: v.f32 = jsB_rdf32(b); break;
			case 0x2b: v.f64 = jsB_rdf64(b); break;
			case 0x2c: v.i32 = (signed char)b[0]; break;
			case 0x2d: v.i32 = b[0]; break;
			case 0x2e: v.i32 = (short)(b[0] | (b[1] << 8)); break;
			case 0x2f: v.i32 = b[0] | (b[1] << 8); break;
			case 0x30: v.i64 = (signed char)b[0]; break;
			case 0x31: v.i64 = b[0]; break;
			case 0x32: v.i64 = (short)(b[0] | (b[1] << 8)); break;
			case 0x33: v.i64 = b[0] | (b[1] << 8); break;
			case 0x34: v.i64 = (int)rd32le(b); break;
			case 0x35: v.i64 = rd32le(b); break;
			}
			st[sp - 1] = v;
			break;
		}
		case 0x36: case 0x37: case 0x38: case 0x39: case 0x3a: case 0x3b: case 0x3c: case 0x3d: case 0x3e: {
			unsigned int off, ea;
			unsigned char *b;
			WVal v;
			static const unsigned char sz[9] = { 4, 8, 4, 8, 1, 2, 1, 2, 4 };
			lebu32(&pc);
			off = lebu32(&pc);
			v = WPOP();
			ea = (unsigned int)WPOP().i32;
			if (!mem) wtrap(J, "no memory");
			MEMCHK((u64)ea + off, sz[op - 0x36]);
			b = mem->p + ea + off;
			switch (op) {
			case 0x36: wr32le(b, (unsigned int)v.i32); break;
			case 0x37: wr32le(b, (unsigned int)(u64)v.i64); wr32le(b + 4, (unsigned int)((u64)v.i64 >> 32)); break;
			case 0x38: jsB_wrf32(b, v.f32); break;
			case 0x39: jsB_wrf64(b, v.f64); break;
			case 0x3a: b[0] = (unsigned char)v.i32; break;
			case 0x3b: b[0] = (unsigned char)v.i32; b[1] = (unsigned char)(v.i32 >> 8); break;
			case 0x3c: b[0] = (unsigned char)v.i64; break;
			case 0x3d: b[0] = (unsigned char)v.i64; b[1] = (unsigned char)(v.i64 >> 8); break;
			case 0x3e: wr32le(b, (unsigned int)(u64)v.i64); break;
			}
			break;
		}
		case 0x3f: pc++; PUSHI32(mem ? (int)(mem->n / 65536) : 0); break;
		case 0x40: {	/* memory.grow */
			unsigned int by = (unsigned int)st[sp - 1].i32, old;
			pc++;
			if (!mem) { st[sp - 1].i32 = -1; break; }
			old = mem->n / 65536;
			if (by == 0) { st[sp - 1].i32 = (int)old; break; }
			if ((u64)old + by > (in->mmax == 0xffffffffu ? 32768u : in->mmax) || (u64)old + by > 32768u) { st[sp - 1].i32 = -1; break; }
			js_getregistry(J, "%wasmCurrent");
			js_getproperty(J, -1, "%memory");
			if (!jsB_bufferresize(J, -1, (old + by) * 65536)) st[sp - 1].i32 = -1;
			else st[sp - 1].i32 = (int)old;
			js_pop(J, 2);
			break;
		}

		case 0x41: st[sp].i64 = 0; st[sp++].i32 = lebs32(&pc); break;
		case 0x42: st[sp++].i64 = lebs64(&pc); break;
		case 0x43: st[sp].i64 = 0; st[sp++].f32 = jsB_rdf32(pc); pc += 4; break;
		case 0x44: st[sp++].f64 = jsB_rdf64(pc); pc += 8; break;

		/* i32 comparisons */
		case 0x45: WTOP().i32 = !WTOP().i32; break;
#define CMP32(OP, T) sp--; { int r_ = (T)st[sp - 1].i32 OP (T)st[sp].i32; st[sp - 1].i64 = 0; st[sp - 1].i32 = r_; } break;
		case 0x46: CMP32(==, int)
		case 0x47: CMP32(!=, int)
		case 0x48: CMP32(<, int)
		case 0x49: CMP32(<, unsigned int)
		case 0x4a: CMP32(>, int)
		case 0x4b: CMP32(>, unsigned int)
		case 0x4c: CMP32(<=, int)
		case 0x4d: CMP32(<=, unsigned int)
		case 0x4e: CMP32(>=, int)
		case 0x4f: CMP32(>=, unsigned int)
		case 0x50: { int r_ = WTOP().i64 == 0; WTOP().i64 = 0; WTOP().i32 = r_; break; }
#define CMP64(OP, T) sp--; { int r_ = (T)st[sp - 1].i64 OP (T)st[sp].i64; st[sp - 1].i64 = 0; st[sp - 1].i32 = r_; } break;
		case 0x51: CMP64(==, i64)
		case 0x52: CMP64(!=, i64)
		case 0x53: CMP64(<, i64)
		case 0x54: CMP64(<, u64)
		case 0x55: CMP64(>, i64)
		case 0x56: CMP64(>, u64)
		case 0x57: CMP64(<=, i64)
		case 0x58: CMP64(<=, u64)
		case 0x59: CMP64(>=, i64)
		case 0x5a: CMP64(>=, u64)
#define CMPF(F, OP) sp--; { int r_ = st[sp - 1].F OP st[sp].F; st[sp - 1].i64 = 0; st[sp - 1].i32 = r_; } break;
		case 0x5b: CMPF(f32, ==)
		case 0x5c: CMPF(f32, !=)
		case 0x5d: CMPF(f32, <)
		case 0x5e: CMPF(f32, >)
		case 0x5f: CMPF(f32, <=)
		case 0x60: CMPF(f32, >=)
		case 0x61: CMPF(f64, ==)
		case 0x62: CMPF(f64, !=)
		case 0x63: CMPF(f64, <)
		case 0x64: CMPF(f64, >)
		case 0x65: CMPF(f64, <=)
		case 0x66: CMPF(f64, >=)

		/* i32 arithmetic */
		case 0x67: WTOP().i32 = clz32((unsigned int)WTOP().i32); break;
		case 0x68: WTOP().i32 = ctz32((unsigned int)WTOP().i32); break;
		case 0x69: WTOP().i32 = pop32((unsigned int)WTOP().i32); break;
#define BIN32(EXPR) sp--; { unsigned int a = (unsigned int)st[sp - 1].i32, b = (unsigned int)st[sp].i32; st[sp - 1].i32 = (int)(EXPR); } break;
		case 0x6a: BIN32(a + b)
		case 0x6b: BIN32(a - b)
		case 0x6c: BIN32(a * b)
		case 0x6d: sp--; { int a = st[sp - 1].i32, b = st[sp].i32;
			if (!b) wtrap(J, "divide by zero");
			if (a == (int)0x80000000u && b == -1) wtrap(J, "divide result unrepresentable");
			st[sp - 1].i32 = a / b; } break;
		case 0x6e: sp--; { unsigned int a = (unsigned int)st[sp - 1].i32, b = (unsigned int)st[sp].i32;
			if (!b) wtrap(J, "divide by zero");
			st[sp - 1].i32 = (int)(a / b); } break;
		case 0x6f: sp--; { int a = st[sp - 1].i32, b = st[sp].i32;
			if (!b) wtrap(J, "divide by zero");
			st[sp - 1].i32 = b == -1 ? 0 : a % b; } break;
		case 0x70: sp--; { unsigned int a = (unsigned int)st[sp - 1].i32, b = (unsigned int)st[sp].i32;
			if (!b) wtrap(J, "divide by zero");
			st[sp - 1].i32 = (int)(a % b); } break;
		case 0x71: BIN32(a & b)
		case 0x72: BIN32(a | b)
		case 0x73: BIN32(a ^ b)
		case 0x74: BIN32(a << (b & 31))
		case 0x75: sp--; st[sp - 1].i32 = st[sp - 1].i32 >> (st[sp].i32 & 31); break;
		case 0x76: BIN32(a >> (b & 31))
		case 0x77: BIN32((a << (b & 31)) | (a >> ((32 - (b & 31)) & 31)))
		case 0x78: BIN32((a >> (b & 31)) | (a << ((32 - (b & 31)) & 31)))

		/* i64 arithmetic */
		case 0x79: WTOP().i64 = clz64((u64)WTOP().i64); break;
		case 0x7a: WTOP().i64 = ctz64((u64)WTOP().i64); break;
		case 0x7b: WTOP().i64 = pop32((unsigned int)(u64)WTOP().i64) + pop32((unsigned int)((u64)WTOP().i64 >> 32)); break;
#define BIN64(EXPR) sp--; { u64 a = (u64)st[sp - 1].i64, b = (u64)st[sp].i64; st[sp - 1].i64 = (i64)(EXPR); } break;
		case 0x7c: BIN64(a + b)
		case 0x7d: BIN64(a - b)
		case 0x7e: BIN64(a * b)
		case 0x7f: sp--; { i64 a = st[sp - 1].i64, b = st[sp].i64;
			if (!b) wtrap(J, "divide by zero");
			if ((u64)a == ((u64)1 << 63) && b == -1) wtrap(J, "divide result unrepresentable");
			st[sp - 1].i64 = a / b; } break;
		case 0x80: sp--; { u64 a = (u64)st[sp - 1].i64, b = (u64)st[sp].i64;
			if (!b) wtrap(J, "divide by zero");
			st[sp - 1].i64 = (i64)(a / b); } break;
		case 0x81: sp--; { i64 a = st[sp - 1].i64, b = st[sp].i64;
			if (!b) wtrap(J, "divide by zero");
			st[sp - 1].i64 = b == -1 ? 0 : a % b; } break;
		case 0x82: sp--; { u64 a = (u64)st[sp - 1].i64, b = (u64)st[sp].i64;
			if (!b) wtrap(J, "divide by zero");
			st[sp - 1].i64 = (i64)(a % b); } break;
		case 0x83: BIN64(a & b)
		case 0x84: BIN64(a | b)
		case 0x85: BIN64(a ^ b)
		case 0x86: BIN64(a << (b & 63))
		case 0x87: sp--; { i64 a = st[sp - 1].i64; unsigned int s_ = (unsigned int)st[sp].i64 & 63;
			st[sp - 1].i64 = a < 0 ? (i64)~(~(u64)a >> s_) : (i64)((u64)a >> s_); } break;
		case 0x88: BIN64(a >> (b & 63))
		case 0x89: BIN64((a << (b & 63)) | (a >> ((64 - (b & 63)) & 63)))
		case 0x8a: BIN64((a >> (b & 63)) | (a << ((64 - (b & 63)) & 63)))

		/* f32 */
		case 0x8b: WTOP().f32 = bitsf32(f32bits(WTOP().f32) & 0x7fffffffu); break;
		case 0x8c: WTOP().f32 = bitsf32(f32bits(WTOP().f32) ^ 0x80000000u); break;
		case 0x8d: WTOP().f32 = (float)ceil(WTOP().f32); break;
		case 0x8e: WTOP().f32 = (float)floor(WTOP().f32); break;
		case 0x8f: WTOP().f32 = (float)wtrunc(WTOP().f32); break;
		case 0x90: WTOP().f32 = (float)nearest(WTOP().f32); break;
		case 0x91: WTOP().f32 = (float)sqrt(WTOP().f32); break;
#define BINF(F, EXPR) sp--; { double a = st[sp - 1].F, b = st[sp].F; st[sp - 1].F = (EXPR); } break;
		case 0x92: sp--; st[sp - 1].f32 = st[sp - 1].f32 + st[sp].f32; break;
		case 0x93: sp--; st[sp - 1].f32 = st[sp - 1].f32 - st[sp].f32; break;
		case 0x94: sp--; st[sp - 1].f32 = st[sp - 1].f32 * st[sp].f32; break;
		case 0x95: sp--; st[sp - 1].f32 = st[sp - 1].f32 / st[sp].f32; break;
		case 0x96: BINF(f32, (float)fmin_w(a, b))
		case 0x97: BINF(f32, (float)fmax_w(a, b))
		case 0x98: sp--; st[sp - 1].f32 = bitsf32((f32bits(st[sp - 1].f32) & 0x7fffffffu) | (f32bits(st[sp].f32) & 0x80000000u)); break;
		/* f64 */
		case 0x99: WTOP().f64 = bitsf64(f64bits(WTOP().f64) & ~((u64)1 << 63)); break;
		case 0x9a: WTOP().f64 = bitsf64(f64bits(WTOP().f64) ^ ((u64)1 << 63)); break;
		case 0x9b: WTOP().f64 = ceil(WTOP().f64); break;
		case 0x9c: WTOP().f64 = floor(WTOP().f64); break;
		case 0x9d: WTOP().f64 = wtrunc(WTOP().f64); break;
		case 0x9e: WTOP().f64 = nearest(WTOP().f64); break;
		case 0x9f: WTOP().f64 = sqrt(WTOP().f64); break;
		case 0xa0: BINF(f64, a + b)
		case 0xa1: BINF(f64, a - b)
		case 0xa2: BINF(f64, a * b)
		case 0xa3: BINF(f64, a / b)
		case 0xa4: BINF(f64, fmin_w(a, b))
		case 0xa5: BINF(f64, fmax_w(a, b))
		case 0xa6: sp--; st[sp - 1].f64 = bitsf64((f64bits(st[sp - 1].f64) & ~((u64)1 << 63)) | (f64bits(st[sp].f64) & ((u64)1 << 63))); break;

		/* conversions */
		case 0xa7: { int v = (int)(unsigned int)(u64)WTOP().i64; WTOP().i64 = 0; WTOP().i32 = v; break; }
		case 0xa8: { int v = trunc_i32(J, WTOP().f32, 0, 0); WTOP().i64 = 0; WTOP().i32 = v; break; }
		case 0xa9: { int v = trunc_i32(J, WTOP().f32, 0, 1); WTOP().i64 = 0; WTOP().i32 = v; break; }
		case 0xaa: { int v = trunc_i32(J, WTOP().f64, 0, 0); WTOP().i64 = 0; WTOP().i32 = v; break; }
		case 0xab: { int v = trunc_i32(J, WTOP().f64, 0, 1); WTOP().i64 = 0; WTOP().i32 = v; break; }
		case 0xac: WTOP().i64 = WTOP().i32; break;
		case 0xad: WTOP().i64 = (i64)(unsigned int)WTOP().i32; break;
		case 0xae: WTOP().i64 = trunc_i64(J, WTOP().f32, 0, 0); break;
		case 0xaf: WTOP().i64 = trunc_i64(J, WTOP().f32, 0, 1); break;
		case 0xb0: WTOP().i64 = trunc_i64(J, WTOP().f64, 0, 0); break;
		case 0xb1: WTOP().i64 = trunc_i64(J, WTOP().f64, 0, 1); break;
		case 0xb2: { float v = (float)WTOP().i32; WTOP().i64 = 0; WTOP().f32 = v; break; }
		case 0xb3: { float v = (float)(double)(unsigned int)WTOP().i32; WTOP().i64 = 0; WTOP().f32 = v; break; }
		case 0xb4: { float v = (float)(double)WTOP().i64; WTOP().i64 = 0; WTOP().f32 = v; break; }
		case 0xb5: { float v = (float)u64tod((u64)WTOP().i64); WTOP().i64 = 0; WTOP().f32 = v; break; }
		case 0xb6: { float v = (float)WTOP().f64; WTOP().i64 = 0; WTOP().f32 = v; break; }
		case 0xb7: WTOP().f64 = (double)WTOP().i32; break;
		case 0xb8: WTOP().f64 = (double)(unsigned int)WTOP().i32; break;
		case 0xb9: WTOP().f64 = (double)WTOP().i64; break;
		case 0xba: WTOP().f64 = u64tod((u64)WTOP().i64); break;
		case 0xbb: WTOP().f64 = (double)WTOP().f32; break;
		case 0xbc: { unsigned int v = f32bits(WTOP().f32); WTOP().i64 = 0; WTOP().i32 = (int)v; break; }
		case 0xbd: WTOP().i64 = (i64)f64bits(WTOP().f64); break;
		case 0xbe: { float v = bitsf32((unsigned int)WTOP().i32); WTOP().i64 = 0; WTOP().f32 = v; break; }
		case 0xbf: WTOP().f64 = bitsf64((u64)WTOP().i64); break;
		/* sign extension */
		case 0xc0: WTOP().i32 = (signed char)WTOP().i32; break;
		case 0xc1: WTOP().i32 = (short)WTOP().i32; break;
		case 0xc2: WTOP().i64 = (signed char)WTOP().i64; break;
		case 0xc3: WTOP().i64 = (short)WTOP().i64; break;
		case 0xc4: WTOP().i64 = (int)WTOP().i64; break;
		/* references */
		case 0xd0: pc++; PUSHI32(-1); break;
		case 0xd1: WTOP().i32 = WTOP().i32 < 0; break;
		case 0xd2: PUSHI32((int)lebu32(&pc)); break;

		case 0xfc: {
			unsigned int sub = lebu32(&pc);
			switch (sub) {
			case 0: { int v = trunc_i32(J, WTOP().f32, 1, 0); WTOP().i64 = 0; WTOP().i32 = v; break; }
			case 1: { int v = trunc_i32(J, WTOP().f32, 1, 1); WTOP().i64 = 0; WTOP().i32 = v; break; }
			case 2: { int v = trunc_i32(J, WTOP().f64, 1, 0); WTOP().i64 = 0; WTOP().i32 = v; break; }
			case 3: { int v = trunc_i32(J, WTOP().f64, 1, 1); WTOP().i64 = 0; WTOP().i32 = v; break; }
			case 4: WTOP().i64 = trunc_i64(J, WTOP().f32, 1, 0); break;
			case 5: WTOP().i64 = trunc_i64(J, WTOP().f32, 1, 1); break;
			case 6: WTOP().i64 = trunc_i64(J, WTOP().f64, 1, 0); break;
			case 7: WTOP().i64 = trunc_i64(J, WTOP().f64, 1, 1); break;
			case 8: {	/* memory.init */
				unsigned int seg = lebu32(&pc), n, s, d;
				pc++;
				n = (unsigned int)WPOP().i32; s = (unsigned int)WPOP().i32; d = (unsigned int)WPOP().i32;
				if (seg >= m->ndatas) wtrap(J, "bad data segment");
				if (!mem || (u64)d + n > mem->n || (u64)s + n > (in->dropped[seg] ? 0 : m->datas[seg].n)) wtrap(J, "memory access out of bounds");
				memcpy(mem->p + d, m->datas[seg].bytes + s, n);
				break;
			}
			case 9: { unsigned int seg = lebu32(&pc); if (seg < m->ndatas) in->dropped[seg] = 1; break; }
			case 10: {	/* memory.copy */
				unsigned int n, s, d;
				pc += 2;
				n = (unsigned int)WPOP().i32; s = (unsigned int)WPOP().i32; d = (unsigned int)WPOP().i32;
				if (!mem || (u64)d + n > mem->n || (u64)s + n > mem->n) wtrap(J, "memory access out of bounds");
				memmove(mem->p + d, mem->p + s, n);
				break;
			}
			case 11: {	/* memory.fill */
				unsigned int n, v, d;
				pc++;
				n = (unsigned int)WPOP().i32; v = (unsigned int)WPOP().i32; d = (unsigned int)WPOP().i32;
				if (!mem || (u64)d + n > mem->n) wtrap(J, "memory access out of bounds");
				memset(mem->p + d, (int)(v & 255), n);
				break;
			}
			case 16: lebu32(&pc); PUSHI32((int)in->ntable); break;	/* table.size */
			default: js_error(J, "WebAssembly: unsupported instruction 0xfc %u", sub);
			}
			break;
		}
		case 0x25: {	/* table.get */
			unsigned int e;
			lebu32(&pc);
			e = (unsigned int)WTOP().i32;
			if (e >= in->ntable) wtrap(J, "table index out of bounds");
			WTOP().i32 = in->table[e];
			break;
		}
		case 0x26: {	/* table.set */
			unsigned int e;
			int v;
			lebu32(&pc);
			v = WPOP().i32; e = (unsigned int)WPOP().i32;
			if (e >= in->ntable) wtrap(J, "table index out of bounds");
			in->table[e] = v;
			break;
		}
		default:
			js_error(J, "WebAssembly: unsupported instruction 0x%02x", op);
		}
		continue;
	branch: {
			WLabel *L;
			unsigned int k;
			if (d >= nl - fr[nf - 1].labels) wtrap(J, "bad branch depth");
			L = &lb[nl - 1 - d];
			if (L == &lb[fr[nf - 1].labels]) goto doreturn;
			for (k = 0; k < L->arity; ++k) st[L->sp + k] = st[sp - L->arity + k];
			sp = L->sp + L->arity;
			pc = L->cont;
			nl = (unsigned int)(L - lb);	/* (a loop's label is pushed again by its loop instruction) */
			continue;
		}
	doreturn: {
			WType *t = &m->types[f->type];
			unsigned int k, base = fr[nf - 1].locals;
			for (k = 0; k < (unsigned int)t->nr; ++k) st[base + k] = st[sp - t->nr + k];
			sp = base + t->nr;
			nl = fr[nf - 1].labels;
			nf--;
			if (!nf) {
				for (k = 0; k < (unsigned int)t->nr; ++k) res[k] = st[k];
				break;
			}
			f = &m->funcs[fr[nf - 1].fi];
			cbase = f->code;
			pc = fr[nf - 1].pc;
			continue;
		}
	}

	js_endtry(J);
	js_free(J, R->st); js_free(J, R->lb); js_free(J, R->fr); js_free(J, R);
}

/* %wasmCall(instance, function index, args): the result (undefined, a number, or an array) */
static void W_call(js_State *J)
{
	WInst *in = toinst(J, 1);
	unsigned int fi = (unsigned int)js_tointeger(J, 2), n, i;
	WVal args[16], res[8];
	WType *t;
	if (fi >= in->m->nfuncs) js_rangeerror(J, "no such function");
	t = &in->m->types[in->m->funcs[fi].type];
	n = js_isobject(J, 3) ? js_getlength(J, 3) : 0;
	for (i = 0; i < (unsigned int)t->np; ++i) {
		if (i < n) { js_getindex(J, 3, i); args[i] = toval(J, -1, t->p[i]); js_pop(J, 1); }
		else args[i].i64 = 0;
	}
	/* the instance being run (for the host calls and memory.grow), saved around nested runs */
	js_getregistry(J, "%wasmCurrent");
	js_copy(J, 1);
	js_setregistry(J, "%wasmCurrent");
	if (js_try(J)) {	/* (the saved one back, under the error value) */
		js_rot2(J);
		js_setregistry(J, "%wasmCurrent");
		js_throw(J);
	}
	wrun(J, in, fi, args, res);
	js_endtry(J);
	js_setregistry(J, "%wasmCurrent");
	if (t->nr == 0) js_pushundefined(J);
	else if (t->nr == 1) pushval(J, t->r[0], res[0]);
	else {
		js_newarray(J);
		for (i = 0; i < (unsigned int)t->nr; ++i) { pushval(J, t->r[i], res[i]); js_setindex(J, -2, i); }
	}
}

/* %wasmGlobal(instance, index[, value]): its value (set first when value is given) */
static void W_global(js_State *J)
{
	WInst *in = toinst(J, 1);
	unsigned int g = (unsigned int)js_tointeger(J, 2);
	if (g >= in->m->nglobals) js_rangeerror(J, "no such global");
	if (!js_isundefined(J, 3)) in->globals[g] = toval(J, 3, in->m->globals[g].type);
	pushval(J, in->m->globals[g].type, in->globals[g]);
}

/* %wasmTable(instance, index[, function index]): the table's entry (-1: none), or its length */
static void W_table(js_State *J)
{
	WInst *in = toinst(J, 1);
	if (js_isundefined(J, 2)) { js_pushnumber(J, in->ntable); return; }
	{
		unsigned int e = (unsigned int)js_tointeger(J, 2);
		if (e >= in->ntable) js_rangeerror(J, "table index out of bounds");
		if (!js_isundefined(J, 3)) in->table[e] = js_isnumber(J, 3) ? js_toint32(J, 3) : -1;
		js_pushnumber(J, in->table[e]);
	}
}

/* %wasmStart(instance): runs the start function (if any) */
static void W_start(js_State *J)
{
	WInst *in = toinst(J, 1);
	if (in->m->start >= 0) {
		js_getglobal(J, "%wasmCall");
		js_pushundefined(J);
		js_copy(J, 1);
		js_pushnumber(J, in->m->start);
		js_newarray(J);
		js_call(J, 3);
		js_pop(J, 1);
	}
	js_pushundefined(J);
}

/* %wasmGrow(buffer, pages, max): a Memory's buffer grown in place; its old size in pages, or -1 */
static void W_grow(js_State *J)
{
	BwAB *ab = toab(J, 1);
	unsigned int by = (unsigned int)js_tointeger(J, 2), old = ab->n / 65536, mx = js_isundefined(J, 3) ? 32768 : (unsigned int)js_tointeger(J, 3);
	if (mx > 32768) mx = 32768;
	if ((u64)old + by > mx || !jsB_bufferresize(J, 1, (old + by) * 65536)) { js_pushnumber(J, -1); return; }
	js_pushnumber(J, old);
}

static void W_init(js_State *J)
{
	js_newcfunction(J, W_compile, "%wasmCompile", 1); js_defglobal(J, "%wasmCompile", JS_DONTENUM);
	js_newcfunction(J, W_imports, "%wasmImports", 1); js_defglobal(J, "%wasmImports", JS_DONTENUM);
	js_newcfunction(J, W_exports, "%wasmExports", 1); js_defglobal(J, "%wasmExports", JS_DONTENUM);
	js_newcfunction(J, W_meminfo, "%wasmMemoryInfo", 1); js_defglobal(J, "%wasmMemoryInfo", JS_DONTENUM);
	js_newcfunction(J, W_instantiate, "%wasmInstantiate", 3); js_defglobal(J, "%wasmInstantiate", JS_DONTENUM);
	js_newcfunction(J, W_call, "%wasmCall", 3); js_defglobal(J, "%wasmCall", JS_DONTENUM);
	js_newcfunction(J, W_global, "%wasmGlobal", 3); js_defglobal(J, "%wasmGlobal", JS_DONTENUM);
	js_newcfunction(J, W_table, "%wasmTable", 3); js_defglobal(J, "%wasmTable", JS_DONTENUM);
	js_newcfunction(J, W_start, "%wasmStart", 1); js_defglobal(J, "%wasmStart", JS_DONTENUM);
	js_newcfunction(J, W_grow, "%wasmGrow", 3); js_defglobal(J, "%wasmGrow", JS_DONTENUM);
	js_pushundefined(J);
}

/* %wasminit(): the functions above (es.js's WebAssembly block calls it when first named) */
void jsB_initwasm(js_State *J)
{
	js_newcfunction(J, W_init, "%wasminit", 0);
	js_defglobal(J, "%wasminit", JS_DONTENUM);
}
