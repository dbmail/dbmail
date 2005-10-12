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

#define FROM_STANDARD_DATE "Tue Oct 11 13:06:24 2005"

static size_t dump_message_to_stream(struct DbmailMessage *message, GMimeStream *ostream)
{
	size_t r = 0;
	gchar *s;
	GString *sender;
	GString *date;
	InternetAddressList *ialist;
	InternetAddress *ia;
	
	GString *t;
	
	g_return_val_if_fail(GMIME_IS_MESSAGE(message->content),0);

	s = dbmail_message_to_string(message);

	if (! strncmp(s,"From ",5)==0) {
		ialist = internet_address_parse_string(g_mime_message_get_sender(GMIME_MESSAGE(message->content)));
		sender = g_string_new("nobody@foo");
		if (ialist) {
			ia = ialist->address;
			if (ia) 
				g_string_printf(sender,"%s", ia->value.addr);
		}
		internet_address_list_destroy(ialist);
		
		date = g_string_new(dbmail_message_get_internal_date(message));
		if (date->len < 1)
			date = g_string_new(FROM_STANDARD_DATE);
		
		t = g_string_new("From ");
		g_string_append_printf(t,"%s %s\n", sender->str, date->str);

		r = g_mime_stream_write_string(ostream,t->str);

		g_string_free(t,TRUE);
		g_string_free(sender,TRUE);
		g_string_free(date,TRUE);
		
	}
	
	r += g_mime_stream_write_string(ostream,s);
	r += g_mime_stream_write_string(ostream,"\n");
	
	g_free(s);
	return r;
}

int dbmail_mailbox_dump(struct DbmailMailbox *self, FILE *file)
{
	unsigned i,j;
	int count=0;
	gboolean h;
	GMimeStream *ostream;
	GList *ids;
	struct DbmailMessage *message = NULL;
	GString *q = g_string_new("");
	GString *t = g_string_new("");

	ostream = g_mime_stream_file_new(file);
	
	ids = g_list_slices(self->ids,100);
	ids = g_list_first(ids);

	while (ids) {
		g_string_printf(q,"SELECT is_header,messageblk FROM %smessageblks b "
				"JOIN %sphysmessage p ON b.physmessage_id=p.id "
				"JOIN %smessages m ON m.physmessage_id=p.id "
				"WHERE m.message_idnr IN (%s)", DBPFX, DBPFX, DBPFX,
				(char *)ids->data);
		
		if (db_query(q->str) == -1)
			return -1;

		if ((j = db_num_rows()) < 1)
			break;
		
		for (i=0; i<j; i++) {
			h = db_get_result_int(i,0);
			if (h) {
				if (t->len > 0) {
					message = dbmail_message_new();
					message = dbmail_message_init_with_string(message,t);
					if(dump_message_to_stream(message,ostream) > 0)
						count++;
					dbmail_message_free(message);
				}
				g_string_printf(t,"%s", db_get_result(i,1));
			} else {
				g_string_append_printf(t,"%s",db_get_result(i,1));
			}
		}
		db_free_result();

		if (! g_list_next(ids))
			break;
		
		ids = g_list_next(ids);
	}
	
	if (self->ids && t->len) {
		message = dbmail_message_new();
		message = dbmail_message_init_with_string(message,t);
		if (dump_message_to_stream(message,ostream) > 0)
			count++;
		dbmail_message_free(message);
	}
	
	g_string_free(t,TRUE);
	g_list_foreach(ids,(GFunc)g_free,NULL);
	g_list_free(ids);
	g_string_free(q,TRUE);
	g_object_unref(ostream);
	
	return count;
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


