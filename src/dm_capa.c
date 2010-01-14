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
#include "dm_capa.h"

#define THIS_MODULE "CAPA"

/*
 */

#define T Capa_T

#define MAX_CAPASIZE 1024

struct T {
	const char capabilities[MAX_CAPASIZE];
	const char initial_set[MAX_CAPASIZE];
	GList *current_set;
};

static GList *capa_search(T A, const char *c)
{
	return g_list_find_custom(A->current_set, c, (GCompareFunc)strcasecmp);
}

T Capa_new(void)
{
	field_t val;
	T A;
	A = g_malloc0(sizeof(*A));
	char **v;

	GETCONFIGVALUE("capability", "IMAP", val);
	if (strlen(val) > 0)
		strncpy((char *)A->initial_set,val,MAX_CAPASIZE-1);
	else
		strncpy((char *)A->initial_set,IMAP_CAPABILITY_STRING,MAX_CAPASIZE-1);

	A->current_set = NULL; //g_string_split(A->initial_set, " ");
	v = g_strsplit(A->initial_set, " ", -1);
	while (*v) {
		A->current_set = g_list_append(A->current_set, *v++);
	}

	return A;
}

const gchar * Capa_as_string(T A)
{
	GString *t = g_list_join(A->current_set, " ");
	strncpy((char *)A->capabilities, t->str, MAX_CAPASIZE-1);
	g_string_free(t, TRUE);
	return A->capabilities;
}

gboolean Capa_match(T A, const char *c)
{
	return capa_search(A, c)?TRUE:FALSE;
}

void Capa_add(T A, const char *c)
{
	A->current_set = g_list_append(A->current_set, g_strdup(c));
}

void Capa_remove(T A, const char * c)
{
	GList *element = capa_search(A, c);
	if (element) {
		A->current_set = g_list_remove_link(A->current_set, element);
		g_list_destroy(element);
	}
}

void Capa_free(T *A)
{
	T c = *A;
	if (c->current_set) g_list_destroy(c->current_set);
	if (c) g_free(c);
	c = NULL;
}

