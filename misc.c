/*	$Id$
 *	Miscelaneous functions */

#include "config.h"
#include "misc.h"

extern struct list users;

struct list tmplist;

struct element *finduser(char *username)
{
	struct element *e;
	
	e=list_getstart(&tmplist);
	while (e!=NULL)
		{
		if (strcmp((char *)e->data,username)==0)
				return e;
		e=e->nextnode;
		}
	return NULL;
}

void check_duplicates()
{
/* 	The target email addresses must not contain duplicates. 
  	This would happen if somebody is in the to: and the Cc: field.
	The MTA would deliver it twice. We won't. */

	struct element *tmp;
	
	printf ("Checking for duplicates\n");
	
	tmp=list_getstart(&users);
	while (tmp!=NULL)
	{
		if (finduser((char *)tmp->data)==NULL)
			list_nodeadd(&tmplist,(void *)tmp->data,strlen((char *)tmp->data));
		tmp=tmp->nextnode;
	}
	users=tmplist;
	printf ("total email addresses found %lu\n",list_totalnodes(&users));
}
