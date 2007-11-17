/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2004-2006 NFG Net Facilities Group BV support@nfg.nl

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
/*
 * return a list of strings (a,b,c,..N)
 */

//FIXME: this needs some cleaning up:
GList *g_list_slices(GList *list, unsigned limit)
{
	unsigned i,j;
	GList *new = NULL;
	GString *slice;

	j = g_list_length(list) % limit;
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
	unsigned i,j;
	GList *new = NULL;
	GString *slice;

	j = g_list_length(list) % limit;
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

GList *g_list_dedup_func(GList *list, GCompareFunc compare_func, int freeitems)
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

/* Given a _sorted_ list of _char *_ entries, removes duplicates and frees them. */
GList *g_list_dedup(GList *list)
{
	return g_list_dedup_func(list, (GCompareFunc)strcmp, TRUE);
}

/* Given a _sorted_ list of pointers to u64's, removes duplicates and frees the pointers to them. */
GList *g_list_dedup_u64_p(GList *list)
{
	return g_list_dedup_func(list, (GCompareFunc)ucmp, TRUE);
}

