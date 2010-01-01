/*
 * 
 
 Copyright (c) 2004-2009 NFG Net Facilities Group BV support@nfg.nl

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
#include "dm_mailboxstate.h"

typedef struct {
	u64_t id;
	u64_t rows;		// total number of messages in mailbox
	u64_t recent;
	u64_t unseen;
	u64_t owner_id;
	u64_t size;

	GList *sorted;		// ordered list of UID values

	MailboxState_T mbstate;	// cache mailbox metadata;

	GTree *found;		// search result (key: uid, value: msn)

//	GTree *msginfo; 	// cache MessageInfo
//	GTree *ids; 		// key: uid, value: msn
//	GTree *msn; 		// key: msn, value: uid

	GNode *search;
	gchar *charset;		// charset used during search/sort

	fetch_items_t *fi;	// imap fetch

	gboolean uid, no_select, no_inferiors, no_children;
} DbmailMailbox;


DbmailMailbox * dbmail_mailbox_new(u64_t id);
int dbmail_mailbox_open(DbmailMailbox *self);
int dbmail_mailbox_sort(DbmailMailbox *self);
int dbmail_mailbox_search(DbmailMailbox *self);

GTree * dbmail_mailbox_get_msginfo(DbmailMailbox *self);

void dbmail_mailbox_set_id(DbmailMailbox *self, u64_t id);
u64_t dbmail_mailbox_get_id(DbmailMailbox *self);

void dbmail_mailbox_set_uid(DbmailMailbox *self, gboolean uid);
gboolean dbmail_mailbox_get_uid(DbmailMailbox *self);

int dbmail_mailbox_dump(DbmailMailbox *self, FILE *ostream);

void dbmail_mailbox_free(DbmailMailbox *self);

char * dbmail_mailbox_ids_as_string(DbmailMailbox *self);
char * dbmail_mailbox_sorted_as_string(DbmailMailbox *self);
char * dbmail_mailbox_orderedsubject(DbmailMailbox *self);

int dbmail_mailbox_build_imap_search(DbmailMailbox *self, char **search_keys, u64_t *idx, search_order_t order);

GTree * dbmail_mailbox_get_set(DbmailMailbox *self, const char *set, gboolean uid);

#endif
