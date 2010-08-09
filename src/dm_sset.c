/*
  
 Copyright (c) 2008 NFG Net Facilities Group BV support@nfg.nl

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

#include "dm_sset.h"
#include <stdlib.h>
#include <search.h>
#include <assert.h>

#define THIS_MODULE "SSET"

/*
 * implements the Sorted Set interface using standard posix binary trees
 */

#define T Sset_T

struct T {
	void *root;
	int (*cmp)(const void *, const void *);
	int len;
};


T Sset_new(int (*cmp)(const void *a, const void *b))
{
	T S;
	S = calloc(1, sizeof(*S));
	S->root = NULL;
	S->cmp = cmp;
	return S;
}

int Sset_has(T S, const void *a)
{
	if (!S->root) return 0;
	return tfind(a, &(S->root), S->cmp)?1:0;
}


void Sset_add(T S, const void *a)
{
	if (! Sset_has(S, a)) {
		void * t = NULL;
		t = tsearch(a, &(S->root), S->cmp);
		assert(t);
		S->len++;
	}
}

int Sset_len(T S)
{
	return S->len;
}

void * Sset_del(T S, const void * a)
{
	void * t = NULL;
	if ((t = tdelete(a, &(S->root), S->cmp)) != NULL)
		S->len--;
	return t;
}

void Sset_map(T S, void (*func)(const void *))
{
	return;
}

void Sset_free(T *S)
{
	T s = *S;
	if (s) free(s);
	s = NULL;
}

T Sset_or(T a, T b) // a + b
{
	return a;
}

T Sset_and(T a, T b) // a * b
{
	return a;
}

T Sset_not(T a, T b) // a - b
{
	return a;
}

T Sset_xor(T a, T b) // a / b
{
	return a;
}


