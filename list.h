/*
 Copyright (C) 1999-2003 IC & S  dbmail@ic-s.nl

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
 * (c) 2000-2002 IC&S, The Netherlands
 *
 * list.h: list header */

#ifndef  _LIST_H
#define  _LIST_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>

/*
 * list data types
 */
struct element
{
  void *data;	
  size_t dsize;
  struct element *nextnode;	
};


struct list 
{
  struct element *start;
  long total_nodes;
};


struct element *list_nodeadd(struct list *tlist, const void *data,
				    size_t dsize);

struct element *list_nodedel(struct list *tlist, void *data);
struct element *list_nodepop(struct list *list);
struct element *list_getstart(struct list *tlist);
void list_freelist(struct element **start);
long list_totalnodes(struct list *tlist);
void list_showlist(struct list *tlist);
void list_init(struct list *tlist);

/* this function had to be renamed because some MySQL versions
 * export a function with the name list_reverse(). Nice of them,
 * but a pretty "strange" way to pollute the global namespace
 */
struct element* dbmail_list_reverse(struct element *start);

#endif 
