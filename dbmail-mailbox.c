/*
  Copyright (C) 2004-2005 NFG Net Facilities Group BV, info@nfg.nl

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

/**
 * \file dbmail-mailbox.c
 *
 * implements DbmailMailbox object
 */

#include "dbmail.h"

struct DbmailMailbox * dbmail_mailbox_new(u64_t mailbox_idnr)
{
	struct DbmailMailbox *self = g_new0(struct DbmailMailbox, 1);
	assert(self);
	self->idnr = mailbox_idnr;
	return self;
}
void * dbmail_mailbox_free(struct DbmailMailbox *self)
{
	g_free(self);
}

GList * dbmail_mailbox_orderedsubject(struct DbmailMailbox *self)
{
	GString *q = g_string_new("");
	/* full threads (unordered) */
	g_string_printf(q, "SELECT message_idnr,subjectfield "
			"FROM dbmail_messages "
			"JOIN dbmail_subjectfield using (physmessage_id) "
			"JOIN dbmail_datefield using (physmessage_id) "
			"WHERE mailbox_idnr=%llu "
			"ORDER BY subjectfield,datefield", self->idnr);
	
	/* thread-roots (ordered) */
	g_string_printf(q, "SELECT message_idnr,subjectfield "
			"FROM dbmail_messages "
			"JOIN dbmail_subjectfield USING (physmessage_id) "
			"JOIN dbmail_datefield USING (physmessage_id) "
			"WHERE mailbox_idnr=%llu "
			"GROUP BY subjectfield ORDER BY datefield", self->idnr);
	
	g_string_free(q,TRUE);
}


