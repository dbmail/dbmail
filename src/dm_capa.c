/*
 Copyright (c) 2004-2013 NFG Net Facilities Group BV support@nfg.nl
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

#include "dbmail.h"
#include "dm_capa.h"

#define THIS_MODULE "CAPA"

/*
 */

#define T Capa_T

#define MAX_CAPASIZE 1024

struct T {
	Mempool_T pool;
	const char capabilities[MAX_CAPASIZE];
	List_T max_set;
	List_T current_set;
	gboolean dirty;
};

static List_T capa_search(List_T set, const char *c)
{
	List_T found = NULL;
	List_T first = p_list_first(set);
	String_T S;
	char *s;
	while (first) {
		S = p_list_data(first);
		s = (char *)p_string_str(S);
		if (strcasecmp(s, c)==0) {
			found = first;
			break;
		}
		first = p_list_next(first);
	}
	return found;
}

static void capa_update(T A)
{
	if (! A->dirty)
		return;

	String_T t = p_string_new(A->pool, "");
	List_T L = p_list_first(A->current_set);
	while (L) {
		String_T S = p_list_data(L);
		char *s = (char *)p_string_str(S);
		p_string_append(t, s);
		L = p_list_next(L);
		if (L)
			p_string_append(t, " ");
	}
	strncpy((char *)A->capabilities, p_string_str(t), MAX_CAPASIZE-1);
	p_string_free(t, TRUE);
	t = NULL;
	A->dirty = FALSE;
}

T Capa_new(Mempool_T pool)
{
	Field_T val;
        char maxcapa[MAX_CAPASIZE];
	T A;
	A = mempool_pop(pool, sizeof(*A));
	A->pool = pool;
	char **v, **h;

	memset(&maxcapa,0,sizeof(maxcapa));

	GETCONFIGVALUE("capability", "IMAP", val);
	if (strlen(val) > 0)
		strncpy((char *)maxcapa, val, MAX_CAPASIZE-1);
	else
		strncpy((char *)maxcapa, IMAP_CAPABILITY_STRING, MAX_CAPASIZE-1);

	A->max_set = p_list_new(A->pool);
	A->current_set = p_list_new(A->pool);

	h = v = g_strsplit(maxcapa, " ", -1);
	while (*v) {
		String_T S = p_string_new(A->pool, *v++);
		A->max_set = p_list_append(A->max_set, S);
		A->current_set = p_list_append(A->current_set, S);
		assert(A->current_set);
	}

	g_strfreev(h);

	A->dirty = TRUE;
	return A;
}

const gchar * Capa_as_string(T A)
{
	capa_update(A);
	return A->capabilities;
}

gboolean Capa_match(T A, const char *c)
{
	return capa_search(A->current_set, c)?TRUE:FALSE;
}

void Capa_add(T A, const char *c)
{
	List_T element = capa_search(A->max_set, c);
	if (element) {
		A->current_set = p_list_append(A->current_set, 
				p_list_data(element));
		assert(A->current_set);
		A->dirty = TRUE;
	}
}

void Capa_remove(T A, const char * c)
{
	List_T element = capa_search(A->current_set, c);
	if (element) {
		A->current_set = p_list_remove(A->current_set, element);
		p_list_free(&element);
		assert(A->current_set);
		A->dirty = TRUE;
	}
}

void Capa_free(T *A)
{
	Mempool_T pool;
	T c = *A;
	List_T curr, first;

	first = p_list_first(c->current_set);
	p_list_free(&first);

	first = p_list_first(c->max_set);
	curr = first;
	while (curr) {
		String_T data = p_list_data(curr);
		p_string_free(data, TRUE);
		curr = p_list_next(curr);
	}
	p_list_free(&first);

	pool = c->pool;
	mempool_push(pool, c, sizeof(*c));
	c = NULL;
}

