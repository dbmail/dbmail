/*
 Copyright (c) 2004-2013 NFG Net Facilities Group BV support@nfg.nl
 Copyright (c) 2014-2019 Paul J Stevens, The Netherlands, support@nfg.nl
 Copyright (c) 2020-2023 Alan Hicks, Persistent Objects Ltd support@p-o.co.uk

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
 * 
 * \brief DbmailMailbox class  
 *
 */

#ifndef DM_MAILBOX_H
#define DM_MAILBOX_H

#include "dbmail.h"
#include "dm_mailboxstate.h"

typedef struct {
	Mempool_T pool;
	gboolean freepool;

	uint64_t id;
	uint64_t owner_id;
	uint64_t size;
	gboolean uid; 
	uint64_t modseq;
	gboolean condstore;
	gboolean qresync;

	MailboxState_T mbstate;	// cache mailbox metadata;

	GList *sorted;		// ordered list of UID values
	GTree *found;		// search result (key: uid, value: msn)
	GNode *search;
	const char *charset;		// charset used during search/sort

} DbmailMailbox;


DbmailMailbox * dbmail_mailbox_new(Mempool_T, uint64_t);
int dbmail_mailbox_open(DbmailMailbox *self);
int dbmail_mailbox_sort(DbmailMailbox *self);
int dbmail_mailbox_search(DbmailMailbox *self);

GTree * dbmail_mailbox_get_msginfo(DbmailMailbox *self);

uint64_t dbmail_mailbox_get_id(DbmailMailbox *self);

void dbmail_mailbox_set_uid(DbmailMailbox *self, gboolean uid);
gboolean dbmail_mailbox_get_uid(DbmailMailbox *self);

int dbmail_mailbox_dump(DbmailMailbox *self, FILE *ostream);

void dbmail_mailbox_free(DbmailMailbox *self);

char * dbmail_mailbox_imap_modseq_as_string(DbmailMailbox *self, gboolean uid);
char * dbmail_mailbox_ids_as_string(DbmailMailbox *self, gboolean uid, const char *sep);
char * dbmail_mailbox_sorted_as_string(DbmailMailbox *self);
char * dbmail_mailbox_orderedsubject(DbmailMailbox *self);

int dbmail_mailbox_build_imap_search(DbmailMailbox *self, String_T *search_keys, uint64_t *idx, search_order order);

GTree * dbmail_mailbox_get_set(DbmailMailbox *self, const char *set, gboolean uid);

#endif
