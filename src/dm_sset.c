/*
 Copyright (c) 2010-2013 NFG Net Facilities Group BV support@nfg.nl
 Copyright (c) 2014-2019 Paul J Stevens, The Netherlands, support@nfg.nl
 Copyright (c) 2020-2023 Alan Hicks, Persistent Objects Ltd support@p-o.co.uk

 This program is free software; you can redistribute it and/or 
 modify it under the terms of the GNU General Public License 
 as published by the Free Software Foundation; either 
 version 2 of the License, or (at your option) any later 
 version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <glib.h>

#include "dm_sset.h"
#include "dm_debug.h"

#define THIS_MODULE "SSET"

/*
 * implements the Sorted Set interface using GTree
 */

#define T Sset_T

struct T {
	void *root;
	int (*cmp)(const void *, const void *);
	size_t size; // sizeof key
	void (*free)(void *);
};


static int compare_data(void *a, void *b, void *c)
{
	T S = (T)c;
	return S->cmp(a, b);
}

T Sset_new(int (*cmp)(const void *a, const void *b), size_t size, void (*free)(void *))
{
	T S;
	assert(size > 0);
	S = calloc(1, sizeof(*S));
	S->root = (void *)g_tree_new_full((GCompareDataFunc)compare_data, S, free, NULL);
	S->cmp = cmp;
	S->size = size;
	S->free = free;
	return S;
}

int Sset_has(T S, const void *a)
{
	return g_tree_lookup((GTree *)S->root, a)?1:0;
}


void Sset_add(T S, const void *a)
{
	if (! Sset_has(S, a))
		g_tree_insert((GTree *)S->root, (void *)a, (void *)a);
}

int Sset_len(T S)
{
	return g_tree_nnodes((GTree *)S->root);
}

void Sset_del(T S, const void * a)
{
	void * t = NULL;
	if ((t = g_tree_lookup((GTree *)S->root, a)) != NULL)
		g_tree_remove((GTree *)S->root, a);
}

struct mapper_data {
	int (*func)(void *, void *);
	void *data;
};

static int mapper(void *key, void UNUSED *value, void *data)
{
	struct mapper_data *m = (struct mapper_data *)data;
	return m->func(key, m->data)?1:0;
}

void Sset_map(T S, int (*func)(void *, void *), void *data)
{
	struct mapper_data m;
	m.func = func;
	m.data = data;
	g_tree_foreach((GTree *)S->root, (GTraverseFunc)mapper, &m);
}

void Sset_free(T *S)
{
	T s = *S;
	if (s) {
		g_tree_destroy((GTree *)s->root);
		s->root = NULL;
		free(s);
	}
	s = NULL;
}

static int sset_copy(void *a, void *c)
{
	T t = (T)c;
	if (! Sset_has(t, a)) {
		void * item = malloc(t->size);
		memcpy(item, (const void *)a, t->size);
		Sset_add(t, item);
	}	       
	return 0;
}

T Sset_or(T a, T b) // a + b
{
	T c = Sset_new(a->cmp, a->size, a->free?a->free:free);

	Sset_map(a, sset_copy, c);
	Sset_map(b, sset_copy, c);

	return c;
}

struct sset_match_helper {
	T i;
	T o;
};

static int sset_match_and(void *a, void *c)
{
	struct sset_match_helper *m = (struct sset_match_helper *)c;
	T t = m->o;

	if (Sset_has(m->i, a)) {
		void * item = malloc(t->size);
		memcpy(item, (const void *)a, t->size);
		Sset_add(m->o, item);
	}
	return 0;
}

T Sset_and(T a, T b) // a * b
{
	T c = Sset_new(a->cmp, a->size, a->free?a->free:free);
	T s;
	struct sset_match_helper h;
       	if (Sset_len(a) < Sset_len(b)) {
		s = a;
		h.i = b;
	} else {
		s = b;
		h.i = a;
	}
	h.o = c;

	Sset_map(s, sset_match_and, &h);

	return c;
}

static int sset_match_not(void *a, void *c)
{
	struct sset_match_helper *m = (struct sset_match_helper *)c;
	T t = m->o;

	if (! Sset_has(m->i, a)) {
		void * item = malloc(t->size);
		memcpy(item, (const void *)a, t->size);
		Sset_add(m->o, item);
	}
	return 0;
}

T Sset_not(T a, T b) // a - b
{
	T c = Sset_new(a->cmp, a->size, a->free?a->free:free);
	struct sset_match_helper h;
	h.o = c;

	h.i = b;
	Sset_map(a, sset_match_not, &h);

	return c;
}

T Sset_xor(T a, T b) // a / b
{
	T c = Sset_new(a->cmp, a->size, a->free?a->free:free);
	struct sset_match_helper h;
	h.o = c;

	h.i = b;
	Sset_map(a, sset_match_not, &h);

	h.i = a;
	Sset_map(b, sset_match_not, &h);

	return c;
}


