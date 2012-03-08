/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2004-2011 NFG Net Facilities Group BV support@nfg.nl

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

/* 
 *
 * functions to create lists and add/delete items */

#include "dbmail.h"


void g_list_destroy(GList *l)
{
	if (! l) return;
	l = g_list_first(l);
	g_list_foreach(l,(GFunc)g_free,NULL);

	l = g_list_first(l);
	g_list_free(l);
}




/*
 * return a list of strings (a,b,c,..N)
 */

//FIXME: this needs some cleaning up:
GList *g_list_slices(GList *list, unsigned limit)
{
	unsigned i;
	GList *new = NULL;
	GString *slice;

	list = g_list_first(list);
	
	while(list) {
		slice = g_string_new("");
		g_string_append_printf(slice,"%s",(gchar *)list->data);
		for (i=1; i<limit; i++) {
			list = g_list_next(list);
			if (!list)
				break;
			g_string_append_printf(slice,",%s", (gchar *)list->data);
		}
		new = g_list_append_printf(new, "%s", slice->str);
		g_string_free(slice,TRUE);
		if (! g_list_next(list))
			break;
		list = g_list_next(list);
	}

	return new;
}

GList *g_list_slices_u64(GList *list, unsigned limit)
{
	unsigned i;
	GList *new = NULL;
	GString *slice;

	list = g_list_first(list);
	while(list) {
		slice = g_string_new("");
		g_string_append_printf(slice,"%llu",*(u64_t *)list->data);
		for (i=1; i<limit; i++) {
			if (! g_list_next(list)) 
				break;
			list = g_list_next(list);
			g_string_append_printf(slice,",%llu", *(u64_t *)list->data);
		}
		new = g_list_append_printf(new, "%s", slice->str);
		g_string_free(slice,TRUE);
		if (! g_list_next(list))
			break;
		list = g_list_next(list);
	}

	return new;
}

GList *g_list_dedup(GList *list, GCompareFunc compare_func, int freeitems)
{
	char *lastdata = NULL;

	list = g_list_first(list);
	while (list) {
		if (lastdata && list->data && compare_func(lastdata, list->data) == 0) {
			if (freeitems)
				g_free(list->data);
			list = g_list_delete_link(g_list_previous(list), list);
		} else {
			lastdata = (char *)list->data;
		}
		if (! g_list_next(list))
			break;
		list = g_list_next(list);
	}

	return g_list_first(list);
}

GString * g_list_join(GList * list, const gchar * sep)
{
	GString *string = g_string_new("");
	if (sep == NULL)
		sep="";
	if (list == NULL)
		return string;
	list = g_list_first(list);
	g_string_append_printf(string,"%s",(gchar *)list->data);
	while((list = g_list_next(list))) {
		g_string_append_printf(string,"%s%s", sep,(gchar *)list->data);
		if (! g_list_next(list))
			break;
	}
	return string;	
}
GString * g_list_join_u64(GList * list, const gchar * sep)
{
	u64_t *token;
	GString *string = g_string_new("");
	if (sep == NULL)
		sep="";
	if (list == NULL)
		return string;
	list = g_list_first(list);
	token = (u64_t*)list->data;
	g_string_append_printf(string,"%llu",*token);
	while((list = g_list_next(list))) {
		token = (u64_t*)list->data;
		g_string_append_printf(string,"%s%llu", sep,*token);
		if (! g_list_next(list))
			break;
	}
	return string;	
}

/*
 * append a formatted string to a GList
 */
GList * g_list_append_printf(GList * list, const char * format, ...)
{
	va_list ap, cp;
	va_start(ap, format);
	va_copy(cp, ap);
	list = g_list_append(list, g_strdup_vprintf(format, cp));
	va_end(cp);
	return list;
}


/*
 * a and b are lists of char keys
 * matching is done using func
 * for each key in b, keys are copied into or removed from a and freed
 */

void g_list_merge(GList **a, GList *b, int condition, GCompareFunc func)
{
	gchar *t;

	b = g_list_first(b);

	if (condition == IMAPFA_ADD) {
		while (b) {
			t = (gchar *)b->data;
			if (! g_list_find_custom(*(GList **)a, t, (GCompareFunc)func))
				*(GList **)a = g_list_append(*(GList **)a, g_strdup(t));
			if (! g_list_next(b))
				break;
			b = g_list_next(b);
		}
	}
	if (condition == IMAPFA_REMOVE) {
		GList *el = NULL;

		while (b) {
			*(GList **)a = g_list_first(*(GList **)a);
			t = (gchar *)b->data;
			if ((el = g_list_find_custom(*(GList **)a, t, (GCompareFunc)func)) != NULL) {
				*(GList **)a = g_list_remove_link(*(GList **)a, el);
				g_list_destroy(el);
			}

			if (! g_list_next(b))
				break;
			b = g_list_next(b);
		}
	}
	if (condition == IMAPFA_REPLACE) {
		g_list_destroy(*(GList **)a);
		*(GList **)a = NULL;

		while (b) {
			t = (gchar *)b->data;
			*(GList **)a = g_list_append(*(GList **)a, g_strdup(t));
			if (! g_list_next(b))
				break;
			b = g_list_next(b);
		}
	}


}


