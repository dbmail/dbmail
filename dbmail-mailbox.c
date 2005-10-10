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

extern db_param_t _db_params;
#define DBPFX _db_params.pfx


struct DbmailMailbox * dbmail_mailbox_new(u64_t id)
{
	struct DbmailMailbox *self = g_new0(struct DbmailMailbox, 1);
	assert(self);
	dbmail_mailbox_set_id(self,id);
	self->ids = NULL;
	return self;
}

void dbmail_mailbox_free(struct DbmailMailbox *self)
{
	if (self->ids) {
		g_list_foreach(self->ids,(GFunc)g_free,NULL);
		g_list_free(self->ids);
	}
	g_free(self);
}

void dbmail_mailbox_set_id(struct DbmailMailbox *self, u64_t id)
{
	assert(id > 0);
	self->id = id;
}

u64_t dbmail_mailbox_get_id(struct DbmailMailbox *self)
{
	assert(self->id > 0);
	return self->id;
}

struct DbmailMailbox * dbmail_mailbox_open(struct DbmailMailbox *self)
{
	u64_t row,rows;
	GList *ids = NULL;
	GString *q = g_string_new("");
	g_string_printf(q,"SELECT message_idnr FROM dbmail_messages "
			"WHERE mailbox_idnr=%llu", dbmail_mailbox_get_id(self));
	
	if (db_query(q->str) == DM_EQUERY)
		return self;
		
	if ((rows  = db_num_rows()) < 1) {
		trace(TRACE_INFO, "%s,%s: no messages in mailbox",
				__FILE__, __func__);
		db_free_result();
		return self;
	}
	
	for (row=0; row < rows; row++)
		ids = g_list_append(ids,g_strdup((char *)db_get_result(row, 0)));
	
	db_free_result();
	self->ids = ids;
	return self;
}

int dbmail_mailbox_dump(struct DbmailMailbox *self, FILE *file)
{
	unsigned i,j;
	GMimeStream *ostream, *fstream;
	GMimeFilter *filter;
	GList *ids;
	GString *q = g_string_new("");

	ostream = g_mime_stream_file_new(file);
	fstream = g_mime_stream_filter_new_with_stream(ostream);
	filter = g_mime_filter_from_new(GMIME_FILTER_FROM_MODE_ESCAPE);
	g_mime_stream_filter_add((GMimeStreamFilter *)fstream, filter);
	
	ids = g_list_slices(self->ids,100);
	ids = g_list_first(ids);

	while (ids) {
		g_string_printf(q,"SELECT messageblk FROM %smessageblks b "
				"JOIN %sphysmessage p ON b.physmessage_id=p.id "
				"JOIN %smessages m ON m.physmessage_id=p.id "
				"WHERE m.message_idnr IN (%s)", DBPFX, DBPFX, DBPFX,
				(char *)ids->data);
		
		if (db_query(q->str) == -1)
			return -1;

		if ((j = db_num_rows()) < 1)
			break;
		
		for (i=0; i<j; i++) 
			g_mime_stream_printf(fstream, "%s", db_get_result(i,0));
		
		db_free_result();

		ids = g_list_next(ids);
	}
	
	g_list_foreach(ids,(GFunc)g_free,NULL);
	g_list_free(ids);
	g_string_free(q,TRUE);
	g_object_unref(filter);
	g_object_unref(ostream);
	g_object_unref(fstream);
	
	return 0;
}

GList * dbmail_mailbox_orderedsubject(struct DbmailMailbox *self)
{
	GList *res = NULL;
	GString *q = g_string_new("");
	/* full threads (unordered) */
	g_string_printf(q, "SELECT message_idnr,subjectfield "
			"FROM dbmail_messages "
			"JOIN dbmail_subjectfield using (physmessage_id) "
			"JOIN dbmail_datefield using (physmessage_id) "
			"WHERE mailbox_idnr=%llu "
			"ORDER BY subjectfield,datefield", dbmail_mailbox_get_id(self));
	
	/* thread-roots (ordered) */
	g_string_printf(q, "SELECT message_idnr,subjectfield "
			"FROM dbmail_messages "
			"JOIN dbmail_subjectfield USING (physmessage_id) "
			"JOIN dbmail_datefield USING (physmessage_id) "
			"WHERE mailbox_idnr=%llu "
			"GROUP BY subjectfield ORDER BY datefield", dbmail_mailbox_get_id(self));
	
	g_string_free(q,TRUE);

	return res;
}


