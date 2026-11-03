#include "jsi.h"

#include <assert.h>

/*
	Use an AA-tree to quickly look up properties in objects:

	The level of every leaf node is one.
	The level of every left child is one less than its parent.
	The level of every right child is equal or one less than its parent.
	The level of every right grandchild is less than its grandparent.
	Every node of level greater than one has two children.

	A link where the child's level is equal to that of its parent is called a horizontal link.
	Individual right horizontal links are allowed, but consecutive ones are forbidden.
	Left horizontal links are forbidden.

	skew() fixes left horizontal links.
	split() fixes consecutive right horizontal links.
*/

#define sentinel (*J->psent)

/* Browser 9300: the property tree's sentinel, one per state (was a writable static) */
int jsV_initsentinel(js_State *J)
{
	J->psent = J->alloc(J->actx, NULL, sizeof(js_Property));
	if (!J->psent)
		return 0;
	memset(J->psent, 0, sizeof(js_Property));
	J->psent->left = J->psent->right = J->psent;
	J->psent->level = 0;
	J->psent->value.t.type = JS_TUNDEFINED;
	/* stackidx's undefined (outside the stack) */
	J->undefval = J->alloc(J->actx, NULL, sizeof(js_Value));
	if (!J->undefval) {
		J->alloc(J->actx, J->psent, 0);
		J->psent = NULL;
		return 0;
	}
	memset(J->undefval, 0, sizeof(js_Value));
	J->undefval->t.type = JS_TUNDEFINED;
	return 1;
}


static js_Property *newproperty(js_State *J, js_Object *obj, const char *name)
{
	int n = strlen(name) + 1;
	js_Property *node = js_malloc(J, offsetof(js_Property, name) + n);
	node->left = node->right = &sentinel;
	node->level = 1;
	node->atts = 0;
	node->value.t.type = JS_TUNDEFINED;
	node->value.u.number = 0;
	node->getter = NULL;
	node->setter = NULL;
	memcpy(node->name, name, n);
	node->onext = NULL;
	node->oprev = obj->otail;
	if (obj->otail)
		obj->otail->onext = node;
	else
		obj->ohead = node;
	obj->otail = node;
	++obj->count;
	++J->gccounter;
	return node;
}

/* Browser 9300: an array index ("0".."4294967294", canonical) */
static int isindexname(const char *s, double *v)
{
	double n = 0;
	if (*s == '0')
		return s[1] == 0 ? (*v = 0, 1) : 0;
	if (!*s)
		return 0;
	for (; *s; ++s) {
		if (*s < '0' || *s > '9')
			return 0;
		n = n * 10 + (*s - '0');
		if (n > 4294967294.0)
			return 0;
	}
	*v = n;
	return 1;
}

/* Browser 9300: the own properties in the order ES2015 enumerates them (integer keys
 * ascending, then the others as they were added); malloc'ed, NULL when none */
js_Property **jsV_ownprops(js_State *J, js_Object *obj, int *np)
{
	js_Property **out, *p, *t;
	double *key, v, kt;
	int n = 0, ni = 0, i, j, gap;
	*np = 0;
	for (p = obj->ohead; p; p = p->onext)
		++n;
	if (!n)
		return NULL;
	out = js_malloc(J, n * sizeof *out);
	key = js_malloc(J, n * sizeof *key);
	for (p = obj->ohead; p; p = p->onext)
		if (isindexname(p->name, &v)) {
			key[ni] = v;
			out[ni++] = p;
		}
	/* shell sort of the integer keys */
	for (gap = ni / 2; gap > 0; gap /= 2)
		for (i = gap; i < ni; ++i)
			for (j = i; j >= gap && key[j-gap] > key[j]; j -= gap) {
				kt = key[j]; key[j] = key[j-gap]; key[j-gap] = kt;
				t = out[j]; out[j] = out[j-gap]; out[j-gap] = t;
			}
	j = ni;
	for (p = obj->ohead; p; p = p->onext)
		if (!isindexname(p->name, &v))
			out[j++] = p;
	js_free(J, key);
	*np = n;
	return out;
}

static js_Property *lookup(js_State *J, js_Property *node, const char *name)
{
	while (node != &sentinel) {
		int c = strcmp(name, node->name);
		if (c == 0)
			return node;
		else if (c < 0)
			node = node->left;
		else
			node = node->right;
	}
	return NULL;
}

static js_Property *skew(js_Property *node)
{
	if (node->left->level == node->level) {
		js_Property *temp = node;
		node = node->left;
		temp->left = node->right;
		node->right = temp;
	}
	return node;
}

static js_Property *split(js_Property *node)
{
	if (node->right->right->level == node->level) {
		js_Property *temp = node;
		node = node->right;
		temp->right = node->left;
		node->left = temp;
		++node->level;
	}
	return node;
}

static js_Property *insert(js_State *J, js_Object *obj, js_Property *node, const char *name, js_Property **result)
{
	if (node != &sentinel) {
		int c = strcmp(name, node->name);
		if (c < 0)
			node->left = insert(J, obj, node->left, name, result);
		else if (c > 0)
			node->right = insert(J, obj, node->right, name, result);
		else
			return *result = node;
		node = skew(node);
		node = split(node);
		return node;
	}
	return *result = newproperty(J, obj, name);
}

static void freeproperty(js_State *J, js_Object *obj, js_Property *node)
{
	if (node->oprev)
		node->oprev->onext = node->onext;
	else
		obj->ohead = node->onext;
	if (node->onext)
		node->onext->oprev = node->oprev;
	else
		obj->otail = node->oprev;
	js_free(J, node);
	--obj->count;
}

static js_Property *unlinkproperty(js_State *J, js_Property *node, const char *name, js_Property **garbage)
{
	js_Property *temp, *a, *b;
	if (node != &sentinel) {
		int c = strcmp(name, node->name);
		if (c < 0) {
			node->left = unlinkproperty(J, node->left, name, garbage);
		} else if (c > 0) {
			node->right = unlinkproperty(J, node->right, name, garbage);
		} else {
			*garbage = node;
			if (node->left == &sentinel && node->right == &sentinel) {
				return &sentinel;
			}
			else if (node->left == &sentinel) {
				a = node->right;
				while (a->left != &sentinel)
					a = a->left;
				b = unlinkproperty(J, node->right, a->name, &temp);
				temp->level = node->level;
				temp->left = node->left;
				temp->right = b;
				node = temp;
			}
			else {
				a = node->left;
				while (a->right != &sentinel)
					a = a->right;
				b = unlinkproperty(J, node->left, a->name, &temp);
				temp->level = node->level;
				temp->left = b;
				temp->right = node->right;
				node = temp;
			}
		}

		if (node->left->level < node->level - 1 || node->right->level < node->level - 1)
		{
			if (node->right->level > --node->level)
				node->right->level = node->level;
			node = skew(node);
			node->right = skew(node->right);
			node->right->right = skew(node->right->right);
			node = split(node);
			node->right = split(node->right);
		}
	}
	return node;
}

static js_Property *deleteproperty(js_State *J, js_Object *obj, js_Property *tree, const char *name)
{
	js_Property *garbage = &sentinel;
	tree = unlinkproperty(J, tree, name, &garbage);
	if (garbage != &sentinel)
		freeproperty(J, obj, garbage);
	return tree;
}

js_Object *jsV_newobject(js_State *J, enum js_Class type, js_Object *prototype)
{
	js_Object *obj = js_malloc(J, sizeof *obj);
	memset(obj, 0, sizeof *obj);
	obj->gcmark = 0;
	obj->gcnext = J->gcobj;
	J->gcobj = obj;
	++J->gccounter;

	obj->type = type;
	obj->properties = &sentinel;
	obj->prototype = prototype;
	obj->extensible = 1;
	return obj;
}

js_Property *jsV_getownproperty(js_State *J, js_Object *obj, const char *name)
{
	return lookup(J, obj->properties, name);
}

js_Property *jsV_getpropertyx(js_State *J, js_Object *obj, const char *name, int *own)
{
	*own = 1;
	do {
		js_Property *ref = lookup(J, obj->properties, name);
		if (ref)
			return ref;
		obj = obj->prototype;
		*own = 0;
	} while (obj);
	return NULL;
}

js_Property *jsV_getproperty(js_State *J, js_Object *obj, const char *name)
{
	do {
		js_Property *ref = lookup(J, obj->properties, name);
		if (ref)
			return ref;
		obj = obj->prototype;
	} while (obj);
	return NULL;
}

static js_Property *jsV_getenumproperty(js_State *J, js_Object *obj, const char *name)
{
	do {
		js_Property *ref = lookup(J, obj->properties, name);
		if (ref && !(ref->atts & JS_DONTENUM))
			return ref;
		obj = obj->prototype;
	} while (obj);
	return NULL;
}

js_Property *jsV_setproperty(js_State *J, js_Object *obj, const char *name)
{
	js_Property *result;

	if (!obj->extensible) {
		result = lookup(J, obj->properties, name);
		if (J->strict && !result)
			js_typeerror(J, "object is non-extensible");
		return result;
	}

	obj->properties = insert(J, obj, obj->properties, name, &result);

	return result;
}

void jsV_delproperty(js_State *J, js_Object *obj, const char *name)
{
	obj->properties = deleteproperty(J, obj, obj->properties, name);
}

/* Flatten hierarchy of enumerable properties into an iterator object */

static js_Iterator *itnewnode(js_State *J, const char *name, js_Iterator *next) {
	int n = strlen(name) + 1;
	js_Iterator *node = js_malloc(J, offsetof(js_Iterator, name) + n);
	node->next = next;
	memcpy(node->name, name, n);
	return node;
}

static js_Iterator *itwalk(js_State *J, js_Iterator *iter, js_Property *prop, js_Object *seen)
{
	if (prop->right != &sentinel)
		iter = itwalk(J, iter, prop->right, seen);
	if (!(prop->atts & JS_DONTENUM)) {
		if (!seen || !jsV_getenumproperty(J, seen, prop->name)) {
			iter = itnewnode(J, prop->name, iter);
		}
	}
	if (prop->left != &sentinel)
		iter = itwalk(J, iter, prop->left, seen);
	return iter;
}

/* Browser 9300: in ES2015 order (the list is built back to front) */
static js_Iterator *itown(js_State *J, js_Iterator *iter, js_Object *obj, js_Object *seen)
{
	int n, i;
	js_Property **v = jsV_ownprops(J, obj, &n);
	for (i = n - 1; i >= 0; --i)
		if (!(v[i]->atts & JS_DONTENUM))
			if (!seen || !jsV_getenumproperty(J, seen, v[i]->name))
				iter = itnewnode(J, v[i]->name, iter);
	if (v)
		js_free(J, v);
	return iter;
}

static js_Iterator *itflatten(js_State *J, js_Object *obj)
{
	js_Iterator *iter = NULL;
	if (obj->prototype)
		iter = itflatten(J, obj->prototype);
	if (obj->properties != &sentinel)
		iter = itown(J, iter, obj, obj->prototype);
	return iter;
}

js_Object *jsV_newiterator(js_State *J, js_Object *obj, int own)
{
	js_Object *io = jsV_newobject(J, JS_CITERATOR, NULL);
	io->u.iter.target = obj;
	io->u.iter.i = 0;
	io->u.iter.n = 0;
	if (own) {
		io->u.iter.head = NULL;
		if (obj->properties != &sentinel)
			io->u.iter.head = itown(J, io->u.iter.head, obj, NULL);
	} else {
		io->u.iter.head = itflatten(J, obj);
	}
	io->u.iter.current = io->u.iter.head;

	if (obj->type == JS_CSTRING)
		io->u.iter.n = obj->u.s.length;

	if (obj->type == JS_CARRAY && obj->u.a.simple)
		io->u.iter.n = obj->u.a.flat_length;

	return io;
}

const char *jsV_nextiterator(js_State *J, js_Object *io)
{
	if (io->type != JS_CITERATOR)
		js_typeerror(J, "not an iterator");
	if (io->u.iter.i < io->u.iter.n) {
		js_itoa(J->scratch, io->u.iter.i);
		io->u.iter.i++;
		return J->scratch;
	}
	while (io->u.iter.current) {
		const char *name = io->u.iter.current->name;
		io->u.iter.current = io->u.iter.current->next;
		if (jsV_getproperty(J, io->u.iter.target, name))
			return name;
	}
	return NULL;
}

/* Walk all the properties and delete them one by one for arrays */

void jsV_resizearray(js_State *J, js_Object *obj, int newlen)
{
	char buf[32];
	const char *s;
	int k;
	assert(!obj->u.a.simple);
	if (newlen < obj->u.a.length) {
		if (obj->u.a.length > obj->count * 2) {
			js_Object *it = jsV_newiterator(J, obj, 1);
			while ((s = jsV_nextiterator(J, it))) {
				k = jsV_numbertointeger(jsV_stringtonumber(J, s));
				if (k >= newlen && !strcmp(s, jsV_numbertostring(J, buf, k)))
					jsV_delproperty(J, obj, s);
			}
		} else {
			for (k = newlen; k < obj->u.a.length; ++k) {
				jsV_delproperty(J, obj, js_itoa(buf, k));
			}
		}
	}
	obj->u.a.length = newlen;
}
