/*
 * 
 
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
 
 *
 * 
 */

/* 
 * \file dbmail-mailbox.h
 * 
 * \brief DbmailMailbox class  
 *
 */

#ifndef _DBMAIL_MAILBOX_H
#define _DBMAIL_MAILBOX_H

#include "dbmail.h"

struct DbmailMailbox {
	u64_t id;
	gchar *name;
	u64_t owner_id;
	u64_t size;
	GTree *ids; 		// key: uid, value: msn
	GTree *msn; 		// key: msn, value: uid
	gboolean uid;		// subset type
	GList *sorted;
	GNode *search;
	fetch_items_t *fi;	// imap fetch
};

struct DbmailMailbox * dbmail_mailbox_new(u64_t id);
int dbmail_mailbox_open(struct DbmailMailbox *self);

int dbmail_mailbox_sort(struct DbmailMailbox *self);
int dbmail_mailbox_search(struct DbmailMailbox *self);

void dbmail_mailbox_set_id(struct DbmailMailbox *self, u64_t id);
u64_t dbmail_mailbox_get_id(struct DbmailMailbox *self);

void dbmail_mailbox_set_uid(struct DbmailMailbox *self, gboolean uid);
gboolean dbmail_mailbox_get_uid(struct DbmailMailbox *self);

int dbmail_mailbox_dump(struct DbmailMailbox *self, FILE *ostream);

void dbmail_mailbox_free(struct DbmailMailbox *self);

char * dbmail_mailbox_ids_as_string(struct DbmailMailbox *self);
char * dbmail_mailbox_sorted_as_string(struct DbmailMailbox *self);
char * dbmail_mailbox_orderedsubject(struct DbmailMailbox *self);

int dbmail_mailbox_build_imap_search(struct DbmailMailbox *self, char **search_keys, u64_t *idx, int sorted);

GTree * dbmail_mailbox_get_set(struct DbmailMailbox *self, const char *set);


#endif
