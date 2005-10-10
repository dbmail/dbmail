/*
 * 
 
 Copyright (C) 2005, NFG Net Facilities Group BV, info@nfg.nl

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
	GList *ids;
};

struct DbmailMailbox * dbmail_mailbox_new(u64_t id);
struct DbmailMailbox * dbmail_mailbox_open(struct DbmailMailbox *self);

void dbmail_mailbox_set_id(struct DbmailMailbox *self, u64_t id);
u64_t dbmail_mailbox_get_id(struct DbmailMailbox *self);

int dbmail_mailbox_dump(struct DbmailMailbox *self, FILE *ostream);

void dbmail_mailbox_free(struct DbmailMailbox *self);

GList * dbmail_mailbox_orderedsubject(struct DbmailMailbox *self);


#endif
