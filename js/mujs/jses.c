/* Browser 9300: the engine's ES2015+ runtime: js/es.js (as jses.h) and the parts in C. */
#include "jses.h"

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
	js_getglobal(J, "%genproto");
	if (js_isobject(J, -1)) {
		js_newcfunction(J, ES_co, "%co", 2);
		js_defproperty(J, -2, "%co", JS_DONTENUM);
	}
	js_pop(J, 1);
}
