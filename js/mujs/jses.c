/* Browser 9300: the engine's ES2015+ runtime: js/es.js (as jses.h) and the parts in C. */
typedef struct { const char *names, *src, *file; } BwLazyChunk;
#include "jses.h"

/* Browser 9300: lazy globals. Each name of a chunk is an accessor on the global object until a
 * script reads one (the getter compiles and runs the chunk, which defines them all; the
 * accessors still there are deleted first) or sets one (that name becomes a plain value). */
typedef struct { const char *names, *src, *file; char name[40]; } BwLazyName;

static void ES_lazyget(js_State *J);

static void lazy_free(js_State *J, void *data)
{
	js_free(J, data);
}

static int lazy_pending(js_State *J, const char *name)
{
	js_Property *p = jsV_getownproperty(J, J->G, name);
	return p && p->getter && p->getter->type == JS_CCFUNCTION && p->getter->u.c.function == ES_lazyget;
}

static void lazy_load(js_State *J, BwLazyName *n)
{
	const char *s = n->names, *e;
	char name[40];
	while (*s) {
		for (e = s; *e && *e != ' '; ++e)
			;
		if (e - s < (int)sizeof name) {
			memcpy(name, s, e - s);
			name[e - s] = 0;
			if (lazy_pending(J, name))
				jsV_delproperty(J, J->G, name);
		}
		s = *e ? e + 1 : e;
	}
	js_loadstring(J, n->file, n->src);
	js_pushglobal(J);
	js_call(J, 0);
	js_pop(J, 1);
}

static void ES_lazyget(js_State *J)
{
	BwLazyName *n = js_currentfunctiondata(J);
	char name[40];
	memcpy(name, n->name, sizeof name);	/* (n may be collected once the accessor is gone) */
	lazy_load(J, n);
	js_getglobal(J, name);
}

static void ES_lazyset(js_State *J)
{
	BwLazyName *n = js_currentfunctiondata(J);
	char name[40];
	memcpy(name, n->name, sizeof name);
	if (lazy_pending(J, name))
		jsV_delproperty(J, J->G, name);
	js_copy(J, 1);
	js_setglobal(J, name);
	js_pushundefined(J);
}

static void lazy_fun(js_State *J, js_CFunction f, const char *names, const char *src, const char *file, const char *name, int len)
{
	BwLazyName *n = js_malloc(J, sizeof *n);
	n->names = names;
	n->src = src;
	n->file = file;
	memcpy(n->name, name, len);
	n->name[len] = 0;
	js_newcfunctionx(J, f, n->name, f == ES_lazyset, n, lazy_free);
}

/* names: space separated; src and file stay (static text) */
void jsB_lazy(js_State *J, const char *names, const char *src, const char *file)
{
	const char *s = names, *e;
#ifdef BW_LAZY_OFF
	js_loadstring(J, file, src); js_pushglobal(J); js_call(J, 0); js_pop(J, 1); return;
#endif
	while (*s) {
		for (e = s; *e && *e != ' '; ++e)
			;
		if (e > s && e - s < 40) {
			char name[40];
			memcpy(name, s, e - s);
			name[e - s] = 0;
			js_pushglobal(J);
			lazy_fun(J, ES_lazyget, names, src, file, s, (int)(e - s));
			lazy_fun(J, ES_lazyset, names, src, file, s, (int)(e - s));
			js_defaccessor(J, -3, name, JS_DONTENUM);
			js_pop(J, 1);
		}
		s = *e ? e + 1 : e;
	}
}

/* still not compiled (the host's %runJobs: no promise yet, no jobs) */
int jsB_lazypending(js_State *J, const char *name)
{
	return lazy_pending(J, name);
}

static void ES_construct(js_State *J)
{
	int n, i;
	if (!js_iscallable(J, 1))
		js_typeerror(J, "%s is not a constructor", js_typeof(J, 1));
	n = js_isobject(J, 2) ? js_getlength(J, 2) : 0;
	js_copy(J, 1);
	for (i = 0; i < n; ++i)
		js_getindex(J, 2, i);
	js_construct(J, n);
}

static void ES_setPrototypeOf(js_State *J)
{
	js_Object *obj, *proto, *p;
	if (!js_isobject(J, 1)) {
		js_copy(J, 1);
		return;
	}
	obj = js_toobject(J, 1);
	if (js_isnull(J, 2))
		proto = NULL;
	else if (js_isobject(J, 2))
		proto = js_toobject(J, 2);
	else
		js_typeerror(J, "Object prototype may only be an Object or null");
	for (p = proto; p; p = p->prototype)
		if (p == obj)
			js_typeerror(J, "Cyclic __proto__ value");
	obj->prototype = proto;
	js_copy(J, 1);
}

/* generator.%co(mode, value): mode 0 next, 1 throw, 2 return -> [value, done] */
static void ES_co(js_State *J)
{
	js_Object *g;
	int mode = js_tointeger(J, 1), st, y;
	if (!js_isobject(J, 0))
		js_typeerror(J, "not a generator");
	g = js_toobject(J, 0);
	js_getproperty(J, 0, "%st");
	st = js_isnumber(J, -1) ? js_tointeger(J, -1) : -1;
	js_pop(J, 1);
	if (st < 0)
		js_typeerror(J, "not a generator");
	if (st == 2)
		js_typeerror(J, "Generator is already running");
	if (st == 3 || mode == 2 || (st == 0 && mode == 1)) {
		js_pushnumber(J, 3);
		js_defproperty(J, 0, "%st", JS_DONTENUM);
		if (mode == 1) {
			js_copy(J, 2);
			js_throw(J);
		}
		js_newarray(J);
		if (mode == 2) js_copy(J, 2); else js_pushundefined(J);
		js_setindex(J, -2, 0);
		js_pushboolean(J, 1);
		js_setindex(J, -2, 1);
		return;
	}
	js_copy(J, 2);
	y = jsR_resumegen(J, g, mode);
	js_newarray(J);
	js_rot2(J);
	js_setindex(J, -2, 0);
	js_pushboolean(J, !y);
	js_setindex(J, -2, 1);
}

void jsB_inites(js_State *J)
{
	js_getglobal(J, "Object");
	js_newcfunction(J, ES_setPrototypeOf, "setPrototypeOf", 2);
	js_defproperty(J, -2, "setPrototypeOf", JS_DONTENUM);
	js_pop(J, 1);
	js_newcfunction(J, ES_construct, "%construct", 2);
	js_defglobal(J, "%construct", JS_DONTENUM);
	if (js_ploadstring(J, "[es]", kBwEs) || (js_pushglobal(J), js_pcall(J, 0)))
		js_report(J, js_trystring(J, -1, "[es] failed"));
	js_pop(J, 1);
	{
		int i;
		jsB_initbytes(J);
		jsB_initwasm(J);
		for (i = 0; i < (int)(sizeof kBwEsLazy / sizeof kBwEsLazy[0]); ++i)
			jsB_lazy(J, kBwEsLazy[i].names, kBwEsLazy[i].src, kBwEsLazy[i].file);
	}
	js_getglobal(J, "%genproto");
	if (js_isobject(J, -1)) {
		js_newcfunction(J, ES_co, "%co", 2);
		js_defproperty(J, -2, "%co", JS_DONTENUM);
	}
	js_pop(J, 1);
}
