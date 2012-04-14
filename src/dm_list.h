/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2004-2012 NFG Net Facilities Group BV support@nfg.nl

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
 * list.h: list header */

#ifndef  DM_LIST_H
#define  DM_LIST_H

#include <glib.h>
#include <sys/types.h>

GList *g_list_slices(GList *list, unsigned limit);
GList *g_list_slices_u64(GList *list, unsigned limit);
GList *g_list_dedup(GList *list, GCompareFunc compare_func, int freeitems);

GString * g_list_join(GList * list, const gchar * sep);
GString * g_list_join_u64(GList * list, const gchar * sep);
GList * g_list_append_printf(GList * list, const char * format, ...);

void g_list_destroy(GList *list);
void g_list_merge(GList **a, GList *b, int condition, GCompareFunc func);


#endif
