/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
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

/*
 * list.h: list header */

#ifndef  DM_LIST_H
#define  DM_LIST_H

#include <glib.h>
#include <sys/types.h>
#include "dm_mempool.h"

GList *g_list_slices(GList *list, unsigned limit);
GList *g_list_slices_u64(GList *list, unsigned limit);
GList *g_list_dedup(GList *list, GCompareFunc compare_func, int freeitems);

GString * g_list_join(GList * list, const gchar * sep);
GString * g_list_join_u64(GList * list, const gchar * sep);
GList * g_list_append_printf(GList * list, const char * format, ...);

void g_list_destroy(GList *list);
void g_list_merge(GList **a, GList *b, int condition, GCompareFunc func);

/*
 * provide memory pool based list
 */

#define T List_T

typedef struct T *T;

extern T  p_list_new(Mempool_T);
extern T  p_list_append(T, void *);
extern T  p_list_prepend(T, void *);
extern T  p_list_last(T);
extern T  p_list_first(T);
extern T  p_list_previous(T);
extern T  p_list_next(T);
extern T  p_list_remove(T, T);
extern size_t  p_list_length(T);
extern void *  p_list_data(T);
extern void    p_list_free(T *);

#undef T

#endif
