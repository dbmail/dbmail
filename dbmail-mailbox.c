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

static gboolean _tree_foreach(gpointer key UNUSED, gpointer value, GString * data)
{
	gboolean res = FALSE;
	GList *sublist = (GList *)value;

	GString *t = g_string_new("");
	
	sublist = g_list_first(sublist);
	while(sublist) {
		g_string_append_printf(t, "(%llu)", (u64_t)GPOINTER_TO_UINT(sublist->data));
		if (! g_list_next(sublist))
			break;
		sublist = g_list_next(sublist);
	}
	if (g_list_length(sublist) > 1)
		g_string_append_printf(data, "(%s)", t->str);
	else
		g_string_append_printf(data, "%s", t->str);

	g_string_free(t,TRUE);

	return res;
}
char * dbmail_mailbox_orderedsubject(struct DbmailMailbox *self, u64_t *rset, unsigned setlen)
{
	GList *sublist = NULL;
	GString *q = g_string_new("");
	u64_t i = 0, r = 0, idnr = 0;
	char *subj;
	char *res = NULL;
	GTree *tree = g_tree_new((GCompareFunc)strcmp);
	GString *threads = g_string_new("");
	
	/* thread-roots (ordered) */
	g_string_printf(q, "SELECT message_idnr,subjectfield "
			"FROM %smessages "
			"JOIN %ssubjectfield USING (physmessage_id) "
			"JOIN %sdatefield USING (physmessage_id) "
			"WHERE mailbox_idnr=%llu "
			"AND status < '%d' "
			"GROUP BY subjectfield ORDER BY datefield", 
			DBPFX, DBPFX, DBPFX,
			dbmail_mailbox_get_id(self),
			MESSAGE_STATUS_DELETE);

	if (db_query(q->str) == DM_EQUERY) {
		g_string_free(q,TRUE);
		return res;;
	}
	if ((r = db_num_rows())==0) {
		g_string_free(q,TRUE);
		return res;
	}
	
	i=0;
	while (i < r) {
		idnr = db_get_result_u64(i,0);
		if (db_binary_search(rset,setlen,idnr) < 0) {
			i++;
			continue;
		}
		subj = (char *)db_get_result(i,1);
		g_tree_insert(tree,(gpointer)(g_strdup(subj)), NULL);
		i++;
	}
		
	/* full threads (unordered) */
	g_string_printf(q, "SELECT message_idnr,subjectfield "
			"FROM %smessages "
			"JOIN %ssubjectfield using (physmessage_id) "
			"JOIN %sdatefield using (physmessage_id) "
			"WHERE mailbox_idnr=%llu "
			"AND status < '%d' "
			"ORDER BY subjectfield,datefield", 
			DBPFX, DBPFX, DBPFX,
			dbmail_mailbox_get_id(self),
			MESSAGE_STATUS_DELETE);
		
	if (db_query(q->str) == DM_EQUERY) {
		g_string_free(q,TRUE);
		return res;;
	}
	if ((r = db_num_rows())==0) {
		g_string_free(q,TRUE);
		return res;
	}
	
	i=0;
	while (i < r) {
		idnr = db_get_result_u64(i,0);
		if (db_binary_search(rset,setlen,idnr) < 0) {
			i++;
			continue;
		}
		
		subj = (char *)db_get_result(i,1);
		sublist = g_tree_lookup(tree,(gconstpointer)subj);
		sublist = g_list_append(sublist,GUINT_TO_POINTER((unsigned)idnr));
		g_tree_insert(tree,(gpointer)(g_strdup(subj)),(gpointer)sublist);
		i++;
	}

	g_tree_foreach(tree,(GTraverseFunc)_tree_foreach,threads);
	res = threads->str;

	g_string_free(threads,FALSE);
	g_string_free(q,TRUE);

	return res;
}


static void _append_join(char *join, char *table)
{
	char *tmp;
	tmp = g_strdup_printf("LEFT JOIN %s%s ON p.id=%s%s.physmessage_id ", DBPFX, table, DBPFX, table);
	g_strlcat(join, tmp, MAX_SEARCH_LEN);
	g_free(tmp);
}

static void _append_sort(char *order, char *field, gboolean reverse)
{
	char *tmp;
	tmp = g_strdup_printf("%s%s,", field, reverse ? " DESC" : "");
	g_strlcat(order, tmp, MAX_SEARCH_LEN);
	g_free(tmp);
}


/*
 * build_imap_search()
 *
 * builds a linked list of search items from a set of IMAP search keys
 * sl should be initialized; new search items are simply added to the list
 *
 * returns -1 on syntax error, -2 on memory error; 0 on success, 1 if ')' has been encountered
 */
int dbmail_mailbox_build_imap_search(struct DbmailMailbox *self, char **search_keys, search_key_t *sk, int *idx, int sorted)
{
	search_key_t *value = NULL;
	char *key = NULL;
	int result;
	gboolean reverse = FALSE;
	GTree *tree;

	if (!search_keys || !search_keys[*idx])
		return 0;

	if (! (tree = sk->sub_search))
		return 0;
	
	if (sorted) {
		key = g_strdup("sorted");
		value = g_tree_lookup(tree,key);
	}
		
	if (! value)
		value = g_new0(search_key_t,1);
	
	if (! value->sub_search)
		value->sub_search = g_tree_new((GCompareFunc)strcmp);
	
	/* SORT */
	if (sorted)
		value->type = IST_SORT;

	if(sorted && (strcasecmp(search_keys[*idx], "reverse") == 0)) {
		(*idx)++;
		reverse = TRUE;
	}
			
	if (sorted && (strcasecmp(search_keys[*idx], "us-ascii") == 0)) {
	
		(*idx)++;
		return 0;
		
	} else if (sorted && (strcasecmp(search_keys[*idx], "iso-8859-1") == 0)) {
		
		(*idx)++;
		return 0;

	} else if (sorted && (strcasecmp(search_keys[*idx], "arrival") == 0)) {
		
		_append_sort(value->order, "internal_date", reverse);
		
		(*idx)++;
	
	} else if (sorted && (strcasecmp(search_keys[*idx], "size") == 0)) {
		
		_append_sort(value->order, "messagesize", reverse);
		
		(*idx)++;
	
	} else if (sorted && (strcasecmp(search_keys[*idx], "from") == 0)) {
		
		_append_join(value->table, "fromfield");
		_append_sort(value->order, "fromaddr", reverse);	
		
		(*idx)++;
		
	} else if (sorted && (strcasecmp(search_keys[*idx], "subject") == 0)) {
		
		_append_join(value->table, "subjectfield");
		_append_sort(value->order, "subjectfield", reverse);
		
		(*idx)++;
		
	} else if (sorted && (strcasecmp(search_keys[*idx], "cc") == 0)) {
		
		_append_join(value->table, "ccfield");
		_append_sort(value->order, "ccaddr", reverse);
		
		(*idx)++;
		
	} else if (sorted && (strcasecmp(search_keys[*idx], "to") == 0)) {
		
		_append_join(value->table, "tofield");
		_append_sort(value->order, "toaddr", reverse);
		
		(*idx)++;
		
		
	} else if (sorted && (strcasecmp(search_keys[*idx], "date") == 0)) {

		_append_join(value->table, "datefield");
		_append_sort(value->order, "datefield", reverse);
		
		(*idx)++;
		

	/* SEARCH */

		
	} else if (strcasecmp(search_keys[*idx], "all") == 0) {
		
		key = g_strdup(search_keys[*idx]);
		
		value->type = IST_SET;
		strcpy(value->search, "1:*");
		(*idx)++;
		
	} else if (strcasecmp(search_keys[*idx], "uid") == 0) {
		
		if (!search_keys[*idx + 1])
			return -1;

		key = g_strdup(search_keys[*idx]);
		
		(*idx)++;

		value->type = IST_SET_UID;
		strncpy(value->search, search_keys[(*idx)], MAX_SEARCH_LEN);
		
		if (!check_msg_set(value->search))
			return -1;
		(*idx)++;
	}

	/*
	 * FLAG search keys
	 */

	else if (strcasecmp(search_keys[*idx], "answered") == 0) {
		
		key = g_strdup(search_keys[*idx]);
		value->type = IST_FLAG;
		strncpy(value->search, "answered_flag=1", MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if (strcasecmp(search_keys[*idx], "deleted") == 0) {
		
		key = g_strdup(search_keys[*idx]);
		value->type = IST_FLAG;
		strncpy(value->search, "deleted_flag=1", MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if (strcasecmp(search_keys[*idx], "flagged") == 0) {
		
		key = g_strdup(search_keys[*idx]);
		value->type = IST_FLAG;
		strncpy(value->search, "flagged_flag=1", MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if (strcasecmp(search_keys[*idx], "recent") == 0) {
		
		key = g_strdup(search_keys[*idx]);
		value->type = IST_FLAG;
		strncpy(value->search, "recent_flag=1", MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if (strcasecmp(search_keys[*idx], "seen") == 0) {
		
		key = g_strdup(search_keys[*idx]);
		value->type = IST_FLAG;
		strncpy(value->search, "seen_flag=1", MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if (strcasecmp(search_keys[*idx], "keyword") == 0) {
		
		/* no results from this one */
		if (!search_keys[(*idx) + 1])
			return -1;
		key = g_strdup(search_keys[*idx]);
		(*idx)++;

		value->type = IST_SET;
		strcpy(value->search, "0");
		(*idx)++;
		
	} else if (strcasecmp(search_keys[*idx], "draft") == 0) {
		
		key = g_strdup(search_keys[*idx]);
		value->type = IST_FLAG;
		strncpy(value->search, "draft_flag=1", MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if (strcasecmp(search_keys[*idx], "new") == 0) {
		
		key = g_strdup(search_keys[*idx]);
		value->type = IST_FLAG;
		strncpy(value->search, "(seen_flag=0 AND recent_flag=1)", MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if (strcasecmp(search_keys[*idx], "old") == 0) {
		
		key = g_strdup(search_keys[*idx]);
		value->type = IST_FLAG;
		strncpy(value->search, "recent_flag=0", MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if (strcasecmp(search_keys[*idx], "unanswered") == 0) {
		
		key = g_strdup(search_keys[*idx]);
		value->type = IST_FLAG;
		strncpy(value->search, "answered_flag=0", MAX_SEARCH_LEN);
		(*idx)++;

	} else if (strcasecmp(search_keys[*idx], "undeleted") == 0) {
		
		key = g_strdup(search_keys[*idx]);
		value->type = IST_FLAG;
		strncpy(value->search, "deleted_flag=0", MAX_SEARCH_LEN);
		(*idx)++;
	
	} else if (strcasecmp(search_keys[*idx], "unflagged") == 0) {
		
		key = g_strdup(search_keys[*idx]);
		value->type = IST_FLAG;
		strncpy(value->search, "flagged_flag=0", MAX_SEARCH_LEN);
		(*idx)++;
	
	} else if (strcasecmp(search_keys[*idx], "unseen") == 0) {
		
		key = g_strdup(search_keys[*idx]);
		value->type = IST_FLAG;
		strncpy(value->search, "seen_flag=0", MAX_SEARCH_LEN);
		(*idx)++;
	
	} else if (strcasecmp(search_keys[*idx], "unkeyword") == 0) {
	
		/* matches every msg */
		if (!search_keys[(*idx) + 1])
			return -1;
		key = g_strdup(search_keys[*idx]);
		(*idx)++;

		value->type = IST_SET;
		strcpy(value->search, "1:*");
		(*idx)++;
	
	} else if (strcasecmp(search_keys[*idx], "undraft") == 0) {
		
		key = g_strdup(search_keys[*idx]);
		value->type = IST_FLAG;
		strncpy(value->search, "draft_flag=0", MAX_SEARCH_LEN);
		(*idx)++;
	
	}

	/*
	 * HEADER search keys
	 */

	else if (strcasecmp(search_keys[*idx], "bcc") == 0) {
		
		key = g_strdup(search_keys[*idx]);
		value->type = IST_HDR;
		strncpy(value->hdrfld, "bcc", MIME_FIELD_MAX);
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		strncpy(value->search, search_keys[(*idx)], MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if (strcasecmp(search_keys[*idx], "cc") == 0) {
		
		key = g_strdup(search_keys[*idx]);
		value->type = IST_HDR;
		strncpy(value->hdrfld, "cc", MIME_FIELD_MAX);
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		strncpy(value->search, search_keys[(*idx)], MAX_SEARCH_LEN);
		(*idx)++;
	
	} else if (strcasecmp(search_keys[*idx], "from") == 0) {
		
		key = g_strdup(search_keys[*idx]);
		value->type = IST_HDR;
		strncpy(value->hdrfld, "from", MIME_FIELD_MAX);
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		strncpy(value->search, search_keys[(*idx)], MAX_SEARCH_LEN);
		(*idx)++;
	
	} else if (strcasecmp(search_keys[*idx], "to") == 0) {
		
		key = g_strdup(search_keys[*idx]);
		value->type = IST_HDR;
		strncpy(value->hdrfld, "to", MIME_FIELD_MAX);
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		strncpy(value->search, search_keys[(*idx)], MAX_SEARCH_LEN);
		(*idx)++;
	
	} else if (strcasecmp(search_keys[*idx], "subject") == 0) {
		
		key = g_strdup(search_keys[*idx]);
		value->type = IST_HDR;
		strncpy(value->hdrfld, "subject", MIME_FIELD_MAX);
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		strncpy(value->search, search_keys[(*idx)], MAX_SEARCH_LEN);
		(*idx)++;
	
	} else if (strcasecmp(search_keys[*idx], "header") == 0) {
		
		key = g_strdup(search_keys[*idx]);
		value->type = IST_HDR;
		if (!search_keys[(*idx) + 1] || !search_keys[(*idx) + 2])
			return -1;

		strncpy(value->hdrfld, search_keys[(*idx) + 1], MIME_FIELD_MAX);
		strncpy(value->search, search_keys[(*idx) + 2], MAX_SEARCH_LEN);

		(*idx) += 3;

	} else if (strcasecmp(search_keys[*idx], "sentbefore") == 0) {
	
		key = g_strdup(search_keys[*idx]);
		value->type = IST_HDRDATE_BEFORE;
		strncpy(value->hdrfld, "date", MIME_FIELD_MAX);
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		strncpy(value->search, search_keys[(*idx)], MAX_SEARCH_LEN);
		(*idx)++;

	} else if (strcasecmp(search_keys[*idx], "senton") == 0) {
		
		key = g_strdup(search_keys[*idx]);
		value->type = IST_HDRDATE_ON;
		strncpy(value->hdrfld, "date", MIME_FIELD_MAX);
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		strncpy(value->search, search_keys[(*idx)], MAX_SEARCH_LEN);
		(*idx)++;
	
	} else if (strcasecmp(search_keys[*idx], "sentsince") == 0) {
		
		key = g_strdup(search_keys[*idx]);
		value->type = IST_HDRDATE_SINCE;
		strncpy(value->hdrfld, "date", MIME_FIELD_MAX);
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		strncpy(value->search, search_keys[(*idx)], MAX_SEARCH_LEN);
		(*idx)++;
	
	}

	/*
	 * INTERNALDATE keys
	 */

	else if (strcasecmp(search_keys[*idx], "before") == 0) {
		
		key = g_strdup(search_keys[*idx]);
		value->type = IST_IDATE;
		if (!search_keys[(*idx) + 1])
			return -1;
		
		(*idx)++;
		if (!check_date(search_keys[*idx]))
			return -1;
		
		g_snprintf(value->search, MAX_SEARCH_LEN, "internaldate < '%s'", date_imap2sql(search_keys[*idx]));
		(*idx)++;
		
	} else if (strcasecmp(search_keys[*idx], "on") == 0) {
		
		key = g_strdup(search_keys[*idx]);
		value->type = IST_IDATE;
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		if (!check_date(search_keys[*idx]))
			return -1;

		g_snprintf(value->search, MAX_SEARCH_LEN, "internal_date LIKE '%s%%'", date_imap2sql(search_keys[*idx]));
		(*idx)++;
		
	} else if (strcasecmp(search_keys[*idx], "since") == 0) {
		
		key = g_strdup(search_keys[*idx]);
		value->type = IST_IDATE;
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		if (!check_date(search_keys[*idx]))
			return -1;

		g_snprintf(value->search, MAX_SEARCH_LEN, "internaldate > '%s'", date_imap2sql(search_keys[*idx]));
		(*idx)++;
	}

	/*
	 * DATA-keys
	 */

	else if (strcasecmp(search_keys[*idx], "body") == 0) {
		
		key = g_strdup(search_keys[*idx]);
		value->type = IST_DATA_BODY;
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		strncpy(value->search, search_keys[(*idx)], MAX_SEARCH_LEN);
		(*idx)++;
	
	} else if (strcasecmp(search_keys[*idx], "text") == 0) {
	
		key = g_strdup(search_keys[*idx]);
		value->type = IST_DATA_TEXT;
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		strncpy(value->search, search_keys[(*idx)], MAX_SEARCH_LEN);
		(*idx)++;
	}

	/*
	 * SIZE keys
	 */

	else if (strcasecmp(search_keys[*idx], "larger") == 0) {
		
		key = g_strdup(search_keys[*idx]);
		value->type = IST_SIZE_LARGER;
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		value->size = strtoull(search_keys[(*idx)], NULL, 10);
		(*idx)++;
	
	} else if (strcasecmp(search_keys[*idx], "smaller") == 0) {
	
		key = g_strdup(search_keys[*idx]);
		value->type = IST_SIZE_SMALLER;
		if (!search_keys[(*idx) + 1])
			return -1;

		(*idx)++;
		value->size = strtoull(search_keys[(*idx)], NULL, 10);
		(*idx)++;
	
	}

	/*
	 * NOT, OR, ()
	 */
	
	else if (strcasecmp(search_keys[*idx], "not") == 0) {
	
		key = g_strdup("not");
		value->type = IST_SUBSEARCH_NOT;

		(*idx)++;
		
		if ((result = build_imap_search(search_keys, value, idx, sorted )) < 0) {
			g_tree_destroy(value->sub_search);
			return result;
		}
		
	} else if (strcasecmp(search_keys[*idx], "or") == 0) {
		
		key = g_strdup("or");
		value->type = IST_SUBSEARCH_OR;

		(*idx)++;
		if ((result = build_imap_search(search_keys, value, idx, sorted)) < 0) {
			g_tree_destroy(value->sub_search);
			return result;
		}


	} else if ((! sorted)  && (strcasecmp(search_keys[*idx], "(") == 0)) {
		
		key = g_strdup("and");
		value->type = IST_SUBSEARCH_AND;

		(*idx)++;
		while ((result = build_imap_search(search_keys, value, idx, sorted)) == 0 && search_keys[*idx]);

		if (result < 0) {
			g_tree_destroy(value->sub_search);
			return result;
		}

		if (result == 0) {
			/* no ')' encountered */
			free_searchtree(value->sub_search);
			return -1;
		}
	} else if (strcasecmp(search_keys[*idx], ")") == 0) {
		
		(*idx)++;
		return 1;
	
	} else if (check_msg_set(search_keys[*idx])) {
	
		key = g_strdup("SET");
		value->type = IST_SET;
		strncpy(value->search, search_keys[*idx], MAX_SEARCH_LEN);
		(*idx)++;
	
	} else {
		/* unknown search key */
		return -1;
	}
	trace(TRACE_DEBUG,"%s,%s: tree insert key [%s]", __FILE__, __func__, key);

	g_tree_insert(tree,key,value);

	return 0;
}
/*
 * perform_imap_search()
 *
 * returns 0 on succes, -1 on dbase error, -2 on memory error, 1 if result set is too small
 * (new mail has been added to mailbox while searching, mailbox data out of sync)
 */
int dbmail_mailbox_sort(void) 
{
	return 0;
}
int dbmail_mailbox_search(void) //unsigned int *rset, int setlen, search_key_t * sk, mailbox_t * mb, int condition)
{
	return 0;

#ifdef OLD
	
	int result;
	unsigned int *newset = NULL;
	int subtype = IST_SUBSEARCH_OR;
	search_key_t *subsk = NULL;
	GTree *subtree;
	GList *keylist = NULL;

	if (!setlen) // empty mailbox
		return 0;
	
	if (!rset) {
		trace(TRACE_ERROR,"%s,%s: error empty rset", __FILE__, __func__);
		return -2;	/* stupidity */
	}

	if (!sk) {
		trace(TRACE_ERROR,"%s,%s: error empty sk", __FILE__, __func__);
		return -2;	/* no search */
	}


	trace(TRACE_DEBUG,"%s,%s: search_key [%d] condition [%d]", __FILE__, __func__, sk->type, condition);
	
	g_tree_keys(sk->sub_search);

	switch (sk->type) {
	case IST_SET:
		build_set(rset, setlen, sk->search);
		break;

	case IST_SET_UID:
		build_uid_set(rset, setlen, sk->search, mb);
		break;

	case IST_SORT:
		result = db_search(rset, setlen, sk, mb);
		return result;
		break;

	case IST_HDRDATE_BEFORE:
	case IST_HDRDATE_SINCE:
	case IST_HDRDATE_ON:
	case IST_IDATE:
	case IST_FLAG:
	case IST_HDR:
		if ((result = db_search(rset, setlen, sk, mb)))
			return result;
		break;
	/* 
	 * these all have in common that all messages need to be parsed 
	 */
	case IST_DATA_BODY:
	case IST_DATA_TEXT:
	case IST_SIZE_LARGER:
	case IST_SIZE_SMALLER:
		result = db_search_parsed(rset, setlen, sk, mb, condition);
		break;

	case IST_SUBSEARCH_NOT:
	case IST_SUBSEARCH_AND:
		subtype = IST_SUBSEARCH_AND;

	case IST_SUBSEARCH_OR:

		if (! (newset = (unsigned int *)g_malloc0(sizeof(int) * setlen)))
			return -2;
		

		if (sk->type == IST_SUBSEARCH_AND)
			sk = g_tree_lookup(sk->sub_search,"and");
		if (sk->type == IST_SUBSEARCH_NOT)
			sk = g_tree_lookup(sk->sub_search,"not");
		if (sk->type == IST_SUBSEARCH_OR)
			sk = g_tree_lookup(sk->sub_search,"or");
		
		if (! (subtree = sk->sub_search)) {
			trace(TRACE_DEBUG,"%s,%s: no subtree", __FILE__, __func__);
			break;
		}
		
		keylist = g_tree_keys(subtree);
		while (keylist) {
			subsk = g_tree_lookup(subtree, keylist->data);	
			if (subsk) {
				int i;
				if (sk->type == IST_SUBSEARCH_OR)
					memset(newset, 0, sizeof(int) * setlen);
				else
					for (i = 0; i < setlen; i++)
						newset[i] = rset[i];
				
				if ((result = perform_imap_search(newset, setlen, subsk, mb, sk->type))) {
					dm_free(newset);
					return result;
				}

				if (! subsk->type == IST_SORT)
					combine_sets(rset, newset, setlen, subtype);
				else {
					for (i = 0; i < setlen; i++)
						rset[i] = newset[i];
				}
			}
			if (! g_list_next(keylist))
				break;
			keylist = g_list_next(keylist);
		}

		if (sk->type == IST_SUBSEARCH_NOT)
			invert_set(rset, setlen);

		break;

	default:
		dm_free(newset);
		return -2;	/* ??? */
	}

	dm_free(newset);
	return 0;
#endif
	
}

#ifdef OLD
static void invert_set(unsigned int *set, int setlen)
{
	int i;

	if (!set)
		return;

	for (i = 0; i < setlen; i++)
		set[i] = !set[i];
}


static void combine_sets(unsigned int *dest, unsigned int *sec, int setlen, int type)
{
	int i;

	if (!dest || !sec)
		return;

	if (type == IST_SUBSEARCH_AND) {
		for (i = 0; i < setlen; i++)
			dest[i] = (sec[i] && dest[i]);
	} else if (type == IST_SUBSEARCH_OR) {
		for (i = 0; i < setlen; i++)
			dest[i] = (sec[i] || dest[i]);
	}
}


/* 
 * build_set()
 *
 * builds a msn-set from a IMAP message set spec. the IMAP set is supposed to be correct,
 * no checks are performed.
 */
static void build_set(unsigned int *set, unsigned int setlen, char *cset)
{
	unsigned int i;
	u64_t num, num2;
	char *sep = NULL;

	if ((! set) || (! cset))
		return;

	memset(set, 0, setlen * sizeof(int));

	do {
		num = strtoull(cset, &sep, 10);
		if (num <= setlen && num > 0) {
			if (!*sep)
				set[num - 1] = 1;
			else if (*sep == ',') {
				set[num - 1] = 1;
				cset = sep + 1;
			} else {
				/* sep == ':' here */
				sep++;
				if (*sep == '*') {
					for (i = num - 1; i < setlen; i++)
						set[i] = 1;
					cset = sep + 1;
				} else {
					cset = sep;
					num2 = strtoull(cset, &sep, 10);

					if (num2 > setlen)
						num2 = setlen;
					if (num2 > 0) {
						/* NOTE: here: num2 > 0, num > 0 */
						if (num2 < num) {
							/* swap! */
							i = num;
							num = num2;
							num2 = i;
						}

						for (i = num - 1; i < num2; i++)
							set[i] = 1;
					}
					if (*sep)
						cset = sep + 1;
				}
			}
		} else if (*sep) {
			/* invalid char, skip it */
			cset = sep + 1;
			sep++;
		}
	} while (sep && *sep && cset && *cset);
}


/* 
 * build_uid_set()
 *
 * as build_set() but takes uid's instead of MSN's
 */
static void build_uid_set(unsigned int *set, unsigned int setlen, char *cset,
		   mailbox_t * mb)
{
	unsigned int i, msn, msn2;
	int result;
	int num2found = 0;
	u64_t num, num2;
	char *sep = NULL;

	if (!set)
		return;

	memset(set, 0, setlen * sizeof(int));

	if (!cset || setlen == 0)
		return;

	do {
		num = strtoull(cset, &sep, 10);
		result =
		    binary_search(mb->seq_list, mb->exists, num, &msn);

		if (result < 0 && num < mb->seq_list[mb->exists - 1]) {
			/* ok this num is not a UID, but if a range is specified (i.e. 1:*) 
			 * it is valid -> check *sep
			 */
			if (*sep == ':') {
				result = 1;
				for (msn = 0; mb->seq_list[msn] < num;
				     msn++);
				if (msn >= mb->exists)
					msn = mb->exists - 1;
			}
		}

		if (result >= 0) {
			if (!*sep)
				set[msn] = 1;
			else if (*sep == ',') {
				set[msn] = 1;
				cset = sep + 1;
			} else {
				/* sep == ':' here */
				sep++;
				if (*sep == '*') {
					for (i = msn; i < setlen; i++)
						set[i] = 1;

					cset = sep + 1;
				} else {
					/* fetch second number */
					cset = sep;
					num2 = strtoull(cset, &sep, 10);
					result =
					    binary_search(mb->seq_list,
							  mb->exists, num2,
							  &msn2);

					if (result < 0) {
						/* in a range: (like 1:1000) so this number doesnt need to exist;
						 * find the closest match below this UID value
						 */
						if (mb->exists == 0)
							num2found = 0;
						else {
							for (msn2 =
							     mb->exists -
							     1;; msn2--) {
								if (msn2 ==
								    0
								    && mb->
								    seq_list
								    [msn2]
								    > num2) {
									num2found
									    =
									    0;
									break;
								} else
								    if
								    (mb->
								     seq_list
								     [msn2]
								     <=
								     num2)
								{
									/* found! */
									num2found
									    =
									    1;
									break;
								}
							}
						}

					} else
						num2found = 1;

					if (num2found == 1) {
						if (msn2 < msn) {
							/* swap! */
							i = msn;
							msn = msn2;
							msn2 = i;
						}

						for (i = msn; i <= msn2;
						     i++)
							set[i] = 1;
					}

					if (*sep)
						cset = sep + 1;
				}
			}
		} else {
			/* invalid num, skip it */
			if (*sep) {
				cset = sep + 1;
				sep++;
			}
		}
	} while (sep && *sep && cset && *cset);
}

#endif
