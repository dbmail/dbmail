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

#include "dbmail.h"
#include "dm_sset.h"

#define THIS_MODULE "SSET"

/*
 * implements the Sorted Set interface
 */

#define T SSet_T

struct T {
	GTree *items;
	int (*cmp)(const void *, const void *);
	int (*hash)(const void *);
};


T Sset_new(int (*cmp)(const void *a, const void *b), int (*hash)(const void *c))
{

	T S;
	S = g_malloc0(sizeof(*S));
	S->items = g_tree_new_full((GCompareDataFunc)(cmp), NULL, (GDestroyNotify)g_free, (GDestroyNotify)g_free);
	S->cmp = cmp;
	S->hash = hash;
}

int Sset_has(T S, const void *a)
{
	return g_tree_lookup(S->items, a)?1:0;
}


void Sset_add(T S, const void *a)
{

}

int Sset_len(T S)
{

}

void * Sset_del(T S, const void * a)
{

}

void Sset_free(T *S)
{

}

T Sset_or(T a, T b) // a + b
{

}

T Sset_and(T a, T b) // a * b
{

}

T Sset_not(T a, T b) // a - b
{

}

T Sset_xor(T a, T b); // a / b
{

}


