/* list.h: list header */

#ifndef  _LIST_H
#define  _LIST_H
#endif

#include <stdlib.h>

struct element
{
	void *data;	
	struct element *nextnode;	
};

struct list 
{
	struct element *start;
	struct element *current;
	struct element *itptr;
	long total_nodes;
	int list_inited; /* 1 if the list is initiated */
};

extern struct element *list_nodeadd(struct list *tlist, void *data,
		size_t dsize);
struct element *list_nodedel(struct list *tlist, void *data);
struct element *list_getstart(struct list *tlist);
int list_totalnodes(struct list *tlist);
void list_showlist(struct list *tlist);
void list_init(struct list *tlist);
