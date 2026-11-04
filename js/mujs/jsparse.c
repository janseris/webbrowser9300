#include "jsi.h"

#define LIST(h)			jsP_newnode(J, AST_LIST, 0, h, 0, 0, 0)

#define EXP0(x)			jsP_newnode(J, EXP_ ## x, line, 0, 0, 0, 0)
#define EXP1(x,a)		jsP_newnode(J, EXP_ ## x, line, a, 0, 0, 0)
#define EXP2(x,a,b)		jsP_newnode(J, EXP_ ## x, line, a, b, 0, 0)
#define EXP3(x,a,b,c)		jsP_newnode(J, EXP_ ## x, line, a, b, c, 0)

#define STM0(x)			jsP_newnode(J, STM_ ## x, line, 0, 0, 0, 0)
#define STM1(x,a)		jsP_newnode(J, STM_ ## x, line, a, 0, 0, 0)
#define STM2(x,a,b)		jsP_newnode(J, STM_ ## x, line, a, b, 0, 0)
#define STM3(x,a,b,c)		jsP_newnode(J, STM_ ## x, line, a, b, c, 0)
#define STM4(x,a,b,c,d)		jsP_newnode(J, STM_ ## x, line, a, b, c, d)

static js_Ast *expression(js_State *J, int notin);
static js_Ast *assignment(js_State *J, int notin);
static js_Ast *memberexp(js_State *J);
static js_Ast *statement(js_State *J);
static js_Ast *funbody(js_State *J);
static js_Ast *conditional(js_State *J, int notin);
static js_Ast *unary(js_State *J);
static js_Ast *bindingtarget(js_State *J, int kind);
static js_Ast *classexp(js_State *J, int line, js_Ast *name);
static js_Ast *templatelit(js_State *J, js_Ast *tag);
static js_Ast *propname(js_State *J);
static void expectword(js_State *J, const char *w);

JS_NORETURN static void jsP_error(js_State *J, const char *fmt, ...) JS_PRINTFLIKE(2,3);

#define INCREC() if (++J->astdepth > JS_ASTLIMIT) jsP_error(J, "too much recursion")
#define DECREC() --J->astdepth
#define SAVEREC() int SAVE=J->astdepth
#define POPREC() J->astdepth=SAVE

static void jsP_error(js_State *J, const char *fmt, ...)
{
	va_list ap;
	char buf[512];
	char msgbuf[256];

	va_start(ap, fmt);
	vsnprintf(msgbuf, 256, fmt, ap);
	va_end(ap);

	if (J->es_linestart && J->source >= J->es_linestart && J->lexline == J->line)
		snprintf(buf, 256, "%s:%d:%d: ", J->filename, J->lexline, (int)(J->source - J->es_linestart));
	else
		snprintf(buf, 256, "%s:%d: ", J->filename, J->lexline);
	strcat(buf, msgbuf);

	js_newsyntaxerror(J, buf);
	js_throw(J);
}

static void jsP_warning(js_State *J, const char *fmt, ...)
{
	va_list ap;
	char buf[512];
	char msg[256];

	va_start(ap, fmt);
	vsnprintf(msg, sizeof msg, fmt, ap);
	va_end(ap);

	snprintf(buf, sizeof buf, "%s:%d: warning: %s", J->filename, J->lexline, msg);
	js_report(J, buf);
}

static js_Ast *jsP_newnode(js_State *J, enum js_AstType type, int line, js_Ast *a, js_Ast *b, js_Ast *c, js_Ast *d)
{
	js_Ast *node = js_malloc(J, sizeof *node);

	node->type = type;
	node->line = line;
	node->a = a;
	node->b = b;
	node->c = c;
	node->d = d;
	node->number = 0;
	node->string = NULL;
	node->jumps = NULL;
	node->casejump = 0;

	node->parent = NULL;
	if (a) a->parent = node;
	if (b) b->parent = node;
	if (c) c->parent = node;
	if (d) d->parent = node;

	node->gcnext = J->gcast;
	J->gcast = node;

	return node;
}

static js_Ast *jsP_list(js_Ast *head)
{
	/* set parent pointers in list nodes */
	js_Ast *prev = head, *node = head->b;
	while (node) {
		node->parent = prev;
		prev = node;
		node = node->b;
	}
	return head;
}

static js_Ast *jsP_newstrnode(js_State *J, enum js_AstType type, const char *s)
{
	js_Ast *node = jsP_newnode(J, type, J->lexline, 0, 0, 0, 0);
	node->string = js_intern(J, s);
	return node;
}

static js_Ast *jsP_newnumnode(js_State *J, enum js_AstType type, double n)
{
	js_Ast *node = jsP_newnode(J, type, J->lexline, 0, 0, 0, 0);
	node->number = n;
	return node;
}

static void jsP_freejumps(js_State *J, js_JumpList *node)
{
	while (node) {
		js_JumpList *next = node->next;
		js_free(J, node);
		node = next;
	}
}

void jsP_freeparse(js_State *J)
{
	js_Ast *node = J->gcast;
	while (node) {
		js_Ast *next = node->gcnext;
		jsP_freejumps(J, node->jumps);
		js_free(J, node);
		node = next;
	}
	J->gcast = NULL;
}

/* Lookahead */

static void jsP_next(js_State *J)
{
	J->lookahead = jsY_lex(J);
}

#define jsP_accept(J,x) (J->lookahead == x ? (jsP_next(J), 1) : 0)

#define jsP_expect(J,x) if (!jsP_accept(J, x)) jsP_error(J, "unexpected token: %s (expected %s)", jsY_tokenstring(J->lookahead), jsY_tokenstring(x))

static void semicolon(js_State *J)
{
	if (J->lookahead == ';') {
		jsP_next(J);
		return;
	}
	if (J->newline || J->lookahead == '}' || J->lookahead == 0)
		return;
	jsP_error(J, "unexpected token: %s (expected ';')", jsY_tokenstring(J->lookahead));
}

/* Browser 9300: ES2015+ syntax, lowered to ES5 trees where it can be (let/const, arrows,
 * classes, destructuring, default/rest parameters, for-of, templates, **, ??=, ||=, &&=).
 * Hidden names start with '%' so scripts cannot name them. */

typedef struct {
	const char *source, *text, *raw, *linestart;
	int line, lexline, lexchar, lasttoken, newline, lookahead;
	double number;
} jsP_LexState;

static void lexsave(js_State *J, jsP_LexState *s)
{
	s->source = J->source; s->line = J->line; s->lexline = J->lexline; s->lexchar = J->lexchar;
	s->lasttoken = J->lasttoken; s->newline = J->newline; s->lookahead = J->lookahead;
	s->number = J->number; s->raw = J->es_raw; s->linestart = J->es_linestart;
	if (J->lookahead == TK_IDENTIFIER || J->lookahead == TK_STRING || J->lookahead == TK_REGEXP)
		s->text = js_intern(J, J->text);
	else
		s->text = J->text;
}

static void lexrestore(js_State *J, jsP_LexState *s)
{
	J->source = s->source; J->line = s->line; J->lexline = s->lexline; J->lexchar = s->lexchar;
	J->lasttoken = s->lasttoken; J->newline = s->newline; J->lookahead = s->lookahead;
	J->number = s->number; J->es_raw = s->raw; J->text = s->text; J->es_linestart = s->linestart;
}

/* the token after the lookahead (and whether a line break is before it) */
static int jsP_peek(js_State *J, int *nl)
{
	jsP_LexState s;
	int t;
	lexsave(J, &s);
	jsP_next(J);
	t = J->lookahead;
	if (nl) *nl = J->newline;
	lexrestore(J, &s);
	return t;
}

static int isword(js_State *J, const char *w)
{
	return J->lookahead == TK_IDENTIFIER && !strcmp(J->text, w);
}

static const char *hidname(js_State *J, const char *base)
{
	char buf[48];
	snprintf(buf, sizeof buf, "%%%s%d", base, ++J->es_tmpn);
	return js_intern(J, buf);
}

static js_Ast *idexp(js_State *J, const char *name)
{
	js_Ast *a = jsP_newnode(J, EXP_IDENTIFIER, J->lexline, 0, 0, 0, 0);
	a->string = js_intern(J, name);
	return a;
}

static js_Ast *idast(js_State *J, const char *name)
{
	js_Ast *a = jsP_newnode(J, AST_IDENTIFIER, J->lexline, 0, 0, 0, 0);
	a->string = js_intern(J, name);
	return a;
}

static js_Ast *strexp(js_State *J, const char *s)
{
	js_Ast *a = jsP_newnode(J, EXP_STRING, J->lexline, 0, 0, 0, 0);
	a->string = js_intern(J, s);
	return a;
}

static js_Ast *numexp(js_State *J, double n)
{
	js_Ast *a = jsP_newnode(J, EXP_NUMBER, J->lexline, 0, 0, 0, 0);
	a->number = n;
	return a;
}

static js_Ast *undefexp(js_State *J)
{
	int line = J->lexline;
	return EXP1(VOID, numexp(J, 0));
}

typedef struct { js_Ast *head, *tail; } jsP_Build;

static void bpush(js_State *J, jsP_Build *b, js_Ast *node)
{
	js_Ast *l = LIST(node);
	if (b->tail) {
		b->tail->b = l;
		l->parent = b->tail;
	} else {
		b->head = l;
	}
	b->tail = l;
}

static js_Ast *list1(js_State *J, js_Ast *a)
{
	return LIST(a);
}

static js_Ast *list2(js_State *J, js_Ast *a, js_Ast *b)
{
	jsP_Build l = { 0, 0 };
	bpush(J, &l, a);
	bpush(J, &l, b);
	return l.head;
}

static js_Ast *helpercall(js_State *J, const char *name, js_Ast *args)
{
	int line = J->lexline;
	return EXP2(CALL, idexp(J, name), args);
}

/* a copy of an expression (for "a op= b" lowered to "a = a op b") */
static js_Ast *cloneexp(js_State *J, js_Ast *n)
{
	js_Ast *c;
	if (!n)
		return NULL;
	c = jsP_newnode(J, n->type, n->line, cloneexp(J, n->a), cloneexp(J, n->b), cloneexp(J, n->c), cloneexp(J, n->d));
	c->number = n->number;
	c->string = n->string;
	return c;
}

/* a function's lowering state, saved around a nested function */
typedef struct {
	int arrow, this_, args, block, gen, async, level;
	js_Ast *tmps, *pro;
} jsP_FunCtx;

static void funbegin(js_State *J, jsP_FunCtx *c, int arrow, int gen, int async)
{
	c->arrow = J->es_arrow; c->this_ = J->es_this; c->args = J->es_args;
	c->block = J->es_block; c->gen = J->es_gen; c->async = J->es_async;
	c->tmps = J->es_tmps; c->pro = J->es_pro;
	if (arrow) {
		J->es_arrow++;
	} else {
		J->es_arrow = 0;
		J->es_this = 0;
		J->es_args = 0;
	}
	J->es_tmps = NULL;
	J->es_pro = NULL;
	J->es_block = 0;
	J->es_gen = gen;
	J->es_async = async;
	c->level = ++J->es_level;
}

static void popnames(js_State *J, int level)
{
	while (J->es_names && J->es_names->number >= level) {
		if (J->es_names->a)
			J->es_renamed--;
		J->es_names = J->es_names->b;
	}
}

/* the body with the function's prologue in front ("use strict" stays first) */
static js_Ast *funend(js_State *J, jsP_FunCtx *c, js_Ast *body, int arrow)
{
	jsP_Build b = { 0, 0 };
	js_Ast *rest = body, *l, *rev = NULL;
	int line = J->lexline;

	if (body && body->a && body->a->type == EXP_STRING && !strcmp(body->a->string, "use strict")) {
		bpush(J, &b, body->a);
		rest = body->b;
	}
	if (!arrow && J->es_this)
		bpush(J, &b, STM1(VAR, LIST(EXP2(VAR, idast(J, "%this"), EXP0(THIS)))));
	if (!arrow && J->es_args)
		bpush(J, &b, STM1(VAR, LIST(EXP2(VAR, idast(J, "%args"), idexp(J, "arguments")))));
	if (J->es_tmps)
		bpush(J, &b, STM1(VAR, jsP_list(J->es_tmps)));
	for (l = J->es_pro; l; l = l->b)
		rev = jsP_newnode(J, AST_LIST, 0, l->a, rev, 0, 0);
	for (l = rev; l; l = l->b)
		bpush(J, &b, l->a);

	popnames(J, c->level);
	J->es_level = c->level - 1;
	J->es_arrow = c->arrow;
	if (!arrow) {
		J->es_this = c->this_;
		J->es_args = c->args;
	}
	J->es_tmps = c->tmps; J->es_pro = c->pro; J->es_block = c->block;
	J->es_gen = c->gen; J->es_async = c->async;

	if (!b.head)
		return body;
	b.tail->b = rest;
	return jsP_list(b.head);
}

/* a statement for the current function's prologue */
static void prologue(js_State *J, js_Ast *stm)
{
	J->es_pro = jsP_newnode(J, AST_LIST, 0, stm, J->es_pro, 0, 0);
}

/* a hidden variable of the current function */
static const char *newtemp(js_State *J)
{
	const char *name = hidname(J, "t");
	J->es_tmps = jsP_newnode(J, AST_LIST, 0, jsP_newnode(J, EXP_VAR, 0, idast(J, name), 0, 0, 0), J->es_tmps, 0, 0);
	return name;
}

/* Names in scope, for let/const/class in blocks: one that would clash with a name already
 * visible (function-scoped var semantics) is given a hidden name for the rest of its block. */
static int visible(js_State *J, const char *name)
{
	js_Ast *n;
	for (n = J->es_names; n; n = n->b)
		if (n->string == name)	/* (interned) */
			return 1;
	return 0;
}

static void declname(js_State *J, js_Ast *id, int blockscoped)
{
	js_Ast *n;
	char buf[160];
	n = jsP_newnode(J, AST_IDENTIFIER, 0, 0, 0, 0, 0);
	n->string = id->string;
	n->number = J->es_level;
	n->b = J->es_names;
	if (blockscoped && J->es_block > 0 && strlen(id->string) < 120 && visible(J, id->string)) {
		snprintf(buf, sizeof buf, "%s%%%d", id->string, ++J->es_tmpn);
		n->a = idast(J, buf);
		id->string = n->a->string;
		J->es_renamed++;
	}
	J->es_names = n;
}

/* a name read: renamed, or an imported binding (Browser 9300: modules, live: %mN["key"]) */
static js_Ast *nameexp(js_State *J, const char *name)
{
	js_Ast *n;
	int line = J->lexline;
	if (J->es_renamed) {
		name = js_intern(J, name);
		for (n = J->es_names; n; n = n->b)
			if (n->string == name) {
				if (n->c)
					return EXP2(INDEX, idexp(J, n->a->string), strexp(J, n->c->string));
				return jsP_newstrnode(J, EXP_IDENTIFIER, n->a ? n->a->string : name);
			}
	}
	return jsP_newstrnode(J, EXP_IDENTIFIER, name);
}

static void blockbegin(js_State *J)
{
	J->es_block++;
	J->es_level++;
}

static void blockend(js_State *J)
{
	popnames(J, J->es_level);
	J->es_level--;
	J->es_block--;
}

static js_Ast *thisexp(js_State *J, int line)
{
	if (J->es_arrow) {
		J->es_this = 1;
		return idexp(J, "%this");
	}
	return EXP0(THIS);
}

/* Destructuring: pat (from a binding pattern or an array/object literal) gets the value val.
 * decl: the steps are var initializers (EXP_VAR), else assignments (EXP_ASS). */
static void dsstep(js_State *J, jsP_Build *out, int decl, js_Ast *target, js_Ast *val)
{
	int line = J->lexline;
	if (decl) {
		if (target->type == EXP_IDENTIFIER)
			target = idast(J, target->string);
		if (target->type != AST_IDENTIFIER)
			jsP_error(J, "invalid destructuring target");
		bpush(J, out, EXP2(VAR, target, val));
	} else {
		bpush(J, out, EXP2(ASS, target, val));
	}
}

static void destructure(js_State *J, jsP_Build *out, int decl, js_Ast *pat, js_Ast *val)
{
	int line = pat->line;
	const char *t;
	js_Ast *l;
	int i;

	switch (pat->type) {
	case EXP_ASS: /* target = default */
		if (val->type != EXP_IDENTIFIER) {
			t = newtemp(J);
			dsstep(J, out, decl, idexp(J, t), val);
			val = idexp(J, t);
		}
		destructure(J, out, decl, pat->a,
			EXP3(COND, EXP2(STRICTEQ, cloneexp(J, val), undefexp(J)), pat->b, cloneexp(J, val)));
		return;

	case EXP_ARRAY:
		t = newtemp(J);
		dsstep(J, out, decl, idexp(J, t), helpercall(J, "%arr", list1(J, val)));
		for (i = 0, l = pat->a; l; l = l->b) {
			js_Ast *e = l->a;
			if (e->type == EXP_ELISION) {
				++i;
				continue;
			}
			if (e->type == EXP_SPREAD) {
				destructure(J, out, decl, e->a, helpercall(J, "%slice", list2(J, idexp(J, t), numexp(J, i))));
				break;
			}
			destructure(J, out, decl, e, EXP2(INDEX, idexp(J, t), numexp(J, i)));
			++i;
		}
		return;

	case EXP_OBJECT: {
		jsP_Build keys = { 0, 0 };
		t = newtemp(J);
		dsstep(J, out, decl, idexp(J, t), val);
		for (l = pat->a; l; l = l->b) {
			js_Ast *e = l->a, *key, *acc;
			if (e->type == EXP_SPREAD) {
				destructure(J, out, decl, e->a, helpercall(J, "%objrest", list2(J, idexp(J, t), EXP1(ARRAY, keys.head))));
				break;
			}
			if (e->type != EXP_PROP_VAL)
				jsP_error(J, "invalid destructuring pattern");
			key = e->a;
			if (key->type == AST_IDENTIFIER) {
				acc = EXP2(MEMBER, idexp(J, t), idast(J, key->string));
				bpush(J, &keys, strexp(J, key->string));
			} else if (key->type == EXP_STRING) {
				acc = EXP2(INDEX, idexp(J, t), strexp(J, key->string));
				bpush(J, &keys, strexp(J, key->string));
			} else if (key->type == EXP_NUMBER) {
				acc = EXP2(INDEX, idexp(J, t), numexp(J, key->number));
				bpush(J, &keys, numexp(J, key->number));
			} else {
				const char *k = newtemp(J);
				dsstep(J, out, decl, idexp(J, k), key);
				acc = EXP2(INDEX, idexp(J, t), idexp(J, k));
				bpush(J, &keys, idexp(J, k));
			}
			destructure(J, out, decl, e->b, acc);
		}
		return;
	}

	default:
		dsstep(J, out, decl, pat, val);
	}
}

/* the steps as one expression ("(a = .., b = .., value)") */
static js_Ast *dsexpression(js_State *J, jsP_Build *steps, js_Ast *value)
{
	int line = J->lexline;
	js_Ast *e = NULL, *l;
	for (l = steps->head; l; l = l->b)
		e = e ? EXP2(COMMA, e, l->a) : l->a;
	return e ? EXP2(COMMA, e, value) : value;
}

/* Literals */

static js_Ast *identifier(js_State *J)
{
	js_Ast *a;
	if (J->lookahead == TK_IDENTIFIER) {
		a = jsP_newstrnode(J, AST_IDENTIFIER, J->text);
		jsP_next(J);
		return a;
	}
	jsP_error(J, "unexpected token: %s (expected identifier)", jsY_tokenstring(J->lookahead));
}

static js_Ast *identifieropt(js_State *J)
{
	if (J->lookahead == TK_IDENTIFIER)
		return identifier(J);
	return NULL;
}

static js_Ast *identifiername(js_State *J)
{
	if (J->lookahead == TK_IDENTIFIER || J->lookahead >= TK_BREAK) {
		js_Ast *a = jsP_newstrnode(J, AST_IDENTIFIER, J->text);
		jsP_next(J);
		return a;
	}
	jsP_error(J, "unexpected token: %s (expected identifier or keyword)", jsY_tokenstring(J->lookahead));
}

static js_Ast *arrayelement(js_State *J)
{
	int line = J->lexline;
	if (J->lookahead == ',')
		return EXP0(ELISION);
	if (jsP_accept(J, TK_ELLIPSIS))
		return EXP1(SPREAD, assignment(J, 0));
	return assignment(J, 0);
}

static js_Ast *arrayliteral(js_State *J)
{
	js_Ast *head, *tail;
	if (J->lookahead == ']')
		return NULL;
	head = tail = LIST(arrayelement(J));
	while (jsP_accept(J, ',')) {
		if (J->lookahead != ']')
			tail = tail->b = LIST(arrayelement(J));
	}
	return jsP_list(head);
}

static js_Ast *propname(js_State *J)
{
	js_Ast *name;
	if (J->lookahead == TK_NUMBER) {
		name = jsP_newnumnode(J, EXP_NUMBER, J->number);
		jsP_next(J);
	} else if (J->lookahead == TK_STRING) {
		name = jsP_newstrnode(J, EXP_STRING, J->text);
		jsP_next(J);
	} else if (jsP_accept(J, '[')) { /* computed: any expression but a literal */
		name = assignment(J, 0);
		jsP_expect(J, ']');
		if (name->type == EXP_STRING || name->type == EXP_NUMBER)
			return name;
		if (name->type == AST_IDENTIFIER)
			name->type = EXP_IDENTIFIER;
	} else {
		name = identifiername(J);
	}
	return name;
}

static js_Ast *methodfun(js_State *J, int line, int gen, int async);

static js_Ast *propassign(js_State *J)
{
	js_Ast *name, *value, *arg, *body;
	int line = J->lexline;

	int gen = 0, async = 0;
	js_Ast *f;
	(void)arg; (void)body;

	if (jsP_accept(J, TK_ELLIPSIS))
		return EXP1(SPREAD, assignment(J, 0));
	if (jsP_accept(J, '*'))
		gen = 1;
	name = propname(J);

	if (name->type == AST_IDENTIFIER && !gen && J->lookahead != ':' && J->lookahead != '(' &&
			J->lookahead != ',' && J->lookahead != '}' && J->lookahead != '=') {
		if (!strcmp(name->string, "get") || !strcmp(name->string, "set")) {
			int isget = name->string[0] == 'g';
			name = propname(J);
			f = methodfun(J, line, 0, 0);
			if (isget)
				return EXP3(PROP_GET, name, NULL, f->c);
			if (!f->b)
				jsP_error(J, "setter without a parameter");
			return EXP3(PROP_SET, name, f->b, f->c);
		}
		if (!strcmp(name->string, "async")) {
			async = 1;
			if (jsP_accept(J, '*'))
				gen = 1;
			name = propname(J);
		}
	}

	if (J->lookahead == '(') /* method */
		return EXP2(PROP_VAL, name, methodfun(J, line, gen, async));

	if (name->type == AST_IDENTIFIER && (J->lookahead == ',' || J->lookahead == '}' || J->lookahead == '=')) {
		/* shorthand {a}, or {a = 1} in a pattern */
		value = nameexp(J, name->string);
		if (!strcmp(name->string, "arguments") && J->es_arrow) {
			J->es_args = 1;
			value->string = js_intern(J, "%args");
		}
		if (jsP_accept(J, '='))
			value = EXP2(ASS, value, assignment(J, 0));
		return EXP2(PROP_VAL, name, value);
	}

	jsP_expect(J, ':');
	value = assignment(J, 0);
	return EXP2(PROP_VAL, name, value);
}

static js_Ast *objectliteral(js_State *J)
{
	js_Ast *head, *tail;
	if (J->lookahead == '}')
		return NULL;
	head = tail = LIST(propassign(J));
	while (jsP_accept(J, ',')) {
		if (J->lookahead == '}')
			break;
		tail = tail->b = LIST(propassign(J));
	}
	return jsP_list(head);
}

/* Functions */

/* The formal parameters (after '(', up to ')'), with defaults, rest and patterns lowered into
 * the function's prologue; call between funbegin and funend. */
static js_Ast *parameters(js_State *J)
{
	jsP_Build b = { 0, 0 };
	js_Ast *id, *pat, *def;
	int n = 0, line;

	while (J->lookahead != ')') {
		line = J->lexline;
		if (jsP_accept(J, TK_ELLIPSIS)) {
			jsP_Build steps = { 0, 0 };
			pat = bindingtarget(J, 2);
			destructure(J, &steps, 1, pat, helpercall(J, "%slice", list2(J, idexp(J, "arguments"), numexp(J, n))));
			prologue(J, STM1(VAR, steps.head));
			break;
		}
		if (J->lookahead == TK_IDENTIFIER) {
			id = identifier(J);
			declname(J, id, 0);
			pat = NULL;
		} else {
			pat = bindingtarget(J, 2);
			id = idast(J, hidname(J, "p"));
		}
		if (jsP_accept(J, '=')) {
			def = assignment(J, 0);
			prologue(J, STM3(IF, EXP2(STRICTEQ, idexp(J, id->string), undefexp(J)),
				EXP2(ASS, idexp(J, id->string), def), NULL));
		}
		if (pat) {
			jsP_Build steps = { 0, 0 };
			destructure(J, &steps, 1, pat, idexp(J, id->string));
			prologue(J, STM1(VAR, steps.head));
		}
		bpush(J, &b, id);
		++n;
		if (!jsP_accept(J, ','))
			break;
	}
	return b.head ? jsP_list(b.head) : NULL;
}

/* "(params) { body }" of a function (FUN node without a name); flags: 1 generator, 2 async */
static js_Ast *funrest(js_State *J, int line, js_Ast *name, int gen, int async)
{
	jsP_FunCtx c;
	js_Ast *b, *body, *f;
	funbegin(J, &c, 0, gen, async);
	if (name && J->es_renamed)
		declname(J, name, 0);	/* Browser 9300: inside, a function expression's name is itself (not an outer renamed let) */
	jsP_expect(J, '(');
	b = parameters(J);
	jsP_expect(J, ')');
	body = funbody(J);
	body = funend(J, &c, body, 0);
	f = EXP3(FUN, name, b, body);
	f->number = gen | (async << 1);
	return f;
}

static js_Ast *methodfun(js_State *J, int line, int gen, int async)
{
	return funrest(J, line, NULL, gen, async);
}

static js_Ast *fundec(js_State *J, int line, int async)
{
	js_Ast *a, *f, *n;
	int gen = jsP_accept(J, '*');
	a = identifier(J);
	declname(J, a, 0);
	f = funrest(J, line, NULL, gen, async);
	n = jsP_newnode(J, AST_FUNDEC, line, a, f->b, f->c, 0);
	n->number = f->number;
	return n;
}

static js_Ast *funstm(js_State *J, int line, int async)
{
	js_Ast *a, *f;
	int gen = jsP_accept(J, '*');
	a = identifier(J);
	declname(J, a, 0);
	f = funrest(J, line, a, gen, async);
	/* rewrite function statement as "var X = function X() {}" */
	return STM1(VAR, LIST(EXP2(VAR, a, f)));
}

static js_Ast *funexp(js_State *J, int line, int async)
{
	js_Ast *a;
	int gen = jsP_accept(J, '*');
	a = identifieropt(J);
	return funrest(J, line, a, gen, async);
}

/* Is the '(' in the lookahead the start of an arrow function's parameters? Scans to the
 * matching ')' (quickly giving up on what cannot be a parameter list) and looks for "=>". */
static int isarrowparen(js_State *J)
{
	jsP_LexState s;
	int depth = 1, indef = 0, ok = 0, sp = 0, t;
	int tstack[32];

	lexsave(J, &s);
	jsP_next(J);
	t = J->lookahead;
	if (t != ')' && t != TK_IDENTIFIER && t != '[' && t != '{' && t != TK_ELLIPSIS)
		goto done;
	for (;;) {
		t = J->lookahead;
		if (t == 0)
			goto done;
		if (depth == 1 && !indef) {
			if (t != TK_IDENTIFIER && t != ',' && t != TK_ELLIPSIS && t != '=' && t != ')' && t != '[' && t != '{')
				goto done;
			if (t == '=')
				indef = 1;
		} else if (depth == 1 && t == ',') {
			indef = 0;
		}
		if (t == '(' || t == '[' || t == '{') {
			++depth;
		} else if (t == ')' || t == ']') {
			if (--depth == 0)
				break;
		} else if (t == '}') {
			if (sp > 0 && tstack[sp-1] == depth) { /* the template goes on (its next piece) */
				J->lookahead = jsY_lextemplatenext(J);
				if (!J->number)
					--sp;
				jsP_next(J);
				continue;
			}
			--depth;
		} else if (t == TK_TEMPLATE && J->number) {
			if (sp == 32)
				goto done;
			tstack[sp++] = depth;
		}
		jsP_next(J);
	}
	jsP_next(J);
	ok = J->lookahead == TK_ARROW;
done:
	lexrestore(J, &s);
	return ok;
}

/* an arrow function from its parameters (single: the one identifier, else "(...)") */
static js_Ast *arrowfun(js_State *J, int line, js_Ast *single, int async, int notin)
{
	jsP_FunCtx c;
	js_Ast *params, *body, *f;
	funbegin(J, &c, 1, 0, async);
	if (single) {
		declname(J, single, 0);
		params = LIST(single);
	} else {
		jsP_expect(J, '(');
		params = parameters(J);
		jsP_expect(J, ')');
	}
	jsP_expect(J, TK_ARROW);
	if (J->lookahead == '{') {
		body = funbody(J);
	} else {
		js_Ast *e = assignment(J, notin);
		body = LIST(STM1(RETURN, e));
	}
	body = funend(J, &c, body, 1);
	f = EXP3(FUN, NULL, params, body);
	f->number = async << 1;
	return f;
}

/* Expressions */

static js_Ast *arguments(js_State *J)
{
	jsP_Build b = { 0, 0 };
	int line;
	while (J->lookahead != ')') {
		line = J->lexline;
		if (jsP_accept(J, TK_ELLIPSIS))
			bpush(J, &b, EXP1(SPREAD, assignment(J, 0)));
		else
			bpush(J, &b, assignment(J, 0));
		if (!jsP_accept(J, ','))
			break;
	}
	return b.head ? jsP_list(b.head) : NULL;
}

static js_Ast *primary(js_State *J)
{
	js_Ast *a;
	int line = J->lexline;

	if (J->lookahead == TK_IDENTIFIER) {
		if (J->text[0] == '#') { /* #x in obj */
			a = jsP_newstrnode(J, EXP_STRING, J->text);
			jsP_next(J);
			return a;
		}
		if (!strcmp(J->text, "class")) {
			jsP_next(J);
			return classexp(J, line, NULL);
		}
		if (!strcmp(J->text, "import")) { /* Browser 9300: import.meta, import(url) */
			int t = jsP_peek(J, NULL);
			if (t == '.') {
				jsP_next(J);
				jsP_next(J);
				expectword(J, "meta");
				return idexp(J, "%meta");
			}
			if (t == '(') {
				jsP_next(J);
				jsP_next(J);
				a = assignment(J, 0);
				if (jsP_accept(J, ',') && J->lookahead != ')')
					assignment(J, 0);
				jsP_accept(J, ',');
				jsP_expect(J, ')');
				return helpercall(J, "%dimp", list2(J, a, J->es_module ? idexp(J, "%meta") : undefexp(J)));
			}
		}
		if (!strcmp(J->text, "async")) {
			int nl, t = jsP_peek(J, &nl);
			if (t == TK_FUNCTION && !nl) {
				jsP_next(J);
				jsP_next(J);
				return funexp(J, line, 1);
			}
		}
		if (!strcmp(J->text, "super") && J->es_class) {
			js_Ast *base, *key;
			jsP_next(J);
			if (jsP_accept(J, '(')) { /* super(...) in a constructor */
				js_Ast *args = arguments(J);
				js_Ast *l;
				jsP_expect(J, ')');
				l = jsP_newnode(J, AST_LIST, 0, thisexp(J, line), 0, 0, 0);
				l->b = jsP_newnode(J, AST_LIST, 0, idexp(J, J->es_class->string), 0, 0, 0);
				l->b->b = jsP_newnode(J, AST_LIST, 0, EXP1(ARRAY, args), 0, 0, 0);
				l->b->b->b = jsP_newnode(J, AST_LIST, 0, idexp(J, J->es_class->a->string), 0, 0, 0);
				return helpercall(J, "%superCall", jsP_list(l));
			}
			base = idexp(J, J->es_class->string);
			if (!J->es_class->number)
				base = EXP2(MEMBER, base, idast(J, "prototype"));
			if (jsP_accept(J, '.')) {
				a = EXP2(MEMBER, base, identifiername(J));
			} else {
				jsP_expect(J, '[');
				key = expression(J, 0);
				jsP_expect(J, ']');
				a = EXP2(INDEX, base, key);
			}
			if (jsP_accept(J, '(')) { /* super.m(...): this stays */
				js_Ast *args = arguments(J);
				jsP_expect(J, ')');
				a = EXP2(CALL, EXP2(MEMBER, a, idast(J, "call")),
					jsP_newnode(J, AST_LIST, 0, thisexp(J, line), args, 0, 0));
				jsP_list(a->b);
			}
			return a;
		}
		a = nameexp(J, J->text);
		if (J->es_arrow && a->type == EXP_IDENTIFIER && !strcmp(a->string, "arguments")) {
			J->es_args = 1;
			a->string = js_intern(J, "%args");
		}
		jsP_next(J);
		return a;
	}
	if (J->lookahead == TK_TEMPLATE)
		return templatelit(J, NULL);
	if (J->lookahead == TK_STRING) {
		a = jsP_newstrnode(J, EXP_STRING, J->text);
		jsP_next(J);
		return a;
	}
	if (J->lookahead == TK_REGEXP) {
		a = jsP_newstrnode(J, EXP_REGEXP, J->text);
		a->number = J->number;
		jsP_next(J);
		return a;
	}
	if (J->lookahead == TK_NUMBER) {
		a = jsP_newnumnode(J, EXP_NUMBER, J->number);
		jsP_next(J);
		return a;
	}

	if (jsP_accept(J, TK_THIS)) return thisexp(J, line);
	if (jsP_accept(J, TK_NULL)) return EXP0(NULL);
	if (jsP_accept(J, TK_TRUE)) return EXP0(TRUE);
	if (jsP_accept(J, TK_FALSE)) return EXP0(FALSE);
	if (jsP_accept(J, '{')) {
		a = EXP1(OBJECT, objectliteral(J));
		jsP_expect(J, '}');
		return a;
	}
	if (jsP_accept(J, '[')) {
		a = EXP1(ARRAY, arrayliteral(J));
		jsP_expect(J, ']');
		return a;
	}
	if (jsP_accept(J, '(')) {
		a = expression(J, 0);
		jsP_expect(J, ')');
		return a;
	}

	jsP_error(J, "unexpected token in expression: %s", jsY_tokenstring(J->lookahead));
}


static js_Ast *newexp(js_State *J)
{
	js_Ast *a, *b;
	int line = J->lexline;

	if (jsP_accept(J, TK_NEW)) {
		a = memberexp(J);
		if (jsP_accept(J, '(')) {
			b = arguments(J);
			jsP_expect(J, ')');
			return EXP2(NEW, a, b);
		}
		return EXP1(NEW, a);
	}

	if (jsP_accept(J, TK_FUNCTION))
		return funexp(J, line, 0);

	return primary(J);
}

static js_Ast *memberexp(js_State *J)
{
	js_Ast *a = newexp(J);
	int line;
	SAVEREC();
loop:
	INCREC();
	line = J->lexline;
	if (jsP_accept(J, '.')) { a = EXP2(MEMBER, a, identifiername(J)); goto loop; }
	if (jsP_accept(J, '[')) { a = EXP2(INDEX, a, expression(J, 0)); jsP_expect(J, ']'); goto loop; }
	if (J->lookahead == TK_TEMPLATE) { a = templatelit(J, a); goto loop; }
	POPREC();
	return a;
}

static js_Ast *callexp(js_State *J)
{
	js_Ast *a = newexp(J);
	int line, opt = 0;
	SAVEREC();
loop:
	INCREC();
	line = J->lexline;
	if (jsP_accept(J, '.')) { a = EXP2(MEMBER, a, identifiername(J)); goto loop; }
	if (jsP_accept(J, '[')) { a = EXP2(INDEX, a, expression(J, 0)); jsP_expect(J, ']'); goto loop; }
	if (jsP_accept(J, '(')) { a = EXP2(CALL, a, arguments(J)); jsP_expect(J, ')'); goto loop; }
	if (J->lookahead == TK_TEMPLATE) { a = templatelit(J, a); goto loop; }
	if (jsP_accept(J, TK_QDOT)) {
		opt = 1;
		if (jsP_accept(J, '(')) { a = EXP2(CALL, a, arguments(J)); jsP_expect(J, ')'); }
		else if (jsP_accept(J, '[')) { a = EXP2(INDEX, a, expression(J, 0)); jsP_expect(J, ']'); }
		else a = EXP2(MEMBER, a, identifiername(J));
		a->number = 1;
		goto loop;
	}
	POPREC();
	if (opt)
		a = EXP1(OPTCHAIN, a);
	return a;
}

static js_Ast *postfix(js_State *J)
{
	js_Ast *a = callexp(J);
	int line = J->lexline;
	if (!J->newline && jsP_accept(J, TK_INC)) return EXP1(POSTINC, a);
	if (!J->newline && jsP_accept(J, TK_DEC)) return EXP1(POSTDEC, a);
	return a;
}

static js_Ast *unary(js_State *J)
{
	js_Ast *a;
	int line = J->lexline;
	INCREC();
	if (jsP_accept(J, TK_DELETE)) a = EXP1(DELETE, unary(J));
	else if (jsP_accept(J, TK_VOID)) a = EXP1(VOID, unary(J));
	else if (jsP_accept(J, TK_TYPEOF)) a = EXP1(TYPEOF, unary(J));
	else if (jsP_accept(J, TK_INC)) a = EXP1(PREINC, unary(J));
	else if (jsP_accept(J, TK_DEC)) a = EXP1(PREDEC, unary(J));
	else if (jsP_accept(J, '+')) a = EXP1(POS, unary(J));
	else if (jsP_accept(J, '-')) a = EXP1(NEG, unary(J));
	else if (jsP_accept(J, '~')) a = EXP1(BITNOT, unary(J));
	else if (jsP_accept(J, '!')) a = EXP1(LOGNOT, unary(J));
	else if (J->es_async && isword(J, "await")) { jsP_next(J); a = EXP1(AWAIT, unary(J)); }
	else a = postfix(J);
	DECREC();
	return a;
}

static js_Ast *exponent(js_State *J)
{
	js_Ast *a = unary(J);
	int line = J->lexline;
	if (jsP_accept(J, TK_POW)) {
		js_Ast *b;
		INCREC();
		b = exponent(J);
		DECREC();
		return helpercall(J, "%pow", list2(J, a, b));
	}
	return a;
}

static js_Ast *multiplicative(js_State *J)
{
	js_Ast *a = exponent(J);
	int line;
	SAVEREC();
loop:
	INCREC();
	line = J->lexline;
	if (jsP_accept(J, '*')) { a = EXP2(MUL, a, exponent(J)); goto loop; }
	if (jsP_accept(J, '/')) { a = EXP2(DIV, a, exponent(J)); goto loop; }
	if (jsP_accept(J, '%')) { a = EXP2(MOD, a, exponent(J)); goto loop; }
	POPREC();
	return a;
}

static js_Ast *additive(js_State *J)
{
	js_Ast *a = multiplicative(J);
	int line;
	SAVEREC();
loop:
	INCREC();
	line = J->lexline;
	if (jsP_accept(J, '+')) { a = EXP2(ADD, a, multiplicative(J)); goto loop; }
	if (jsP_accept(J, '-')) { a = EXP2(SUB, a, multiplicative(J)); goto loop; }
	POPREC();
	return a;
}

static js_Ast *shift(js_State *J)
{
	js_Ast *a = additive(J);
	int line;
	SAVEREC();
loop:
	INCREC();
	line = J->lexline;
	if (jsP_accept(J, TK_SHL)) { a = EXP2(SHL, a, additive(J)); goto loop; }
	if (jsP_accept(J, TK_SHR)) { a = EXP2(SHR, a, additive(J)); goto loop; }
	if (jsP_accept(J, TK_USHR)) { a = EXP2(USHR, a, additive(J)); goto loop; }
	POPREC();
	return a;
}

static js_Ast *relational(js_State *J, int notin)
{
	js_Ast *a = shift(J);
	int line;
	SAVEREC();
loop:
	INCREC();
	line = J->lexline;
	if (jsP_accept(J, '<')) { a = EXP2(LT, a, shift(J)); goto loop; }
	if (jsP_accept(J, '>')) { a = EXP2(GT, a, shift(J)); goto loop; }
	if (jsP_accept(J, TK_LE)) { a = EXP2(LE, a, shift(J)); goto loop; }
	if (jsP_accept(J, TK_GE)) { a = EXP2(GE, a, shift(J)); goto loop; }
	if (jsP_accept(J, TK_INSTANCEOF)) { a = EXP2(INSTANCEOF, a, shift(J)); goto loop; }
	if (!notin && jsP_accept(J, TK_IN)) { a = EXP2(IN, a, shift(J)); goto loop; }
	POPREC();
	return a;
}

static js_Ast *equality(js_State *J, int notin)
{
	js_Ast *a = relational(J, notin);
	int line;
	SAVEREC();
loop:
	INCREC();
	line = J->lexline;
	if (jsP_accept(J, TK_EQ)) { a = EXP2(EQ, a, relational(J, notin)); goto loop; }
	if (jsP_accept(J, TK_NE)) { a = EXP2(NE, a, relational(J, notin)); goto loop; }
	if (jsP_accept(J, TK_STRICTEQ)) { a = EXP2(STRICTEQ, a, relational(J, notin)); goto loop; }
	if (jsP_accept(J, TK_STRICTNE)) { a = EXP2(STRICTNE, a, relational(J, notin)); goto loop; }
	POPREC();
	return a;
}

static js_Ast *bitand(js_State *J, int notin)
{
	js_Ast *a = equality(J, notin);
	SAVEREC();
	int line = J->lexline;
	while (jsP_accept(J, '&')) {
		INCREC();
		a = EXP2(BITAND, a, equality(J, notin));
		line = J->lexline;
	}
	POPREC();
	return a;
}

static js_Ast *bitxor(js_State *J, int notin)
{
	js_Ast *a = bitand(J, notin);
	SAVEREC();
	int line = J->lexline;
	while (jsP_accept(J, '^')) {
		INCREC();
		a = EXP2(BITXOR, a, bitand(J, notin));
		line = J->lexline;
	}
	POPREC();
	return a;
}

static js_Ast *bitor(js_State *J, int notin)
{
	js_Ast *a = bitxor(J, notin);
	SAVEREC();
	int line = J->lexline;
	while (jsP_accept(J, '|')) {
		INCREC();
		a = EXP2(BITOR, a, bitxor(J, notin));
		line = J->lexline;
	}
	POPREC();
	return a;
}

static js_Ast *logand(js_State *J, int notin)
{
	js_Ast *a = bitor(J, notin);
	int line = J->lexline;
	if (jsP_accept(J, TK_AND)) {
		INCREC();
		a = EXP2(LOGAND, a, logand(J, notin));
		DECREC();
	}
	return a;
}

static js_Ast *logor(js_State *J, int notin)
{
	js_Ast *a = logand(J, notin);
	int line = J->lexline;
	if (jsP_accept(J, TK_OR)) {
		INCREC();
		a = EXP2(LOGOR, a, logor(J, notin));
		DECREC();
	}
	return a;
}

static js_Ast *conditional(js_State *J, int notin)
{
	js_Ast *a = logor(J, notin);
	int line = J->lexline;
	while (jsP_accept(J, TK_NULLISH)) {
		INCREC();
		a = EXP2(NULLISH, a, logor(J, notin));
		DECREC();
		line = J->lexline;
	}
	if (jsP_accept(J, '?')) {
		js_Ast *b, *c;
		INCREC();
		b = assignment(J, 0);
		jsP_expect(J, ':');
		c = assignment(J, notin);
		DECREC();
		return EXP3(COND, a, b, c);
	}
	return a;
}

static js_Ast *assignment(js_State *J, int notin)
{
	js_Ast *a;
	int line = J->lexline;

	if (J->lookahead == TK_IDENTIFIER) {
		int nl, t = jsP_peek(J, &nl);
		if (t == TK_ARROW && J->text[0] != '#')
			return arrowfun(J, line, identifier(J), 0, notin);
		if (!nl && !strcmp(J->text, "async") && (t == TK_IDENTIFIER || t == '(')) {
			jsP_LexState s;
			int ok;
			lexsave(J, &s);
			jsP_next(J);
			if (t == TK_IDENTIFIER)
				ok = jsP_peek(J, NULL) == TK_ARROW;
			else
				ok = isarrowparen(J);
			if (ok)
				return arrowfun(J, line, t == TK_IDENTIFIER ? identifier(J) : NULL, 1, notin);
			lexrestore(J, &s);
		}
		if (J->es_gen && !strcmp(J->text, "yield")) {
			int star = 0;
			jsP_next(J);
			if (!J->newline && jsP_accept(J, '*'))
				star = 1;
			if (J->newline || J->lookahead == ')' || J->lookahead == ']' || J->lookahead == '}' ||
					J->lookahead == ',' || J->lookahead == ';' || J->lookahead == ':' || J->lookahead == 0)
				a = EXP1(YIELD, NULL);
			else
				a = EXP1(YIELD, assignment(J, notin));
			a->number = star;
			return a;
		}
	} else if (J->lookahead == '(' && isarrowparen(J)) {
		return arrowfun(J, line, NULL, 0, notin);
	}

	a = conditional(J, notin);
	line = J->lexline;
	INCREC();
	if ((a->type == EXP_ARRAY || a->type == EXP_OBJECT) && J->lookahead == '=') {
		jsP_Build steps = { 0, 0 };
		const char *t = newtemp(J);
		js_Ast *rhs;
		jsP_next(J);
		rhs = assignment(J, notin);
		dsstep(J, &steps, 0, idexp(J, t), rhs);
		destructure(J, &steps, 0, a, idexp(J, t));
		a = dsexpression(J, &steps, idexp(J, t));
	}
	else if (jsP_accept(J, '=')) a = EXP2(ASS, a, assignment(J, notin));
	else if (jsP_accept(J, TK_POW_ASS)) a = EXP2(ASS, a, helpercall(J, "%pow", list2(J, cloneexp(J, a), assignment(J, notin))));
	else if (jsP_accept(J, TK_NULLISH_ASS)) a = EXP2(NULLISH, a, EXP2(ASS, cloneexp(J, a), assignment(J, notin)));
	else if (jsP_accept(J, TK_LOR_ASS)) a = EXP2(LOGOR, a, EXP2(ASS, cloneexp(J, a), assignment(J, notin)));
	else if (jsP_accept(J, TK_LAND_ASS)) a = EXP2(LOGAND, a, EXP2(ASS, cloneexp(J, a), assignment(J, notin)));
	else if (jsP_accept(J, TK_MUL_ASS)) a = EXP2(ASS_MUL, a, assignment(J, notin));
	else if (jsP_accept(J, TK_DIV_ASS)) a = EXP2(ASS_DIV, a, assignment(J, notin));
	else if (jsP_accept(J, TK_MOD_ASS)) a = EXP2(ASS_MOD, a, assignment(J, notin));
	else if (jsP_accept(J, TK_ADD_ASS)) a = EXP2(ASS_ADD, a, assignment(J, notin));
	else if (jsP_accept(J, TK_SUB_ASS)) a = EXP2(ASS_SUB, a, assignment(J, notin));
	else if (jsP_accept(J, TK_SHL_ASS)) a = EXP2(ASS_SHL, a, assignment(J, notin));
	else if (jsP_accept(J, TK_SHR_ASS)) a = EXP2(ASS_SHR, a, assignment(J, notin));
	else if (jsP_accept(J, TK_USHR_ASS)) a = EXP2(ASS_USHR, a, assignment(J, notin));
	else if (jsP_accept(J, TK_AND_ASS)) a = EXP2(ASS_BITAND, a, assignment(J, notin));
	else if (jsP_accept(J, TK_XOR_ASS)) a = EXP2(ASS_BITXOR, a, assignment(J, notin));
	else if (jsP_accept(J, TK_OR_ASS)) a = EXP2(ASS_BITOR, a, assignment(J, notin));
	DECREC();
	return a;
}

static js_Ast *expression(js_State *J, int notin)
{
	js_Ast *a = assignment(J, notin);
	SAVEREC();
	int line = J->lexline;
	while (jsP_accept(J, ',')) {
		INCREC();
		a = EXP2(COMMA, a, assignment(J, notin));
		line = J->lexline;
	}
	POPREC();
	return a;
}

/* Statements */

/* kind: 0 var, 1 let/const (block-scoped), 2 a parameter */
static void vardec(js_State *J, jsP_Build *out, int notin, int kind)
{
	js_Ast *a;
	int line = J->lexline;
	if (J->lookahead == '[' || J->lookahead == '{') {
		js_Ast *pat = bindingtarget(J, kind);
		if (!jsP_accept(J, '=')) { /* for (const [a, b] of ...): the loop gives the value */
			bpush(J, out, EXP1(VAR, pat));
			return;
		}
		destructure(J, out, 1, pat, assignment(J, notin));
		return;
	}
	a = identifier(J);
	declname(J, a, kind == 1);
	if (jsP_accept(J, '='))
		bpush(J, out, EXP2(VAR, a, assignment(J, notin)));
	else
		bpush(J, out, EXP1(VAR, a));
}

static js_Ast *vardeclist(js_State *J, int notin, int kind)
{
	jsP_Build b = { 0, 0 };
	vardec(J, &b, notin, kind);
	while (jsP_accept(J, ','))
		vardec(J, &b, notin, kind);
	return jsP_list(b.head);
}

/* a binding pattern or identifier (patterns: EXP_ARRAY / EXP_OBJECT of targets) */
static js_Ast *bindingelement(js_State *J, int kind)
{
	js_Ast *t = bindingtarget(J, kind);
	int line = J->lexline;
	if (jsP_accept(J, '='))
		return EXP2(ASS, t, assignment(J, 0));
	return t;
}

static js_Ast *bindingtarget(js_State *J, int kind)
{
	jsP_Build b = { 0, 0 };
	js_Ast *key, *val, *id;
	int line = J->lexline;

	if (jsP_accept(J, '[')) {
		while (J->lookahead != ']') {
			if (J->lookahead == ',') {
				jsP_next(J);
				bpush(J, &b, EXP0(ELISION));
				continue;
			}
			if (jsP_accept(J, TK_ELLIPSIS))
				bpush(J, &b, EXP1(SPREAD, bindingtarget(J, kind)));
			else
				bpush(J, &b, bindingelement(J, kind));
			if (!jsP_accept(J, ','))
				break;
		}
		jsP_expect(J, ']');
		return EXP1(ARRAY, b.head ? jsP_list(b.head) : NULL);
	}
	if (jsP_accept(J, '{')) {
		while (J->lookahead != '}') {
			if (jsP_accept(J, TK_ELLIPSIS)) {
				bpush(J, &b, EXP1(SPREAD, bindingtarget(J, kind)));
			} else {
				key = propname(J);
				if (jsP_accept(J, ':')) {
					val = bindingelement(J, kind);
				} else {
					if (key->type != AST_IDENTIFIER)
						jsP_error(J, "unexpected token in pattern: %s", jsY_tokenstring(J->lookahead));
					id = idast(J, key->string);
					declname(J, id, kind == 1);
					val = id;
					if (jsP_accept(J, '='))
						val = EXP2(ASS, id, assignment(J, 0));
				}
				bpush(J, &b, EXP2(PROP_VAL, key, val));
			}
			if (!jsP_accept(J, ','))
				break;
		}
		jsP_expect(J, '}');
		return EXP1(OBJECT, b.head ? jsP_list(b.head) : NULL);
	}
	id = identifier(J);
	declname(J, id, kind == 1);
	return id;
}

/* is the lookahead "let"/"const" starting a declaration? */
static int islexical(js_State *J)
{
	int t;
	if (J->lookahead != TK_IDENTIFIER)
		return 0;
	if (strcmp(J->text, "let") && strcmp(J->text, "const"))
		return 0;
	t = jsP_peek(J, NULL);
	return t == TK_IDENTIFIER || t == '[' || t == '{';
}

static js_Ast *statementlist(js_State *J)
{
	js_Ast *head, *tail;
	if (J->lookahead == '}' || J->lookahead == TK_CASE || J->lookahead == TK_DEFAULT)
		return NULL;
	head = tail = LIST(statement(J));
	while (J->lookahead != '}' && J->lookahead != TK_CASE && J->lookahead != TK_DEFAULT)
		tail = tail->b = LIST(statement(J));
	return jsP_list(head);
}

static js_Ast *caseclause(js_State *J)
{
	js_Ast *a, *b;
	int line = J->lexline;

	if (jsP_accept(J, TK_CASE)) {
		a = expression(J, 0);
		jsP_expect(J, ':');
		b = statementlist(J);
		return STM2(CASE, a, b);
	}

	if (jsP_accept(J, TK_DEFAULT)) {
		jsP_expect(J, ':');
		a = statementlist(J);
		return STM1(DEFAULT, a);
	}

	jsP_error(J, "unexpected token in switch: %s (expected 'case' or 'default')", jsY_tokenstring(J->lookahead));
}

static js_Ast *caselist(js_State *J)
{
	js_Ast *head, *tail;
	if (J->lookahead == '}')
		return NULL;
	head = tail = LIST(caseclause(J));
	while (J->lookahead != '}')
		tail = tail->b = LIST(caseclause(J));
	return jsP_list(head);
}

static js_Ast *block(js_State *J)
{
	js_Ast *a;
	int line = J->lexline;
	jsP_expect(J, '{');
	blockbegin(J);
	a = statementlist(J);
	blockend(J);
	jsP_expect(J, '}');
	return STM1(BLOCK, a);
}

static js_Ast *forexpression(js_State *J, int end)
{
	js_Ast *a = NULL;
	if (J->lookahead != end)
		a = expression(J, 0);
	jsP_expect(J, end);
	return a;
}

/* for (head of expr) body: over %arr(expr) by index; head is a var list (one EXP_VAR, its a
 * the target or pattern) or an expression (target or pattern) */
static js_Ast *forof(js_State *J, int line, js_Ast *head, int decl, int isawait)
{
	jsP_Build init = { 0, 0 }, steps = { 0, 0 }, body = { 0, 0 };
	const char *arr = newtemp(J), *i = newtemp(J);
	js_Ast *e, *bind, *val, *stm;
	e = assignment(J, 0);
	jsP_expect(J, ')');
	if (isawait) { /* for (var %it = %aiter(e), %r; !(%r = await %it.next()).done;) { x = await %r.value } */
		bpush(J, &init, EXP2(VAR, idast(J, arr), helpercall(J, "%aiter", list1(J, e))));
		bpush(J, &init, EXP1(VAR, idast(J, i)));
		val = EXP1(AWAIT, EXP2(MEMBER, idexp(J, i), idast(J, "value")));
	} else {
		bpush(J, &init, EXP2(VAR, idast(J, arr), helpercall(J, "%arr", list1(J, e))));
		bpush(J, &init, EXP2(VAR, idast(J, i), numexp(J, 0)));
		val = EXP2(INDEX, idexp(J, arr), idexp(J, i));
	}
	if (decl) {
		if (head->b)
			jsP_error(J, "more than one loop variable in for-of statement");
		destructure(J, &steps, 1, head->a->a, val);
		bind = STM1(VAR, jsP_list(steps.head));
	} else if (head->type == EXP_ARRAY || head->type == EXP_OBJECT) {
		destructure(J, &steps, 0, head, val);
		bind = dsexpression(J, &steps, numexp(J, 0));
	} else {
		bind = EXP2(ASS, head, val);
	}
	bpush(J, &body, bind);
	stm = statement(J);
	bpush(J, &body, stm);
	if (isawait)
		return STM4(FOR_VAR, jsP_list(init.head),
			EXP1(LOGNOT, EXP2(MEMBER, EXP2(ASS, idexp(J, i), EXP1(AWAIT, EXP2(CALL, EXP2(MEMBER, idexp(J, arr), idast(J, "next")), NULL))), idast(J, "done"))),
			NULL,
			STM1(BLOCK, jsP_list(body.head)));
	return STM4(FOR_VAR, jsP_list(init.head),
		EXP2(LT, idexp(J, i), EXP2(MEMBER, idexp(J, arr), idast(J, "length"))),
		EXP1(PREINC, idexp(J, i)),
		STM1(BLOCK, jsP_list(body.head)));
}

static js_Ast *forstatement(js_State *J, int line)
{
	js_Ast *a, *b, *c, *d;
	int kind = 0, isawait = 0;
	if (J->es_async && isword(J, "await")) { /* for await (x of asyncIterable) */
		jsP_next(J);
		isawait = 1;
	}
	jsP_expect(J, '(');
	if (islexical(J)) {
		jsP_next(J);
		kind = 1;
	}
	if (kind || jsP_accept(J, TK_VAR)) {
		a = vardeclist(J, 1, kind);
		if (isword(J, "of")) {
			jsP_next(J);
			return forof(J, line, a, 1, isawait);
		}
		if (a->a->a->type != AST_IDENTIFIER)
			jsP_error(J, "destructuring in for-in is not supported");
		if (jsP_accept(J, ';')) {
			b = forexpression(J, ';');
			c = forexpression(J, ')');
			d = statement(J);
			return STM4(FOR_VAR, a, b, c, d);
		}
		if (jsP_accept(J, TK_IN)) {
			b = expression(J, 0);
			jsP_expect(J, ')');
			c = statement(J);
			return STM3(FOR_IN_VAR, a, b, c);
		}
		jsP_error(J, "unexpected token in for-var-statement: %s", jsY_tokenstring(J->lookahead));
	}

	if (J->lookahead != ';') {
		if (J->lookahead == '[' || J->lookahead == '{') /* a pattern: not destructured yet */
			a = conditional(J, 1);
		else
			a = expression(J, 1);
		if (isword(J, "of")) {
			jsP_next(J);
			return forof(J, line, a, 0, isawait);
		}
	} else
		a = NULL;
	if (jsP_accept(J, ';')) {
		b = forexpression(J, ';');
		c = forexpression(J, ')');
		d = statement(J);
		return STM4(FOR, a, b, c, d);
	}
	if (jsP_accept(J, TK_IN)) {
		b = expression(J, 0);
		jsP_expect(J, ')');
		c = statement(J);
		return STM3(FOR_IN, a, b, c);
	}
	jsP_error(J, "unexpected token in for-statement: %s", jsY_tokenstring(J->lookahead));
}

static js_Ast *statement(js_State *J)
{
	js_Ast *a, *b, *c, *d;
	js_Ast *stm;
	int line = J->lexline;

	INCREC();

	if (J->lookahead == '{') {
		stm = block(J);
	}

	else if (jsP_accept(J, TK_VAR)) {
		a = vardeclist(J, 0, 0);
		semicolon(J);
		stm = STM1(VAR, a);
	}

	else if (islexical(J)) {
		jsP_next(J);
		a = vardeclist(J, 0, 1);
		semicolon(J);
		stm = STM1(VAR, a);
	}

	else if (isword(J, "class") && jsP_peek(J, NULL) == TK_IDENTIFIER) {
		jsP_next(J);
		a = identifier(J);
		declname(J, a, 1);
		stm = STM1(VAR, LIST(EXP2(VAR, a, classexp(J, line, a))));
	}

	else if (isword(J, "async") && J->lookahead == TK_IDENTIFIER) {
		int nl, t = jsP_peek(J, &nl);
		if (t == TK_FUNCTION && !nl) {
			jsP_next(J);
			jsP_next(J);
			stm = funstm(J, line, 1);
		} else {
			stm = expression(J, 0);
			semicolon(J);
		}
	}

	/* empty statement */
	else if (jsP_accept(J, ';')) {
		stm = STM0(EMPTY);
	}

	else if (jsP_accept(J, TK_IF)) {
		jsP_expect(J, '(');
		a = expression(J, 0);
		jsP_expect(J, ')');
		b = statement(J);
		if (jsP_accept(J, TK_ELSE))
			c = statement(J);
		else
			c = NULL;
		stm = STM3(IF, a, b, c);
	}

	else if (jsP_accept(J, TK_DO)) {
		a = statement(J);
		jsP_expect(J, TK_WHILE);
		jsP_expect(J, '(');
		b = expression(J, 0);
		jsP_expect(J, ')');
		jsP_accept(J, ';'); /* (ES2015: inserted after do-while even on the same line) */
		stm = STM2(DO, a, b);
	}

	else if (jsP_accept(J, TK_WHILE)) {
		jsP_expect(J, '(');
		a = expression(J, 0);
		jsP_expect(J, ')');
		b = statement(J);
		stm = STM2(WHILE, a, b);
	}

	else if (jsP_accept(J, TK_FOR)) {
		blockbegin(J);
		stm = forstatement(J, line);
		blockend(J);
	}

	else if (jsP_accept(J, TK_CONTINUE)) {
		a = identifieropt(J);
		semicolon(J);
		stm = STM1(CONTINUE, a);
	}

	else if (jsP_accept(J, TK_BREAK)) {
		a = identifieropt(J);
		semicolon(J);
		stm = STM1(BREAK, a);
	}

	else if (jsP_accept(J, TK_RETURN)) {
		if (J->lookahead != ';' && J->lookahead != '}' && J->lookahead != 0)
			a = expression(J, 0);
		else
			a = NULL;
		semicolon(J);
		stm = STM1(RETURN, a);
	}

	else if (jsP_accept(J, TK_WITH)) {
		jsP_expect(J, '(');
		a = expression(J, 0);
		jsP_expect(J, ')');
		b = statement(J);
		stm = STM2(WITH, a, b);
	}

	else if (jsP_accept(J, TK_SWITCH)) {
		jsP_expect(J, '(');
		a = expression(J, 0);
		jsP_expect(J, ')');
		jsP_expect(J, '{');
		blockbegin(J);
		b = caselist(J);
		blockend(J);
		jsP_expect(J, '}');
		stm = STM2(SWITCH, a, b);
	}

	else if (jsP_accept(J, TK_THROW)) {
		a = expression(J, 0);
		semicolon(J);
		stm = STM1(THROW, a);
	}

	else if (jsP_accept(J, TK_TRY)) {
		a = block(J);
		b = c = d = NULL;
		if (jsP_accept(J, TK_CATCH)) {
			blockbegin(J);
			if (jsP_accept(J, '(')) {
				if (J->lookahead == TK_IDENTIFIER) {
					b = identifier(J);
					declname(J, b, 0);
					jsP_expect(J, ')');
					c = block(J);
				} else { /* catch ({message}) */
					jsP_Build steps = { 0, 0 };
					js_Ast *pat = bindingtarget(J, 1);
					b = idast(J, hidname(J, "e"));
					jsP_expect(J, ')');
					destructure(J, &steps, 1, pat, idexp(J, b->string));
					c = block(J);
					c->a = jsP_list(jsP_newnode(J, AST_LIST, 0, STM1(VAR, jsP_list(steps.head)), c->a, 0, 0));
				}
			} else { /* catch { } */
				b = idast(J, hidname(J, "e"));
				c = block(J);
			}
			blockend(J);
		}
		if (jsP_accept(J, TK_FINALLY)) {
			d = block(J);
		}
		if (!b && !d)
			jsP_error(J, "unexpected token in try: %s (expected 'catch' or 'finally')", jsY_tokenstring(J->lookahead));
		stm = STM4(TRY, a, b, c, d);
	}

	else if (jsP_accept(J, TK_DEBUGGER)) {
		semicolon(J);
		stm = STM0(DEBUGGER);
	}

	else if (jsP_accept(J, TK_FUNCTION)) {
		stm = funstm(J, line, 0);
	}

	/* labelled statement or expression statement */
	else if (J->lookahead == TK_IDENTIFIER) {
		a = expression(J, 0);
		if (a->type == EXP_IDENTIFIER && jsP_accept(J, ':')) {
			a->type = AST_IDENTIFIER;
			b = statement(J);
			stm = STM2(LABEL, a, b);
		} else {
			semicolon(J);
			stm = a;
		}
	}

	/* expression statement */
	else {
		stm = expression(J, 0);
		semicolon(J);
	}

	DECREC();
	return stm;
}

/* Browser 9300: modules (jsP_parsemodule). The module is a function (%ns, %meta): its imports
 * first (var %mN = %imp(spec, %meta): the imported module's namespace, that module run first),
 * then %impcheck(%meta) (a module not fetched yet stops this one), the imported bindings copied
 * from the namespaces, the exports (getters on %ns, so they stay live), then the body. */
static js_Ast *scriptelement(js_State *J);

static void mpush(js_State *J, js_Ast **h, js_Ast **t, js_Ast *stm)
{
	jsP_Build b;
	b.head = *h; b.tail = *t;
	bpush(J, &b, stm);
	*h = b.head; *t = b.tail;
}

static js_Ast *modspec(js_State *J)
{
	js_Ast *a;
	if (J->lookahead != TK_STRING)
		jsP_error(J, "unexpected token: %s (expected a module name)", jsY_tokenstring(J->lookahead));
	a = jsP_newstrnode(J, EXP_STRING, J->text);
	jsP_next(J);
	/* import attributes (with { type: 'json' }): read past */
	if (J->lookahead == TK_WITH || isword(J, "assert")) {
		jsP_next(J);
		jsP_expect(J, '{');
		while (J->lookahead != '}' && J->lookahead != 0)
			jsP_next(J);
		jsP_expect(J, '}');
	}
	return a;
}

static void expectword(js_State *J, const char *w)
{
	if (!isword(J, w))
		jsP_error(J, "unexpected token: %s (expected '%s')", jsY_tokenstring(J->lookahead), w);
	jsP_next(J);
}

/* a name in an import / export list: an identifier, a keyword (default) or a string */
static const char *modname(js_State *J)
{
	const char *n;
	if (J->lookahead == TK_STRING || J->lookahead == TK_IDENTIFIER || J->lookahead >= TK_BREAK) {
		n = J->lookahead >= TK_BREAK && J->lookahead != TK_STRING && J->lookahead != TK_IDENTIFIER ? jsY_tokenstring(J->lookahead) : J->text;
		n = js_intern(J, n);
		jsP_next(J);
		return n;
	}
	jsP_error(J, "unexpected token: %s (expected a name)", jsY_tokenstring(J->lookahead));
	return NULL;
}

static const char *modimport(js_State *J, js_Ast *spec)
{
	int line = J->lexline;
	const char *m = hidname(J, "m");
	mpush(J, &J->es_imph, &J->es_impt, STM1(VAR, LIST(EXP2(VAR, idast(J, m),
		helpercall(J, "%imp", list2(J, spec, idexp(J, "%meta")))))));
	return m;
}

static void modexport(js_State *J, const char *name, js_Ast *val)
{
	int line = J->lexline;
	jsP_Build a = { 0, 0 };
	bpush(J, &a, idexp(J, "%ns"));
	bpush(J, &a, strexp(J, name));
	bpush(J, &a, EXP3(FUN, NULL, NULL, LIST(STM1(RETURN, val))));
	mpush(J, &J->es_exph, &J->es_expt, helpercall(J, "%exp", a.head));
}

#define MOD_MAX 256

static js_Ast *importstm(js_State *J)
{
	int line = J->lexline, n = 0, i;
	const char *keys[MOD_MAX];	/* NULL: the namespace */
	js_Ast *locals[MOD_MAX];
	const char *m;
	if (J->lookahead == TK_STRING) {
		modimport(J, modspec(J));
		semicolon(J);
		return STM0(EMPTY);
	}
	if (J->lookahead == TK_IDENTIFIER) {
		keys[n] = js_intern(J, "default");
		locals[n++] = identifier(J);
		if (!jsP_accept(J, ','))
			goto from;
	}
	if (jsP_accept(J, '*')) {
		expectword(J, "as");
		keys[n] = NULL;
		locals[n++] = identifier(J);
	} else {
		jsP_expect(J, '{');
		while (J->lookahead != '}') {
			const char *k = modname(J);
			if (n >= MOD_MAX)
				jsP_error(J, "too many names in an import");
			keys[n] = k;
			if (isword(J, "as")) {
				jsP_next(J);
				locals[n++] = identifier(J);
			} else {
				locals[n] = idast(J, k);
				n++;
			}
			if (!jsP_accept(J, ','))
				break;
		}
		jsP_expect(J, '}');
	}
from:
	expectword(J, "from");
	m = modimport(J, modspec(J));
	semicolon(J);
	for (i = 0; i < n; i++) {	/* (live: each read is the namespace's property) */
		js_Ast *b = jsP_newnode(J, AST_IDENTIFIER, 0, 0, 0, 0, 0);
		b->string = locals[i]->string;
		b->number = J->es_level;
		b->a = idast(J, m);
		b->c = keys[i] ? strexp(J, keys[i]) : NULL;
		b->b = J->es_names;
		J->es_names = b;
		J->es_renamed++;
	}
	return STM0(EMPTY);
}

static js_Ast *exportstm(js_State *J)
{
	int line = J->lexline, n = 0, i;
	js_Ast *old, *stm, *nm;
	if (jsP_accept(J, '*')) {
		const char *as = NULL, *m;
		if (isword(J, "as")) {
			jsP_next(J);
			as = modname(J);
		}
		expectword(J, "from");
		m = modimport(J, modspec(J));
		semicolon(J);
		if (as)
			modexport(J, as, idexp(J, m));
		else
			mpush(J, &J->es_exph, &J->es_expt, helpercall(J, "%expall", list2(J, idexp(J, "%ns"), idexp(J, m))));
		return STM0(EMPTY);
	}
	if (jsP_accept(J, '{')) {
		const char *loc[MOD_MAX], *exp[MOD_MAX], *m = NULL;
		while (J->lookahead != '}') {
			if (n >= MOD_MAX)
				jsP_error(J, "too many names in an export");
			loc[n] = modname(J);
			if (isword(J, "as")) {
				jsP_next(J);
				exp[n] = modname(J);
			} else {
				exp[n] = loc[n];
			}
			n++;
			if (!jsP_accept(J, ','))
				break;
		}
		jsP_expect(J, '}');
		if (isword(J, "from")) {
			jsP_next(J);
			m = modimport(J, modspec(J));
		}
		semicolon(J);
		for (i = 0; i < n; i++)
			modexport(J, exp[i], m ? EXP2(INDEX, idexp(J, m), strexp(J, loc[i])) : nameexp(J, loc[i]));
		return STM0(EMPTY);
	}
	if (jsP_accept(J, TK_DEFAULT)) {
		int async = 0;
		if (isword(J, "async")) {
			int nl, t = jsP_peek(J, &nl);
			if (t == TK_FUNCTION && !nl) {
				jsP_next(J);
				async = 1;
			}
		}
		if (J->lookahead == TK_FUNCTION) {
			int t = jsP_peek(J, NULL);
			if (t == TK_IDENTIFIER || (t == '*' && 0)) {
				jsP_next(J);
				stm = fundec(J, line, async);
				modexport(J, "default", idexp(J, stm->a->string));
				return stm;
			}
			if (t == '*') {
				jsP_LexState ls;
				lexsave(J, &ls);
				jsP_next(J);
				jsP_next(J);
				t = J->lookahead;
				lexrestore(J, &ls);
				if (t == TK_IDENTIFIER) {
					jsP_next(J);
					stm = fundec(J, line, async);
					modexport(J, "default", idexp(J, stm->a->string));
					return stm;
				}
			}
			jsP_next(J);
			nm = funexp(J, line, async);
		} else if (!async && isword(J, "class") && jsP_peek(J, NULL) == TK_IDENTIFIER) {
			jsP_next(J);
			nm = identifier(J);
			declname(J, nm, 0);
			modexport(J, "default", idexp(J, nm->string));
			return STM1(VAR, LIST(EXP2(VAR, nm, classexp(J, line, nm))));
		} else {
			nm = assignment(J, 0);
			semicolon(J);
		}
		modexport(J, "default", idexp(J, "%default"));
		return STM1(VAR, LIST(EXP2(VAR, idast(J, "%default"), nm)));
	}
	/* export var / let / const / function / class: the names it declares */
	old = J->es_names;
	stm = scriptelement(J);
	for (nm = J->es_names; nm && nm != old; nm = nm->b)
		modexport(J, nm->string, idexp(J, nm->a ? nm->a->string : nm->string));
	return stm;
}

/* Program */

static js_Ast *scriptelement(js_State *J)
{
	int line = J->lexline;
	if (J->es_module && J->es_level == J->es_modlevel && J->lookahead == TK_IDENTIFIER) {
		if (!strcmp(J->text, "import")) {
			int t = jsP_peek(J, NULL);
			if (t != '(' && t != '.') {
				jsP_next(J);
				return importstm(J);
			}
		} else if (!strcmp(J->text, "export")) {
			jsP_next(J);
			return exportstm(J);
		}
	}
	if (jsP_accept(J, TK_FUNCTION))
		return fundec(J, line, 0);
	if (isword(J, "async")) {
		int nl, t = jsP_peek(J, &nl);
		if (t == TK_FUNCTION && !nl) {
			jsP_next(J);
			jsP_next(J);
			return fundec(J, line, 1);
		}
	}
	return statement(J);
}

static js_Ast *script(js_State *J, int terminator)
{
	js_Ast *head, *tail;
	if (J->lookahead == terminator)
		return NULL;
	head = tail = LIST(scriptelement(J));
	while (J->lookahead != terminator)
		tail = tail->b = LIST(scriptelement(J));
	return jsP_list(head);
}

static js_Ast *funbody(js_State *J)
{
	js_Ast *a;
	jsP_expect(J, '{');
	a = script(J, '}');
	jsP_expect(J, '}');
	return a;
}


/* Browser 9300: `a${b}c` -> "a" + b + "c"; tag`a${b}` -> tag(%tpl(site, ["a",""], raw), b) */
static js_Ast *templatelit(js_State *J, js_Ast *tag)
{
	jsP_Build cooked = { 0, 0 }, raw = { 0, 0 }, args = { 0, 0 };
	js_Ast *e = NULL, *x;
	int line = J->lexline, more;

	for (;;) {
		bpush(J, &cooked, strexp(J, J->text));
		bpush(J, &raw, strexp(J, J->es_raw));
		if (!tag && (J->text[0] || !e))
			e = e ? EXP2(ADD, e, strexp(J, J->text)) : strexp(J, J->text);
		more = (int)J->number;
		if (!more)
			break;
		jsP_next(J);
		x = expression(J, 0);
		if (J->lookahead != '}')
			jsP_error(J, "unexpected token in template: %s (expected '}')", jsY_tokenstring(J->lookahead));
		J->lookahead = jsY_lextemplatenext(J);
		if (tag)
			bpush(J, &args, x);
		else
			e = EXP2(ADD, e, x);
	}
	jsP_next(J);
	if (!tag)
		return e;
	x = jsP_newnode(J, AST_LIST, 0, numexp(J, ++J->es_tmpn), 0, 0, 0);
	x->b = jsP_newnode(J, AST_LIST, 0, EXP1(ARRAY, jsP_list(cooked.head)), 0, 0, 0);
	x->b->b = jsP_newnode(J, AST_LIST, 0, EXP1(ARRAY, jsP_list(raw.head)), 0, 0, 0);
	x = jsP_newnode(J, AST_LIST, 0, helpercall(J, "%tpl", jsP_list(x)), args.head, 0, 0);
	return EXP2(CALL, tag, jsP_list(x));
}

/* Browser 9300: a class, after "class" (and its name for a declaration):
 *   (function (%S) { function C(..) {..}  %inherit(C, %S); %defm(C.prototype, "m", function..);
 *     ..; var %F = function () { fields }; return C; })(super)
 * super(..) is %superCall(this, %S, [..], %F); fields start a base class's constructor. */
static js_Ast *classexp(js_State *J, int line, js_Ast *name)
{
	jsP_FunCtx c;
	jsP_Build body = { 0, 0 }, fields = { 0, 0 };
	js_Ast *sup = NULL, *ctor = NULL, *cls, *savecls = J->es_class, *proto, *ctorname, *f, *l, *iife;
	const char *sname = hidname(J, "S"), *fname = hidname(J, "F");
	int derived = 0;

	if (!name && J->lookahead == TK_IDENTIFIER && !isword(J, "extends"))
		name = identifier(J);
	if (isword(J, "extends")) {
		jsP_next(J);
		sup = callexp(J);
		derived = 1;
	}
	ctorname = name ? idast(J, name->string) : idast(J, hidname(J, "C"));

	funbegin(J, &c, 0, 0, 0); /* the wrapper function */
	cls = jsP_newnode(J, AST_IDENTIFIER, line, idast(J, fname), 0, 0, 0);
	cls->string = derived ? sname : js_intern(J, "Object");
	J->es_class = cls;

	jsP_expect(J, '{');
	while (!jsP_accept(J, '}')) {
		int isstatic = 0, gen = 0, async = 0, kind = 0; /* kind: 0 method, 1 get, 2 set, 3 field */
		js_Ast *key, *target, *val;
		line = J->lexline;
		if (jsP_accept(J, ';'))
			continue;
		if (isword(J, "static")) {
			int t = jsP_peek(J, NULL);
			if (t != '(' && t != '=' && t != ';' && t != '}') {
				jsP_next(J);
				isstatic = 1;
				if (J->lookahead == '{') { /* static { ... } */
					jsP_FunCtx sc;
					cls->number = 1;
					funbegin(J, &sc, 0, 0, 0);
					f = EXP3(FUN, NULL, NULL, funend(J, &sc, funbody(J), 0));
					cls->number = 0;
					bpush(J, &body, EXP2(CALL, EXP2(MEMBER, f, idast(J, "call")), list1(J, idexp(J, ctorname->string))));
					continue;
				}
			}
		}
		if (jsP_accept(J, '*'))
			gen = 1;
		if (!gen && (isword(J, "get") || isword(J, "set") || isword(J, "async"))) {
			int t = jsP_peek(J, NULL);
			if (t != '(' && t != '=' && t != ';' && t != '}') {
				if (isword(J, "async"))
					async = 1;
				else
					kind = J->text[0] == 'g' ? 1 : 2;
				jsP_next(J);
				if (async && jsP_accept(J, '*'))
					gen = 1;
			}
		}
		if (J->lookahead == TK_IDENTIFIER && J->text[0] == '#') { /* #private: a hidden-ish property */
			key = jsP_newstrnode(J, AST_IDENTIFIER, J->text);
			jsP_next(J);
		} else {
			key = propname(J);
		}
		if (J->lookahead != '(')
			kind = 3;

		cls->number = isstatic;
		if (kind == 3) { /* field */
			jsP_FunCtx fc;
			js_Ast *e;
			int iscomputed = key->type != AST_IDENTIFIER && key->type != EXP_STRING && key->type != EXP_NUMBER;
			funbegin(J, &fc, 0, 0, 0);
			e = jsP_accept(J, '=') ? assignment(J, 0) : undefexp(J);
			semicolon(J);
			if (isstatic) {
				target = idexp(J, ctorname->string);
				e = funend(J, &fc, list1(J, STM1(RETURN, e)), 0);
				e = EXP2(CALL, EXP2(MEMBER, EXP3(FUN, NULL, NULL, e), idast(J, "call")), list1(J, idexp(J, ctorname->string)));
			} else {
				target = EXP0(THIS);
			}
			if (key->type == AST_IDENTIFIER)
				val = strexp(J, key->string);
			else
				val = key;
			if (iscomputed && !isstatic) { /* evaluated once, when the class is made */
				const char *k = newtemp(J);
				bpush(J, &body, EXP2(ASS, idexp(J, k), key));
				val = idexp(J, k);
			}
			l = jsP_newnode(J, AST_LIST, 0, target, 0, 0, 0);
			l->b = jsP_newnode(J, AST_LIST, 0, val, 0, 0, 0);
			l->b->b = jsP_newnode(J, AST_LIST, 0, e, 0, 0, 0);
			e = helpercall(J, "%defp", jsP_list(l));
			if (isstatic) {
				bpush(J, &body, e);
			} else {
				js_Ast *stms = funend(J, &fc, list1(J, e), 0);
				for (l = stms; l; l = l->b)
					bpush(J, &fields, l->a);
			}
			continue;
		}

		if (!isstatic && kind == 0 && key->type == AST_IDENTIFIER && !strcmp(key->string, "constructor")) {
			cls->number = 0;
			ctor = funrest(J, line, NULL, 0, 0);
			continue;
		}
		f = funrest(J, line, NULL, gen, async);
		cls->number = 0;
		proto = isstatic ? idexp(J, ctorname->string) : EXP2(MEMBER, idexp(J, ctorname->string), idast(J, "prototype"));
		l = jsP_newnode(J, AST_LIST, 0, proto, 0, 0, 0);
		l->b = jsP_newnode(J, AST_LIST, 0, key->type == AST_IDENTIFIER ? strexp(J, key->string) : key, 0, 0, 0);
		if (kind == 0) {
			l->b->b = jsP_newnode(J, AST_LIST, 0, f, 0, 0, 0);
			bpush(J, &body, helpercall(J, "%defm", jsP_list(l)));
		} else {
			l->b->b = jsP_newnode(J, AST_LIST, 0, kind == 1 ? f : EXP0(NULL), 0, 0, 0);
			l->b->b->b = jsP_newnode(J, AST_LIST, 0, kind == 2 ? f : EXP0(NULL), 0, 0, 0);
			bpush(J, &body, helpercall(J, "%defa", jsP_list(l)));
		}
	}
	J->es_class = savecls;

	/* the constructor */
	if (!ctor) {
		jsP_FunCtx fc;
		js_Ast *b = NULL;
		funbegin(J, &fc, 0, 0, 0);
		if (derived) {
			l = jsP_newnode(J, AST_LIST, 0, EXP0(THIS), 0, 0, 0);
			l->b = jsP_newnode(J, AST_LIST, 0, idexp(J, sname), 0, 0, 0);
			l->b->b = jsP_newnode(J, AST_LIST, 0, idexp(J, "arguments"), 0, 0, 0);
			l->b->b->b = jsP_newnode(J, AST_LIST, 0, idexp(J, fname), 0, 0, 0);
			b = list1(J, helpercall(J, "%superCall", jsP_list(l)));
		}
		b = funend(J, &fc, b, 0);
		ctor = EXP3(FUN, NULL, NULL, b);
	}
	if (!derived && fields.head) /* fields first in a base class's constructor */
		ctor->c = jsP_list(jsP_newnode(J, AST_LIST, 0,
			STM3(IF, idexp(J, fname), EXP2(CALL, EXP2(MEMBER, idexp(J, fname), idast(J, "call")), list1(J, EXP0(THIS))), NULL),
			ctor->c, 0, 0));

	{
		jsP_Build w = { 0, 0 };
		js_Ast *fn = jsP_newnode(J, AST_FUNDEC, line, ctorname, ctor->b, ctor->c, 0);
		bpush(J, &w, fn);
		bpush(J, &w, STM1(VAR, list1(J, EXP2(VAR, idast(J, fname), fields.head ? EXP3(FUN, NULL, NULL, jsP_list(fields.head)) : EXP0(NULL)))));
		if (derived)
			bpush(J, &w, helpercall(J, "%inherit", list2(J, idexp(J, ctorname->string), idexp(J, sname))));
		for (l = body.head; l; l = l->b)
			bpush(J, &w, l->a);
		bpush(J, &w, STM1(RETURN, idexp(J, ctorname->string)));
		f = funend(J, &c, jsP_list(w.head), 0);
	}
	iife = EXP3(FUN, NULL, derived ? list1(J, idast(J, sname)) : NULL, f);
	return EXP2(CALL, iife, derived ? list1(J, sup) : NULL);
}

/* Constant folding */

static int toint32(double d)
{
	double two32 = 4294967296.0;
	double two31 = 2147483648.0;

	if (!isfinite(d) || d == 0)
		return 0;

	d = fmod(d, two32);
	d = d >= 0 ? floor(d) : ceil(d) + two32;
	if (d >= two31)
		return d - two32;
	else
		return d;
}

static unsigned int touint32(double d)
{
	return (unsigned int)toint32(d);
}

static int jsP_setnumnode(js_Ast *node, double x)
{
	node->type = EXP_NUMBER;
	node->number = x;
	node->a = node->b = node->c = node->d = NULL;
	return 1;
}

static int jsP_foldconst(js_Ast *node)
{
	double x, y;
	int a, b;

	if (node->type == AST_LIST) {
		while (node) {
			jsP_foldconst(node->a);
			node = node->b;
		}
		return 0;
	}

	if (node->type == EXP_NUMBER)
		return 1;

	a = node->a ? jsP_foldconst(node->a) : 0;
	b = node->b ? jsP_foldconst(node->b) : 0;
	if (node->c) jsP_foldconst(node->c);
	if (node->d) jsP_foldconst(node->d);

	if (a) {
		x = node->a->number;
		switch (node->type) {
		default: break;
		case EXP_NEG: return jsP_setnumnode(node, -x);
		case EXP_POS: return jsP_setnumnode(node, x);
		case EXP_BITNOT: return jsP_setnumnode(node, ~toint32(x));
		}

		if (b) {
			y = node->b->number;
			switch (node->type) {
			default: break;
			case EXP_MUL: return jsP_setnumnode(node, x * y);
			case EXP_DIV: return jsP_setnumnode(node, x / y);
			case EXP_MOD: return jsP_setnumnode(node, fmod(x, y));
			case EXP_ADD: return jsP_setnumnode(node, x + y);
			case EXP_SUB: return jsP_setnumnode(node, x - y);
			case EXP_SHL: return jsP_setnumnode(node, (int)((unsigned int)toint32(x) << (touint32(y) & 0x1F)));
			case EXP_SHR: return jsP_setnumnode(node, toint32(x) >> (touint32(y) & 0x1F));
			case EXP_USHR: return jsP_setnumnode(node, touint32(x) >> (touint32(y) & 0x1F));
			case EXP_BITAND: return jsP_setnumnode(node, toint32(x) & toint32(y));
			case EXP_BITXOR: return jsP_setnumnode(node, toint32(x) ^ toint32(y));
			case EXP_BITOR: return jsP_setnumnode(node, toint32(x) | toint32(y));
			}
		}
	}

	return 0;
}

/* Main entry point */

js_Ast *jsP_parse(js_State *J, const char *filename, const char *source)
{
	js_Ast *p;

	jsP_FunCtx c;

	jsY_initlex(J, filename, source);
	J->es_arrow = J->es_this = J->es_args = 0;
	J->es_tmps = J->es_pro = J->es_names = J->es_class = NULL;
	J->es_level = J->es_block = J->es_renamed = J->es_gen = J->es_async = 0;
	jsP_next(J);
	J->astdepth = 0;
	funbegin(J, &c, 0, 0, 0);
	p = script(J, 0);
	p = funend(J, &c, p, 0);
	if (p)
		jsP_foldconst(p);

	return p;
}

/* Browser 9300: a module: a script whose value is its function (%ns, %meta) */
js_Ast *jsP_parsemodule(js_State *J, const char *filename, const char *source)
{
	jsP_FunCtx c0, c1;
	jsP_Build b = { 0, 0 };
	js_Ast *body, *l, *f, *p;
	int line;
	jsY_initlex(J, filename, source);
	J->es_arrow = J->es_this = J->es_args = 0;
	J->es_tmps = J->es_pro = J->es_names = J->es_class = NULL;
	J->es_level = J->es_block = J->es_renamed = J->es_gen = J->es_async = 0;
	J->es_imph = J->es_impt = J->es_bndh = J->es_bndt = J->es_exph = J->es_expt = NULL;
	jsP_next(J);
	J->astdepth = 0;
	line = J->lexline;
	funbegin(J, &c0, 0, 0, 0);
	funbegin(J, &c1, 0, 0, 1);	/* (async: top-level await) */
	J->es_module = 1;
	J->es_modlevel = J->es_level;
	if (js_try(J)) {
		J->es_module = 0;
		js_throw(J);
	}
	body = script(J, 0);
	js_endtry(J);
	J->es_module = 0;
	for (l = J->es_imph; l; l = l->b)
		bpush(J, &b, l->a);
	bpush(J, &b, helpercall(J, "%impcheck", LIST(idexp(J, "%meta"))));
	for (l = J->es_bndh; l; l = l->b)
		bpush(J, &b, l->a);
	for (l = J->es_exph; l; l = l->b)
		bpush(J, &b, l->a);
	for (l = body; l; l = l->b)
		bpush(J, &b, l->a);
	body = funend(J, &c1, jsP_list(b.head), 0);
	f = EXP3(FUN, NULL, list2(J, idast(J, "%ns"), idast(J, "%meta")), body);
	f->number = 2;	/* async */
	p = funend(J, &c0, LIST(f), 0);
	jsP_foldconst(p);
	return p;
}

js_Ast *jsP_parsefunction(js_State *J, const char *filename, const char *params, const char *body)
{
	js_Ast *p = NULL;
	int line = 0;
	if (params) {
		jsY_initlex(J, filename, params);
		J->es_arrow = J->es_this = J->es_args = 0;
		J->es_tmps = J->es_pro = J->es_names = J->es_class = NULL;
		J->es_level = J->es_block = J->es_renamed = J->es_gen = J->es_async = 0;
		jsP_next(J);
		J->astdepth = 0;
		p = parameters(J);
	}
	return EXP3(FUN, NULL, p, jsP_parse(J, filename, body));
}
