/* $Id$
 * list.h: list header */

#ifndef  _LIST_H
#define  _LIST_H

#include <stdlib.h>

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

struct element *list_nodeadd(struct list *tlist, void *data,
				    size_t dsize);

struct element *list_nodedel(struct list *tlist, void *data);
struct element *list_getstart(struct list *tlist);
void list_freelist(struct element **start);
long list_totalnodes(struct list *tlist);
void list_showlist(struct list *tlist);
void list_init(struct list *tlist);

#endif 
