/*
  
 Copyright (c) 2009-2012 NFG Net Facilities Group BV support@nfg.nl

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
	const char capabilities[MAX_CAPASIZE];
	GList *max_set;
	GList *current_set;
	gboolean dirty;
};

static GList *capa_search(GList *set, const char *c)
{
	return g_list_find_custom(set, c, (GCompareFunc)strcasecmp);
}

static void capa_update(T A)
{
	if (A->dirty) {
		GString *t = g_list_join(A->current_set, " ");
		strncpy((char *)A->capabilities, t->str, MAX_CAPASIZE-1);
		g_string_free(t, TRUE);
		A->dirty = FALSE;
	}
}

T Capa_new(void)
{
	Field_T val;
        char maxcapa[MAX_CAPASIZE];
	T A;
	A = g_malloc0(sizeof(*A));
	char **v, **h;

	memset(&maxcapa,0,sizeof(maxcapa));

	GETCONFIGVALUE("capability", "IMAP", val);
	if (strlen(val) > 0)
		strncpy((char *)maxcapa, val, MAX_CAPASIZE-1);
	else
		strncpy((char *)maxcapa, IMAP_CAPABILITY_STRING, MAX_CAPASIZE-1);

	A->max_set = NULL;
	A->current_set = NULL;

	h = v = g_strsplit(maxcapa, " ", -1);
	while (*v) {
	       A->max_set = g_list_append(A->max_set, *v);
	       A->current_set = g_list_append(A->current_set, *v++);
	}

	g_free(h);

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
	GList *element = capa_search(A->max_set, c);
	if (element) {
		A->current_set = g_list_append(A->current_set, element->data);
		A->dirty = TRUE;
	}
}

void Capa_remove(T A, const char * c)
{
	GList *element = capa_search(A->current_set, c);
	if (element) {
		A->current_set = g_list_remove_link(A->current_set, element);
		g_list_free(element);
		A->dirty = TRUE;
	}
}

void Capa_free(T *A)
{
	T c = *A;
	if (c->current_set) g_list_free(g_list_first(c->current_set));
	if (c->max_set) g_list_destroy(c->max_set);
	if (c) g_free(c);
	c = NULL;
}

