/* $Id$
 * functions to create lists and add/delete items
 * (c) 2001 eelco@eelco.com */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debug.h"
#include "list.h"

extern void func_memtst (char filename[255],int line,int tst);

void list_init (struct list *tlist)
{
  tlist->start=NULL;
  tlist->current=NULL;
  tlist->itptr=NULL;
  tlist->total_nodes=0;
  tlist->list_inited=1;
}

struct element *list_nodeadd(struct list *tlist, void *data,
			     size_t dsize)
{
  /* Adds a node to a linked list (list structure) */
  struct element *p;
  p=tlist->start;
	
  tlist->start=(struct element *)malloc(sizeof(struct element));
  memtst(tlist->start==NULL);
	
  /* allocating data for object */
  memtst((tlist->start->data=(void *)malloc(dsize))==NULL);
	
  /* copying data */
  memtst(((tlist->start->data=memcpy(tlist->start->data,data,dsize)))
	 ==NULL);
  tlist->start->nextnode=p;

	/* updating node count */
  tlist->total_nodes++;
  return tlist->start;
}

struct element *list_nodedel(struct list *tlist,void *data)
{
  struct element *temp;
  struct element *item;
  item=NULL;
  temp=tlist->start;

  /* checking if lists exist else return NULL*/

  if (temp==NULL) return NULL;
	
  while (temp!=NULL) /* walk the list */
    { 
      if (temp->data==data)
	{
	  if (item==NULL)
	    {
	      tlist->start=temp->nextnode;
	      free (temp->data);
	      free ((struct element *)temp);
	      break;
	    }
	  else 
	    {
	      item->nextnode=temp->nextnode;
	      free(temp->data); /* freeing memory */
	      free ((struct element *)temp);
	      break;
	    }
	  /* updating node count */
	  tlist->total_nodes--;
	} 
      item=temp;
      temp=temp->nextnode;
    }        

  return NULL;
}

struct element *list_getstart(struct list *tlist)
{
  return tlist->start;
}

int list_totalnodes(struct list *tlist)
{
  return tlist->total_nodes;
}

void list_showlist(struct list *tlist)
{
  struct element *temp;
  temp=tlist->start;
  while (temp!=NULL)
    {
      trace (TRACE_MESSAGE,"list_showlist():item found [%s]\n",(char *)temp->data);
      temp=temp->nextnode;
    }
} 
