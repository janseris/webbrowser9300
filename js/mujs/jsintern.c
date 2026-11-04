#include "jsi.h"

/* Dynamically grown string buffer */

void js_putc(js_State *J, js_Buffer **sbp, int c)
{
	js_Buffer *sb = *sbp;
	if (!sb) {
		sb = js_malloc(J, sizeof *sb);
		sb->n = 0;
		sb->m = sizeof sb->s;
		*sbp = sb;
	} else if (sb->n == sb->m) {
		sb = js_realloc(J, sb, (sb->m *= 2) + soffsetof(js_Buffer, s));
		*sbp = sb;
	}
	sb->s[sb->n++] = c;
}

void js_puts(js_State *J, js_Buffer **sb, const char *s)
{
	while (*s)
		js_putc(J, sb, *s++);
}

void js_putm(js_State *J, js_Buffer **sb, const char *s, const char *e)
{
	while (s < e)
		js_putc(J, sb, *s++);
}

/* Browser 9300: a hash table of the interned strings (was an AA-tree: every token of every
 * script walked a dozen strcmps down it and rebalanced on the way back). Chains, doubling. */

struct js_StringNode
{
	js_StringNode *left, *right;	/* (left: the next in the chain) */
	int level;			/* (the hash) */
	char string[1];
};

#define jsS_sentinel (*J->ssent)

static unsigned int jsS_hash(const char *s)
{
	unsigned int h = 2166136261u;
	while (*s)
		h = (h ^ (unsigned char)*s++) * 16777619u;
	return h;
}

static void jsS_grow(js_State *J)
{
	int n = J->stabn ? J->stabn * 2 : 1024, i;
	js_StringNode **t = js_malloc(J, n * sizeof *t);
	for (i = 0; i < n; ++i)
		t[i] = NULL;
	for (i = 0; i < J->stabn; ++i) {
		js_StringNode *node = J->stab[i], *next;
		for (; node; node = next) {
			next = node->left;
			node->left = t[(unsigned int)node->level & (n - 1)];
			t[(unsigned int)node->level & (n - 1)] = node;
		}
	}
	if (J->stab)
		js_free(J, J->stab);
	J->stab = t;
	J->stabn = n;
}

void jsS_dumpstrings(js_State *J)
{
	int i;
	js_StringNode *node;
	printf("interned strings {\n");
	for (i = 0; i < J->stabn; ++i)
		for (node = J->stab[i]; node; node = node->left)
			printf("\t'%s'\n", node->string);
	printf("}\n");
}

/* Browser 9300: the string tree's sentinel, one per state (was a writable static) */
int jsS_initsentinel(js_State *J)
{
	J->ssent = J->alloc(J->actx, NULL, sizeof(js_StringNode));
	if (!J->ssent)
		return 0;
	J->ssent->left = J->ssent->right = J->ssent;
	J->ssent->level = 0;
	J->ssent->string[0] = 0;
	return 1;
}

void jsS_freestrings(js_State *J)
{
	int i;
	for (i = 0; i < J->stabn; ++i) {
		js_StringNode *node = J->stab[i], *next;
		for (; node; node = next) {
			next = node->left;
			js_free(J, node);
		}
	}
	if (J->stab)
		js_free(J, J->stab);
	J->stab = NULL;
	J->stabn = J->stabcount = 0;
}

const char *js_intern(js_State *J, const char *s)
{
	unsigned int h = jsS_hash(s);
	js_StringNode *node;
	size_t n;
	if (J->stabn)
		for (node = J->stab[h & (J->stabn - 1)]; node; node = node->left)
			if ((unsigned int)node->level == h && !strcmp(node->string, s))
				return node->string;
	n = strlen(s);
	if (n > JS_STRLIMIT)
		js_rangeerror(J, "invalid string length");
	if (J->stabcount >= J->stabn)
		jsS_grow(J);
	node = js_malloc(J, soffsetof(js_StringNode, string) + n + 1);
	memcpy(node->string, s, n + 1);
	node->level = (int)h;
	node->left = J->stab[h & (J->stabn - 1)];
	node->right = NULL;
	J->stab[h & (J->stabn - 1)] = node;
	J->stabcount++;
	return node->string;
}
