/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2005-2006 NFG Net Facilities Group BV support@nfg.nl

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

/* $Id$
 *
 * list.h: list header */

#ifndef  _LIST_H
#define  _LIST_H

#include <glib.h>
#include <sys/types.h>

/*
 * list data types
 */
struct element {
	void *data;
	size_t dsize;
	struct element *nextnode;
};


struct dm_list {
	struct element *start;
	long total_nodes;
};


void dm_list_init(struct dm_list *tlist);
void dm_list_free(struct element **start);
long dm_list_length(struct dm_list *tlist);

struct element *dm_list_nodeadd(struct dm_list *tlist, const void *data, size_t dsize);
struct element *dm_list_getstart(struct dm_list *tlist);
struct element *dm_list_reverse(struct element *start);

GList * g_list_copy_list(GList *dst, struct element *el);
GList *g_list_slices(GList *list, unsigned limit);

#endif
