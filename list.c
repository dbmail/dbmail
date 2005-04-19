/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl

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
 * functions to create lists and add/delete items */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "debug.h"
#include "list.h"
#include "misc.h"

void dm_list_init(struct dm_list *tlist)
{
	memset(tlist,'\0', sizeof(struct dm_list));
}


/*
 * dm_list_free()
 *
 * frees a list and all the memory associated with it
 */
void dm_list_free(struct element **start)
{
	if (!(*start))
		return;

	dm_list_free(&(*start)->nextnode);

	/* free this item */
	g_free((*start)->data);
	g_free(*start);
	*start = NULL;
}

/* 
 * dm_list_reverse()
 *
 * reverse the order of a linked list
 */
struct element *dm_list_reverse(struct element *start)
{
	struct element *newstart;

	if (!start)
		return NULL;	/* nothing there */

	if (!start->nextnode)
		return start;	/* nothing to reverse */

	newstart = dm_list_reverse(start->nextnode);	/* reverse rest of list */
	start->nextnode->nextnode = start;

	start->nextnode = NULL;	/* terminate list */

	return newstart;
}

/*
 * return a empty initialized element;
 */
static struct element *element_new(void)
{
	return g_new0(struct element,1);
}

/* 
 * dm_list_nodeadd()
 *
 * Adds a node to a linked list (list structure). 
 * New item will be FIRST element of new linked list.
 *
 * returns NULL on failure or first element on success
 */
struct element *dm_list_nodeadd(struct dm_list *tlist, const void *data,
			     size_t dsize)
{
	struct element *p;
	
	if (!tlist)
		return NULL;	/* cannot add to non-existing list */

	if (! (p = element_new()))
		return NULL;

	if (! (p->data = (void *)g_malloc0(dsize))) {
		g_free(p);
		return NULL;
	}
	p->data = memcpy(p->data, data, dsize);
	p->dsize=dsize;
	p->nextnode=tlist->start;
	tlist->start = p;

	/* updating node count */
	tlist->total_nodes++;
	return tlist->start;
}


/*
 * dm_list_nodepop()
 *
 * pops the first element of a linked list
 * ! MEMORY SHOULD BE FREED BY CLIENT !
 */
struct element *dm_list_nodepop(struct dm_list *list)
{
	struct element *ret;

	if (!list || !list->start)
		return NULL;

	ret = list->start;

	list->start = list->start->nextnode;

	return ret;
}



/*
 * dm_list_nodedel()
 *
 * removes the item containing 'data' from the list preserving a valid linked-list structure.
 *
 * returns
 */
struct element *dm_list_nodedel(struct dm_list *tlist, void *data)
{
	struct element *temp;
	struct element *item;
	item = NULL;

	if (!tlist)
		return NULL;

	temp = tlist->start;

	/* checking if lists exist else return NULL */
	if (temp == NULL)
		return NULL;

	while (temp != NULL) {	/* walk the list */
		if (temp->data == data) {
			if (item == NULL) {
				tlist->start = temp->nextnode;
				g_free(temp->data);
				g_free((struct element *) temp);
				break;
			} else {
				item->nextnode = temp->nextnode;
				g_free(temp->data);	/* freeing memory */
				g_free((struct element *) temp);
				break;
			}
			/* updating node count */
			tlist->total_nodes--;
		}
		item = temp;
		temp = temp->nextnode;
	}

	return NULL;
}


struct element *dm_list_getstart(struct dm_list *tlist)
{
	return (tlist) ? tlist->start : NULL;
}


long dm_list_length(struct dm_list *tlist)
{
	return (tlist) ? tlist->total_nodes : -1;	/* a NULL ptr doesnt even have zero nodes (?) */
}


void dm_list_show(struct dm_list *tlist)
{
	struct element *temp;

	if (!tlist) {
		trace(TRACE_MESSAGE,
		      "dm_list_show(): NULL ptr received\n");
		return;
	}

	temp = tlist->start;
	while (temp != NULL) {
		trace(TRACE_MESSAGE, "dm_list_show():item found [%s]\n",
		      (char *) temp->data);
		temp = temp->nextnode;
	}
}
/*
 * shallow copy of struct dm_list into GList
 */

GList * g_list_copy_list(GList *dst, struct element *el)
{
	while(el) {
		dst = g_list_append(dst, el->data);
		el = el->nextnode;
	}
	return dst;
}

/*
 * return a list of strings (a,b,c,..N)
 */

GList *g_list_slices(GList *list, unsigned limit)
{
	unsigned i,j;
	GList *new = NULL;
	GString *slice = g_string_new("");
	if (g_list_length(list) <= limit) {
		slice = g_list_join(list,",");
		new=g_list_append(new,g_strdup(slice->str));
		g_string_free(slice,TRUE);
		return new;
	}
	
	j = g_list_length(list) % limit;
	
	list = g_list_first(list);
	
	while(list) {
		slice = g_string_new("");
		slice = g_string_append(slice, (gchar *)list->data);
		for (i=1; i<limit; i++) {
			list = g_list_next(list);
			if (! list) 
				break;
			slice = g_string_append(slice,",");
			slice = g_string_append(slice,(gchar *)list->data);
		}
		new = g_list_append(new, g_strdup(slice->str));
		list = g_list_next(list);
	}
	g_string_free(slice,TRUE);
	return new;
}
/* basic binary tree */
void dm_btree_insert(sortitems_t ** tree, sortitems_t * item) {
	int val;
	if(!(*tree)) {
		*tree = item;
		return;
	}
	val = strcmp(item->ustr,(*tree)->ustr);
	if(val < 0)
		dm_btree_insert(&(*tree)->left, item);
	else if(val > 0)
		dm_btree_insert(&(*tree)->right, item);
}

void dm_btree_printout(sortitems_t * tree, int * i) {
    if (! tree)
        return;
            
	if(tree->left) 
		dm_btree_printout(tree->left, i);
	trace(TRACE_INFO, "dm_btree_printout: i '%d' '%d', '%s'\n", 
			*i, tree->mid, tree->ustr);
	(*i)++;
	if(tree->right) 
		dm_btree_printout(tree->right, i);
}

void dm_btree_traverse(sortitems_t * tree, int * i, unsigned int *rset) {
    if (! tree)
        return;
	
    if(tree->left) 
		dm_btree_traverse(tree->left, i, rset);
	trace(TRACE_DEBUG, "dm_btree_traverse: i '%d' '%d', '%s'\n", 
			*i, tree->mid, tree->ustr); 
	rset[*i] = tree->mid;
	(*i)++;
	if(tree->right) 
		dm_btree_traverse(tree->right, i, rset);
}

void dm_btree_free(sortitems_t * tree) {
    if (! tree)
        return;
    
	if(tree->left) 
		dm_btree_free(tree->left);
	g_free(tree->ustr);
	if(tree->right) 
		dm_btree_free(tree->right);
	else 
		g_free(tree);
}

