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
struct element* list_reverse(struct element *start);

#endif 
