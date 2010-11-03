/*
  Copyright (c) 2004-2006 NFG Net Facilities Group BV support@nfg.nl

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
#define THIS_MODULE "mailbox"
extern db_param_t _db_params;
#define DBPFX _db_params.pfx

/* internal utilities */


/* class methods */

struct DbmailMailbox * dbmail_mailbox_new(u64_t id)
{
	struct DbmailMailbox *self = g_new0(struct DbmailMailbox, 1);
	assert(self);
	dbmail_mailbox_set_id(self,id);
	dbmail_mailbox_set_uid(self, FALSE);
	self->search = NULL;
	self->fi = NULL;

	if (dbmail_mailbox_open(self)) {
		TRACE(TRACE_ERROR,"opening mailbox failed");
		dbmail_mailbox_free(self);
		return NULL;
	}

	return self;
}

static gboolean _node_free(GNode *node, gpointer dummy UNUSED)
{
	search_key_t *s = (search_key_t *)node->data;
	if (s->found)
		g_tree_destroy(s->found);
	g_free(s);

	return FALSE;
}

void dbmail_mailbox_free(struct DbmailMailbox *self)
{

	if (self->sorted)
		g_list_destroy(self->sorted);
	if (self->ids)
		g_tree_destroy(self->ids);		
	if (self->msn)
		g_tree_destroy(self->msn);

	if (self->search) {
		g_node_traverse(g_node_get_root(self->search), G_POST_ORDER, G_TRAVERSE_ALL, -1, 
			(GNodeTraverseFunc)_node_free, NULL);
		g_node_destroy(self->search);
	}
	if (self->fi) {
		if (self->fi->bodyfetch)
			// FIXME: Should we be using dbmail-imapsession.c: _body_fetch_free?
			g_list_foreach(self->fi->bodyfetch, (GFunc)g_free, NULL);
		g_free(self->fi);
		self->fi = NULL;
	}
	if (self->charset) {
		g_free(self->charset);
		self->charset = NULL;
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

void dbmail_mailbox_set_uid(struct DbmailMailbox *self, gboolean uid)
{
	self->uid = uid;
}

gboolean dbmail_mailbox_get_uid(struct DbmailMailbox *self)
{
	return self->uid;
}

static void uid_msn_map(struct DbmailMailbox *self)
{
	GList *ids = NULL;
	u64_t *id, *msn = NULL;

	ids = g_tree_keys(self->ids);

	if (self->msn)
		g_tree_destroy(self->msn);

	self->msn = g_tree_new_full((GCompareDataFunc)ucmp,NULL,NULL,NULL);
	self->rows = 1;

	ids = g_list_first(ids);
	while (ids) {
		id = (u64_t *)ids->data;
		msn = g_tree_lookup(self->ids, id);
		*msn = self->rows++;

		g_tree_insert(self->msn, msn, id);

		if (! g_list_next(ids))
			break;
		ids = g_list_next(ids);

	}

	g_list_free(g_list_first(ids));

	TRACE(TRACE_DEBUG,"total [%d] UIDs", g_tree_nnodes(self->ids));
	TRACE(TRACE_DEBUG,"total [%d] MSNs", g_tree_nnodes(self->msn));
}
	
void mailbox_uid_msn_new(struct DbmailMailbox *self)
{
	if (self->ids)
		g_tree_destroy(self->ids);

	if (self->msn)
		g_tree_destroy(self->msn);

	self->ids = NULL;
	self->msn = NULL;

	self->ids = g_tree_new_full((GCompareDataFunc)ucmp,NULL,(GDestroyNotify)g_free,(GDestroyNotify)g_free);
	self->msn = g_tree_new_full((GCompareDataFunc)ucmp,NULL,NULL,NULL);
	self->rows = 1;
}

static void mailbox_build_uid_map(struct DbmailMailbox *self)
{
	int i, rows;
	u64_t *id, *msn;

	mailbox_uid_msn_new(self);

	rows = db_num_rows();
	for (i=0; i< rows; i++) {
		id = g_new0(u64_t,1);
		*id = db_get_result_u64(i,0);

		msn = g_new0(u64_t,1);
		*msn = i+1;

		g_tree_insert(self->ids,id,msn);
		g_tree_insert(self->msn,msn,id);
	}
	
	TRACE(TRACE_DEBUG,"ids [%d], msn [%d]", g_tree_nnodes(self->ids), g_tree_nnodes(self->msn));
}

int dbmail_mailbox_open(struct DbmailMailbox *self)
{
	GString *q = g_string_new("");

	g_string_printf(q, "SELECT message_idnr FROM %smessages "
		 "WHERE mailbox_idnr = %llu "
		 "AND status IN (%d,%d) "
		 "ORDER BY message_idnr", DBPFX, 
		 dbmail_mailbox_get_id(self), 
		 MESSAGE_STATUS_NEW, MESSAGE_STATUS_SEEN);
	
	if (db_query(q->str) == DM_EQUERY) {
		g_string_free(q,TRUE);
		return DM_EQUERY;
	}

	g_string_free(q,TRUE);

	mailbox_build_uid_map(self);

	db_free_result();
	return DM_SUCCESS;
}

int dbmail_mailbox_remove_uid(struct DbmailMailbox *self, u64_t *id)
{
	if (! g_tree_remove(self->ids, id)) {
		TRACE(TRACE_ERROR,"trying to remove unknown UID [%llu]", *id);
		return DM_EGENERAL;
	}

	uid_msn_map(self);

	return DM_SUCCESS;
}

#define FROM_STANDARD_DATE "Tue Oct 11 13:06:24 2005"
static gchar * _message_get_envelope_date(const struct DbmailMessage *message)
{
	char *res;
	struct tm gmt;
	assert(message->internal_date);

	res = g_new0(char, TIMESTRING_SIZE+1);
	memset(&gmt,'\0', sizeof(struct tm));
	gmtime_r(&message->internal_date, &gmt);

	strftime(res, TIMESTRING_SIZE, "%a %b %d %H:%M:%S %Y", &gmt);
	return res;
}


static size_t dump_message_to_stream(struct DbmailMessage *message, GMimeStream *ostream)
{
	size_t r = 0;
	gchar *s, *d;
	GString *sender;
	GString *date;
	InternetAddressList *ialist;
	InternetAddress *ia;
	GMimeStream *fstream;
	GMimeFilter *filter;
	GString *t;
	
	g_return_val_if_fail(GMIME_IS_MESSAGE(message->content),0);

	s = dbmail_message_to_string(message);

	if (! strncmp(s,"From ",5)==0) {
		ialist = internet_address_parse_string(g_mime_message_get_sender(GMIME_MESSAGE(message->content)));
		sender = g_string_new("nobody@foo");
		if (ialist) {
			ia = ialist->address;
			if (ia) {
				g_strstrip(g_strdelimit(ia->value.addr,"\"",' '));
				g_string_printf(sender,"%s", ia->value.addr);
			}
		}
		internet_address_list_destroy(ialist);
		
		d = _message_get_envelope_date(message);
		date = g_string_new(d);
		g_free(d);
		if (date->len < 1)
			date = g_string_new(FROM_STANDARD_DATE);
		
		t = g_string_new("From ");
		g_string_append_printf(t,"%s %s\n", sender->str, date->str);

		r = g_mime_stream_write_string(ostream,t->str);

		g_string_free(t,TRUE);
		g_string_free(sender,TRUE);
		g_string_free(date,TRUE);
		
	}
	
	fstream = g_mime_stream_filter_new_with_stream(ostream);
	filter = g_mime_filter_from_new(GMIME_FILTER_FROM_MODE_DEFAULT);
	g_mime_stream_filter_add((GMimeStreamFilter *) fstream, filter);
	g_object_unref(filter);

	r += g_mime_stream_write_string(fstream,s);
	r += g_mime_stream_write_string(fstream,"\n");

	g_object_unref(fstream);
	g_free(s);
	return r;
}

/* Caller must fclose the file pointer itself. */
int dbmail_mailbox_dump(struct DbmailMailbox *self, FILE *file)
{
	unsigned i,j;
	int count=0;
	gboolean h;
	GMimeStream *ostream;
	GList *ids, *cids = NULL, *slice, *topslice;
	struct DbmailMessage *message = NULL;
	GString *q, *t;
	char *internal_date = NULL;
	char * date2char;

	if (self->ids==NULL || g_tree_nnodes(self->ids) == 0) {
		TRACE(TRACE_DEBUG,"cannot dump empty mailbox");
		return 0;
	}
	
	assert(self->ids);

	q = g_string_new("");
	t = g_string_new("");
	ostream = g_mime_stream_file_new(file);
	g_mime_stream_file_set_owner ((GMimeStreamFile *)ostream, FALSE);

	ids = g_tree_keys(self->ids);

	while (ids) {
		cids = g_list_append(cids,g_strdup_printf("%llu", *(u64_t *)ids->data));
		if (! g_list_next(ids))
			break;
		ids = g_list_next(ids);
	}
	
	topslice = g_list_slices(cids,100);
	slice = g_list_first(topslice);

	g_list_destroy(cids);
	
	g_list_free(g_list_first(ids));
	date2char = date2char_str("internal_date");

	while (slice) {
		g_string_printf(q,"SELECT is_header,messageblk,%s FROM %smessageblks b "
				"JOIN %sphysmessage p ON b.physmessage_id = p.id "
				"JOIN %smessages m ON m.physmessage_id = p.id "
				"WHERE message_idnr IN (%s) "
				"ORDER BY messageblk_idnr ",
				date2char,
				DBPFX, DBPFX, DBPFX,
				(char *)slice->data);
		
		if (db_query(q->str) == -1) {
			g_string_free(t,TRUE);
			g_string_free(q,TRUE);
			g_object_unref(ostream);
			g_list_destroy(topslice);
			return -1;
		}

		if ((j = db_num_rows()) < 1)
			break;
		
		for (i=0; i<j; i++) {
			h = db_get_result_int(i,0);
			if (h) {
				if (t->len > 0) {
					message = dbmail_message_new();
					message = dbmail_message_init_with_string(message,t);
					if (internal_date && strlen(internal_date))
						dbmail_message_set_internal_date(message, internal_date);
					if(dump_message_to_stream(message,ostream) > 0)
						count++;
					dbmail_message_free(message);
				}
				if (internal_date) g_free(internal_date);
				internal_date = g_strdup(db_get_result(i,2));
				g_string_printf(t,"%s", db_get_result(i,1));
			} else {
				g_string_append(t, db_get_result(i,1));
			}
		}
		db_free_result();

		if (! g_list_next(slice))
			break;
		
		slice = g_list_next(slice);
	}
	g_free(date2char);
	
	if (t->len) {
		message = dbmail_message_new();
		message = dbmail_message_init_with_string(message,t);
		dbmail_message_set_internal_date(message, internal_date);
		if (internal_date) g_free(internal_date);
		if (dump_message_to_stream(message, ostream) > 0)
			count++;
		dbmail_message_free(message);
	}
	 
	g_list_destroy(topslice);
	g_string_free(t,TRUE);
	g_string_free(q,TRUE);
	g_object_unref(ostream);
	
	return count;
}

static gboolean _tree_foreach(gpointer key UNUSED, gpointer value, GString * data)
{
	gboolean res = FALSE;
	u64_t *id;
	GList *sublist = g_list_first((GList *)value);
	GString *t = g_string_new("");
	int m = g_list_length(sublist);
	
	sublist = g_list_first(sublist);
	while(sublist) {
		id = sublist->data;
		g_string_append_printf(t, "(%llu)", *id);
		
		if (! g_list_next(sublist))
			break;
		sublist = g_list_next(sublist);
	}
	if (m > 1)
		g_string_append_printf(data, "(%s)", t->str);
	else
		g_string_append_printf(data, "%s", t->str);

	g_string_free(t,TRUE);

	return res;
}

char * dbmail_mailbox_orderedsubject(struct DbmailMailbox *self)
{
	GList *sublist = NULL;
	GString *q = g_string_new("");
	u64_t i = 0, r = 0, idnr = 0;
	char *subj;
	char *res = NULL;
	u64_t *id, *msn;
	GTree *tree;
	GString *threads;
	
	/* thread-roots (ordered) */
	g_string_printf(q, "SELECT min(message_idnr),subjectfield "
			"FROM %smessages "
			"JOIN %ssubjectfield USING (physmessage_id) "
			"JOIN %sdatefield USING (physmessage_id) "
			"WHERE mailbox_idnr=%llu "
			"AND status IN (%d, %d) "
			"GROUP BY subjectfield",
			DBPFX, DBPFX, DBPFX,
			dbmail_mailbox_get_id(self),
			MESSAGE_STATUS_NEW, MESSAGE_STATUS_SEEN);

	if (db_query(q->str) == DM_EQUERY) {
		g_string_free(q,TRUE);
		return res;
	}
	if ((r = db_num_rows())==0) {
		g_string_free(q,TRUE);
		db_free_result();
		return res;
	}
	
	tree = g_tree_new_full((GCompareDataFunc)strcmp,NULL,(GDestroyNotify)g_free, NULL);
	
	i=0;
	while (i < r) {
		idnr = db_get_result_u64(i,0);
		if (! g_tree_lookup(self->ids,&idnr)) {
			i++;
			continue;
		}
		subj = (char *)db_get_result(i,1);
		g_tree_insert(tree,g_strdup(subj), NULL);
		i++;
	}
	db_free_result();
		
	/* full threads (unordered) */
	g_string_printf(q, "SELECT message_idnr,subjectfield "
			"FROM %smessages "
			"JOIN %ssubjectfield using (physmessage_id) "
			"JOIN %sdatefield using (physmessage_id) "
			"WHERE mailbox_idnr=%llu "
			"AND status IN (%d,%d) "
			"ORDER BY subjectfield,datefield", 
			DBPFX, DBPFX, DBPFX,
			dbmail_mailbox_get_id(self),
			MESSAGE_STATUS_NEW,MESSAGE_STATUS_SEEN);
		
	if (db_query(q->str) == DM_EQUERY) {
		g_string_free(q,TRUE);
		return res;
	}
	if ((r = db_num_rows())==0) {
		g_string_free(q,TRUE);
		db_free_result();
		return res;
	}
	
	i=0;
	while (i < r) {
		idnr = db_get_result_u64(i,0);
		if (! (msn = g_tree_lookup(self->ids, &idnr))) {
			i++;
			continue;
		}
		subj = (char *)db_get_result(i,1);
		
		id = g_new0(u64_t,1);
		if (dbmail_mailbox_get_uid(self))
			*id = idnr;
		else
			*id = *msn;
		
		sublist = g_tree_lookup(tree,(gconstpointer)subj);
		sublist = g_list_append(sublist,id);
		g_tree_insert(tree,g_strdup(subj),sublist);
		i++;
	}
	db_free_result();

	threads = g_string_new("");
	g_tree_foreach(tree,(GTraverseFunc)_tree_foreach,threads);
	res = threads->str;

	g_string_free(threads,FALSE);
	g_string_free(q,TRUE);

	return res;
}

/*
 * return self->ids as a string
 */
char * dbmail_mailbox_ids_as_string(struct DbmailMailbox *self) 
{
	GString *t;
	gchar *s = NULL;
	GList *l = NULL, *h = NULL;

	if ((self->ids == NULL) || g_tree_nnodes(self->ids) <= 0) {
		TRACE(TRACE_DEBUG,"no ids found");
		return s;
	}

	t = g_string_new("");
	switch (dbmail_mailbox_get_uid(self)) {
		case TRUE:
			l = g_tree_keys(self->ids);
		break;
		case FALSE:
			l = g_tree_values(self->ids);
		break;
	}

	h = l;

	while(l->data) {
		g_string_append_printf(t,"%llu ", *(u64_t *)l->data);
		if (! g_list_next(l))
			break;
		l = g_list_next(l);
	}

	g_list_free(h);

	s = t->str;
	g_string_free(t,FALSE);
	
	return g_strchomp(s);
	
}
char * dbmail_mailbox_sorted_as_string(struct DbmailMailbox *self) 
{
	GString *t;
	gchar *s = NULL;
	GList *l = NULL;
	gboolean uid;
	u64_t *msn;

	l = g_list_first(self->sorted);
	if (! g_list_length(l)>0)
		return s;

	t = g_string_new("");
	uid = dbmail_mailbox_get_uid(self);

	while(l->data) {
		msn = g_tree_lookup(self->ids, l->data);
		if (msn) {
			if (uid)
				g_string_append_printf(t,"%llu ", *(u64_t *)l->data);
			else
				g_string_append_printf(t,"%llu ", *(u64_t *)msn);
		}
		if (! g_list_next(l))
			break;
		l = g_list_next(l);
	}

	s = t->str;
	g_string_free(t,FALSE);

	return g_strchomp(s);
}


/* imap sorted search */
static int append_search(struct DbmailMailbox *self, search_key_t *value, gboolean descend)
{
	GNode *n;
	
	if (self->search)
		n = g_node_append_data(self->search, value);
	else {
		descend = TRUE;
		n = g_node_new(value);
	}
	
	if (descend)
		self->search = n;
	
	TRACE(TRACE_DEBUG, "[%p] leaf [%d] type [%d] field [%s] search [%s] at depth [%u]\n", value, G_NODE_IS_LEAF(n), 
			value->type, value->hdrfld, value->search, 
			g_node_depth(self->search));
	return 0;
}

static void _append_join(char *join, char *table)
{
	char *tmp;
	TRACE(TRACE_DEBUG,"%s", table);
	tmp = g_strdup_printf("LEFT JOIN %s%s ON m.physmessage_id=%s%s.physmessage_id ", DBPFX, table, DBPFX, table);
	g_strlcat(join, tmp, MAX_SEARCH_LEN);
	g_free(tmp);
}

static void _append_sort(char *order, char *field, gboolean reverse)
{
	char *tmp;
	tmp = g_strdup_printf("%s%s,", field, reverse ? " DESC" : "");
	TRACE(TRACE_DEBUG,"%s", tmp);
	g_strlcat(order, tmp, MAX_SEARCH_LEN);
	g_free(tmp);
}

static int _handle_sort_args(struct DbmailMailbox *self, char **search_keys, search_key_t *value, u64_t *idx)
{
	value->type = IST_SORT;
			
	gboolean reverse = FALSE;

	if (! (search_keys && search_keys[*idx]))
		return -1;

	char *key = search_keys[*idx];
	
	if ( MATCH(key, "reverse") ) {
		reverse = TRUE;
		(*idx)++;
		key = search_keys[*idx];
	} 
	
	if ( MATCH(key, "arrival") ) {
		_append_sort(value->order, "internal_date", reverse);
		(*idx)++;
	} 
	
	else if ( MATCH(key, "size") ) {
		_append_sort(value->order, "messagesize", reverse);
		(*idx)++;
	} 
	
	else if ( MATCH(key, "from") ) {
		_append_join(value->table, "fromfield");
		_append_sort(value->order, "fromaddr", reverse);	
		(*idx)++;
	} 
	
	else if ( MATCH(key, "subject") ) {
		_append_join(value->table, "subjectfield");
		_append_sort(value->order, "subjectfield", reverse);
		(*idx)++;
	} 
	
	else if ( MATCH(key, "cc") ) {
		_append_join(value->table, "ccfield");
		_append_sort(value->order, "ccaddr", reverse);
		(*idx)++;
	} 
	
	else if ( MATCH(key, "to") ) {
		_append_join(value->table, "tofield");
		_append_sort(value->order, "toaddr", reverse);
		(*idx)++;
	} 
	
	else if ( MATCH(key, "date") ) {
		_append_join(value->table, "datefield");
		_append_sort(value->order, "datefield", reverse);
		(*idx)++;
	}	

	else if ( MATCH(key, "(") )
		(*idx)++;

	else if ( MATCH(key, ")") ) 
		(*idx)++;
	
	else if ( MATCH(key, "utf-8") )  {
		(*idx)++;
		append_search(self, value, 0);
		return 1;
	}
	
	else if ( MATCH(key, "us-ascii") ) {
		(*idx)++;
		append_search(self, value, 0);
		return 1;
	}
	
	else if ( MATCH(key, "iso-8859-1") ) {
		(*idx)++;
		append_search(self, value, 0);
		return 1;
	}

	else
		return -1; /* done */

	return 0;
}

static void pop_search(struct DbmailMailbox *self)
{
	// switch back to parent 
	if (self->search && self->search->parent) 
		self->search = self->search->parent;
}

static int _handle_search_args(struct DbmailMailbox *self, char **search_keys, u64_t *idx)
{
	int result = 0;

	if (! (search_keys && search_keys[*idx]))
		return 1;

	char *p = NULL, *t = NULL, *key = search_keys[*idx];

	search_key_t *value = g_new0(search_key_t,1);
	
	/* SEARCH */

	if ( MATCH(key, "all") ) {
		TRACE(TRACE_DEBUG,"IST_UIDSET: 1:*");
		value->type = IST_UIDSET;
		strcpy(value->search, "1:*");
		(*idx)++;
		
	} 
	
	else if ( MATCH(key, "uid") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		g_return_val_if_fail(check_msg_set(search_keys[*idx + 1]),-1);
		value->type = IST_UIDSET;
		(*idx)++;
		TRACE(TRACE_DEBUG,"IST_UIDSET: %s", search_keys[(*idx)]);
		strncpy(value->search, search_keys[(*idx)], MAX_SEARCH_LEN);
		(*idx)++;
	}

	/*
	 * FLAG search keys
	 */

	else if ( MATCH(key, "answered") ) {
		value->type = IST_FLAG;
		strncpy(value->search, "answered_flag=1", MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if ( MATCH(key, "deleted") ) {
		value->type = IST_FLAG;
		strncpy(value->search, "deleted_flag=1", MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if ( MATCH(key, "flagged") ) {
		value->type = IST_FLAG;
		strncpy(value->search, "flagged_flag=1", MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if ( MATCH(key, "recent") ) {
		value->type = IST_FLAG;
		strncpy(value->search, "recent_flag=1", MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if ( MATCH(key, "seen") ) {
		value->type = IST_FLAG;
		strncpy(value->search, "seen_flag=1", MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if ( MATCH(key, "keyword") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		value->type = IST_SET;
		(*idx)++;
		strcpy(value->search, "0");
		(*idx)++;
		
	} else if ( MATCH(key, "draft") ) {
		value->type = IST_FLAG;
		strncpy(value->search, "draft_flag=1", MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if ( MATCH(key, "new") ) {
		value->type = IST_FLAG;
		strncpy(value->search, "(seen_flag=0 AND recent_flag=1)", MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if ( MATCH(key, "old") ) {
		value->type = IST_FLAG;
		strncpy(value->search, "recent_flag=0", MAX_SEARCH_LEN);
		(*idx)++;
		
	} else if ( MATCH(key, "unanswered") ) {
		value->type = IST_FLAG;
		strncpy(value->search, "answered_flag=0", MAX_SEARCH_LEN);
		(*idx)++;

	} else if ( MATCH(key, "undeleted") ) {
		value->type = IST_FLAG;
		strncpy(value->search, "deleted_flag=0", MAX_SEARCH_LEN);
		(*idx)++;
	
	} else if ( MATCH(key, "unflagged") ) {
		value->type = IST_FLAG;
		strncpy(value->search, "flagged_flag=0", MAX_SEARCH_LEN);
		(*idx)++;
	
	} else if ( MATCH(key, "unseen") ) {
		value->type = IST_FLAG;
		strncpy(value->search, "seen_flag=0", MAX_SEARCH_LEN);
		(*idx)++;
	
	} else if ( MATCH(key, "unkeyword") ) {
		g_return_val_if_fail(search_keys[(*idx) + 1],-1);
		value->type = IST_SET;
		(*idx)++;
		strcpy(value->search, "1:*");
		(*idx)++;
	
	} else if ( MATCH(key, "undraft") ) {
		value->type = IST_FLAG;
		strncpy(value->search, "draft_flag=0", MAX_SEARCH_LEN);
		(*idx)++;
	
	}

	/*
	 * HEADER search keys
	 */
#define IMAP_SET_SEARCH		(*idx)++; \
		if ((p = dbmail_iconv_str_to_db((const char *)search_keys[*idx], self->charset)) != NULL) {  \
			t = dm_stresc(p); \
			g_free(p); \
		} else { \
			TRACE(TRACE_WARNING, "search_key [%s] is not charset [%s]", search_keys[*idx], self->charset); \
			t = dm_stresc(search_keys[*idx]); \
		} \
		strncpy(value->search, t, MAX_SEARCH_LEN); \
		g_free(t); \
		(*idx)++
	
	else if ( MATCH(key, "bcc") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		value->type = IST_HDR;
		strncpy(value->hdrfld, "bcc", MIME_FIELD_MAX);
		IMAP_SET_SEARCH;
		
	} else if ( MATCH(key, "cc") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		value->type = IST_HDR;
		strncpy(value->hdrfld, "cc", MIME_FIELD_MAX);
		IMAP_SET_SEARCH;
	
	} else if ( MATCH(key, "from") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		value->type = IST_HDR;
		strncpy(value->hdrfld, "from", MIME_FIELD_MAX);
		IMAP_SET_SEARCH;
	
	} else if ( MATCH(key, "to") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		value->type = IST_HDR;
		strncpy(value->hdrfld, "to", MIME_FIELD_MAX);
		IMAP_SET_SEARCH;
	
	} else if ( MATCH(key, "subject") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		value->type = IST_HDR;
		strncpy(value->hdrfld, "subject", MIME_FIELD_MAX);
		IMAP_SET_SEARCH;
	
	} else if ( MATCH(key, "header") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		g_return_val_if_fail(search_keys[*idx + 2], -1);
		value->type = IST_HDR;
		t = dm_stresc(search_keys[*idx + 1]);
		strncpy(value->hdrfld, t, MIME_FIELD_MAX);
		g_free(t);
		t = dm_stresc(search_keys[*idx + 2]);
		strncpy(value->search, t, MAX_SEARCH_LEN);
		g_free(t);
		(*idx) += 3;

	} else if ( MATCH(key, "sentbefore") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		value->type = IST_HDRDATE_BEFORE;
		strncpy(value->hdrfld, "datefield", MIME_FIELD_MAX);
		IMAP_SET_SEARCH;

	} else if ( MATCH(key, "senton") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		value->type = IST_HDRDATE_ON;
		strncpy(value->hdrfld, "datefield", MIME_FIELD_MAX);
		IMAP_SET_SEARCH;

	} else if ( MATCH(key, "sentsince") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		value->type = IST_HDRDATE_SINCE;
		strncpy(value->hdrfld, "datefield", MIME_FIELD_MAX);
		IMAP_SET_SEARCH;
	}

	/*
	 * INTERNALDATE keys
	 */

	else if ( MATCH(key, "before") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		g_return_val_if_fail(check_date(search_keys[*idx + 1]),-1);
		value->type = IST_IDATE;
		(*idx)++;
		g_snprintf(value->search, MAX_SEARCH_LEN, "internal_date < '%s'", date_imap2sql(search_keys[*idx]));
		(*idx)++;
		
	} else if ( MATCH(key, "on") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		g_return_val_if_fail(check_date(search_keys[*idx + 1]),-1);
		value->type = IST_IDATE;
		(*idx)++;
		g_snprintf(value->search, MAX_SEARCH_LEN, "internal_date %s '%s%%'", 
				db_get_sql(SQL_SENSITIVE_LIKE), date_imap2sql(search_keys[*idx]));
		(*idx)++;
		
	} else if ( MATCH(key, "since") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		g_return_val_if_fail(check_date(search_keys[*idx + 1]),-1);
		value->type = IST_IDATE;
		(*idx)++;
		g_snprintf(value->search, MAX_SEARCH_LEN, "internal_date > '%s'", date_imap2sql(search_keys[*idx]));
		(*idx)++;
	}

	/*
	 * DATA-keys
	 */

	else if ( MATCH(key, "body") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		value->type = IST_DATA_BODY;
		IMAP_SET_SEARCH;

	} else if ( MATCH(key, "text") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		value->type = IST_DATA_TEXT;
		IMAP_SET_SEARCH;
	}

	/*
	 * SIZE keys
	 */

	else if ( MATCH(key, "larger") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		value->type = IST_SIZE_LARGER;
		(*idx)++;
		value->size = strtoull(search_keys[(*idx)], NULL, 10);
		(*idx)++;
	
	} else if ( MATCH(key, "smaller") ) {
		g_return_val_if_fail(search_keys[*idx + 1], -1);
		value->type = IST_SIZE_SMALLER;
		(*idx)++;
		value->size = strtoull(search_keys[(*idx)], NULL, 10);
		(*idx)++;
	
	}

	/*
	 * NOT, OR, ()
	 */
	
	else if ( MATCH(key, "not") ) {
		char *nextkey;

		g_return_val_if_fail(search_keys[*idx + 1], -1);
		nextkey = search_keys[*idx+1];

		if ( MATCH(nextkey, "answered") ) {
			value->type = IST_FLAG;
			strncpy(value->search, "answered_flag=0", MAX_SEARCH_LEN);
			(*idx)+=2;
			
		} else if ( MATCH(nextkey, "deleted") ) {
			value->type = IST_FLAG;
			strncpy(value->search, "deleted_flag=0", MAX_SEARCH_LEN);
			(*idx)+=2;
			
		} else if ( MATCH(nextkey, "flagged") ) {
			value->type = IST_FLAG;
			strncpy(value->search, "flagged_flag=0", MAX_SEARCH_LEN);
			(*idx)+=2;
			
		} else if ( MATCH(nextkey, "recent") ) {
			value->type = IST_FLAG;
			strncpy(value->search, "recent_flag=0", MAX_SEARCH_LEN);
			(*idx)+=2;
			
		} else if ( MATCH(nextkey, "seen") ) {
			value->type = IST_FLAG;
			strncpy(value->search, "seen_flag=0", MAX_SEARCH_LEN);
			(*idx)+=2;
			
		} else if ( MATCH(nextkey, "draft") ) {
			value->type = IST_FLAG;
			strncpy(value->search, "draft_flag=0", MAX_SEARCH_LEN);
			(*idx)+=2;
			
		} else if ( MATCH(nextkey, "new") ) {
			value->type = IST_FLAG;
			strncpy(value->search, "(seen_flag=1 AND recent_flag=0)", MAX_SEARCH_LEN);
			(*idx)+=2;
			
		} else if ( MATCH(nextkey, "old") ) {
			value->type = IST_FLAG;
			strncpy(value->search, "recent_flag=1", MAX_SEARCH_LEN);
			(*idx)+=2;
			
		} else {
			value->type = IST_SUBSEARCH_NOT;
			(*idx)++;
		
			append_search(self, value, 1);
			if ((result = _handle_search_args(self, search_keys, idx)) < 0)
				return result;
			pop_search(self);

			return 0;
		}
			
	} else if ( MATCH(key, "or") ) {
		value->type = IST_SUBSEARCH_OR;
		(*idx)++;
		
		append_search(self, value, 1);
		if ((result = _handle_search_args(self, search_keys, idx)) < 0)
			return result;
		if ((result = _handle_search_args(self, search_keys, idx)) < 0)
			return result;
		pop_search(self);
		
		return 0;

	} else if ( MATCH(key, "(") ) {
		value->type = IST_SUBSEARCH_AND;
		(*idx)++;
		
		append_search(self,value,1);
		while ((result = dbmail_mailbox_build_imap_search(self, search_keys, idx, 0)) == 0);
		pop_search(self);
		
		return 0;

	} else if ( MATCH(key, ")") ) {
		(*idx)++;
		g_free(value);
		return 1;
	
	} else if (check_msg_set(key)) {
		TRACE(TRACE_DEBUG,"IST_SET: %s", key);
		value->type = IST_SET;
		strncpy(value->search, key, MAX_SEARCH_LEN);
		(*idx)++;
		
	/* ignore the charset. Let the database handle this */
        } else if ( MATCH(key, "charset") )  {
                (*idx)++;// FIXME: check for valid charset here
		self->charset = g_strdup(search_keys[*idx]);
		TRACE(TRACE_DEBUG,"using charset [%s] for searching", self->charset);
                (*idx)++; 
	} else {
		/* unknown search key */
		TRACE(TRACE_DEBUG,"unknown search key [%s]", key);
		g_free(value);
		return -1;
	}
	
	if (value->type)
		append_search(self, value, 0);
	else
		g_free(value);

	return 0;
}

/*
 * build_imap_search()
 *
 * builds a linked list of search items from a set of IMAP search keys
 * sl should be initialized; new search items are simply added to the list
 *
 * returns -1 on syntax error, -2 on memory error; 0 on success, 1 if ')' has been encountered
 */
int dbmail_mailbox_build_imap_search(struct DbmailMailbox *self, char **search_keys, u64_t *idx, search_order_t order)
{
	int result = 0;
	search_key_t * value, * s;
	
	if (! (search_keys && search_keys[*idx]))
		return 1;

	/* default initial key for ANDing */
	value = g_new0(search_key_t,1);
	value->type = IST_SET;

	if (check_msg_set(search_keys[*idx])) {
		strncpy(value->search, search_keys[*idx], MAX_SEARCH_LEN);
		(*idx)++;
	} else {
		/* match all messages if no initial sequence set is defined */
		strncpy(value->search, "1:*", MAX_SEARCH_LEN);
	}
	append_search(self, value, 0);

	/* SORT */
	switch (order) {
		case SEARCH_SORTED:
			value = g_new0(search_key_t,1);
			value->type = IST_SORT;
			s = value;
			while(((result = _handle_sort_args(self, search_keys, value, idx)) == 0) && search_keys[*idx]);
			if (result < 0)
				g_free(s);
		break;
		case SEARCH_THREAD_ORDEREDSUBJECT:
		case SEARCH_THREAD_REFERENCES:
			(*idx)++;
			TRACE(TRACE_DEBUG,"search_key: [%s]", search_keys[*idx]);
			// eat the charset arg
			if (MATCH(search_keys[*idx],"utf-8"))
				(*idx)++;
			else if (MATCH(search_keys[*idx],"us-ascii"))
				(*idx)++;
			else if (MATCH(search_keys[*idx],"iso-8859-1"))
				(*idx)++;
			else
				return -1;

		break;
		case SEARCH_UNORDERED:
		default:
		// ignore
		break;

	} 

	/* SEARCH */
	while( search_keys[*idx] && ((result = _handle_search_args(self, search_keys, idx)) == 0) );
	
	TRACE(TRACE_DEBUG,"done [%d] at idx [%llu]", result, *idx);
	return result;
}


static gboolean _do_sort(GNode *node, struct DbmailMailbox *self)
{
	GString *q;
	u64_t tid, *id;
	unsigned i, rows;
	GTree *z;
	search_key_t *s = (search_key_t *)node->data;
	
	TRACE(TRACE_DEBUG,"type [%d]", s->type);

	if (s->type != IST_SORT)
		return FALSE;
	
	if (s->searched)
		return FALSE;

	q = g_string_new("");
	g_string_printf(q, "SELECT message_idnr FROM %smessages m "
			 "LEFT JOIN %sphysmessage p ON m.physmessage_id=p.id "
			 "%s"
			 "WHERE m.mailbox_idnr = %llu AND m.status IN (%d,%d) " 
			 "ORDER BY %smessage_idnr", DBPFX, DBPFX, s->table,
			 dbmail_mailbox_get_id(self), MESSAGE_STATUS_NEW, MESSAGE_STATUS_SEEN, s->order);

	if (db_query(q->str) == -1)
		return TRUE;

        if (self->sorted) {
                g_list_destroy(self->sorted);
                self->sorted = NULL;
        }

	z = g_tree_new((GCompareFunc)ucmp);
	
	rows = db_num_rows();
	for (i=0; i< rows; i++) {
		tid = db_get_result_u64(i,0);
		if (g_tree_lookup(self->ids,&tid) && (! g_tree_lookup(z, &tid))) {
			id = g_new0(u64_t,1);
			*id = tid;
			g_tree_insert(z, id, id);
			self->sorted = g_list_prepend(self->sorted,id);
		}
	}

	g_tree_destroy(z);

        self->sorted = g_list_reverse(self->sorted);

	g_string_free(q,TRUE);
	db_free_result();

	s->searched = TRUE;
	
	return FALSE;
}
static GTree * mailbox_search(struct DbmailMailbox *self, search_key_t *s)
{
	unsigned i, rows;
	char *qs, *date, *field;
	u64_t *k, *v, *w;
	u64_t id;
	char gt_lt = 0;
	const char *op;
	char partial[DEF_FRAGSIZE];
	
	GString *t;
	GString *q;

	if (!s->search)
		return NULL;

	t = g_string_new("");
	q = g_string_new("");
	switch (s->type) {
		case IST_HDRDATE_ON:
		case IST_HDRDATE_SINCE:
		case IST_HDRDATE_BEFORE:
		
		field = g_strdup_printf(db_get_sql(SQL_TO_DATE), s->hdrfld);
		qs = g_strdup_printf("'%s'", date_imap2sql(s->search));
		date = g_strdup_printf(db_get_sql(SQL_TO_DATE), qs);
		g_free(qs);

		if (s->type == IST_HDRDATE_SINCE)
			op = ">=";
		else if (s->type == IST_HDRDATE_BEFORE)
			op = "<";
		else
			op = "=";

		g_string_printf(t,"%s %s %s", field, op, date);
		g_free(date);
		g_free(field);
		
		g_string_printf(q,"SELECT message_idnr FROM %smessages m "
			"JOIN %sphysmessage p ON m.physmessage_id=p.id "
			"JOIN %sdatefield d ON d.physmessage_id=p.id "
			"WHERE mailbox_idnr= %llu AND status IN (%d,%d) "
			"AND %s "
			"ORDER BY message_idnr", DBPFX, DBPFX, DBPFX,
			dbmail_mailbox_get_id(self), 
			MESSAGE_STATUS_NEW, MESSAGE_STATUS_SEEN, 
			t->str);
			break;
			
		case IST_HDR:
		
		memset(partial,0,sizeof(partial));
		snprintf(partial, DEF_FRAGSIZE, db_get_sql(SQL_PARTIAL), "headervalue");
		g_string_printf(q, "SELECT message_idnr FROM %smessages m "
			 "JOIN %sphysmessage p ON m.physmessage_id=p.id "
			 "JOIN %sheadervalue v ON v.physmessage_id=p.id "
			 "JOIN %sheadername n ON v.headername_id=n.id "
			 "WHERE mailbox_idnr = %llu "
			 "AND status IN (%d,%d) "
			 "AND headername %s '%s' AND %s %s '%%%s%%' "
			 "ORDER BY message_idnr", 
			 DBPFX, DBPFX, DBPFX, DBPFX,
			 dbmail_mailbox_get_id(self), 
			 MESSAGE_STATUS_NEW, MESSAGE_STATUS_SEEN, 
			 db_get_sql(SQL_INSENSITIVE_LIKE), s->hdrfld, 
			 partial, db_get_sql(SQL_INSENSITIVE_LIKE), s->search);
		break;

		case IST_DATA_TEXT:
		memset(partial, 0, sizeof(partial));
		snprintf(partial, DEF_FRAGSIZE, db_get_sql(SQL_PARTIAL), "v.headervalue");
		g_string_printf(q, "SELECT m.message_idnr, v.headervalue, k.messageblk " 
				"FROM %smessageblks k "
				"JOIN %sphysmessage p ON k.physmessage_id=p.id "
				"JOIN %smessages m ON m.physmessage_id=p.id "
				"JOIN %sheadervalue v ON v.physmessage_id=p.id "
				"WHERE m.mailbox_idnr=%llu AND m.status in (%d,%d) "
				"HAVING %s %s '%%%s%%' "
				"OR k.messageblk %s '%%%s%%'",
				DBPFX,DBPFX,DBPFX,DBPFX,
				dbmail_mailbox_get_id(self),
				MESSAGE_STATUS_NEW, MESSAGE_STATUS_SEEN,
				partial, db_get_sql(SQL_INSENSITIVE_LIKE), s->search,
				db_get_sql(SQL_INSENSITIVE_LIKE), s->search
			       );
		break;
			
		case IST_IDATE:
		g_string_printf(q, "SELECT message_idnr FROM %smessages m "
			 "JOIN %sphysmessage p ON m.physmessage_id=p.id "
			 "WHERE mailbox_idnr = %llu "
			 "AND status IN (%d,%d) AND p.%s "
			 "ORDER BY message_idnr", 
			 DBPFX, DBPFX, 
			 dbmail_mailbox_get_id(self),
			 MESSAGE_STATUS_NEW, MESSAGE_STATUS_SEEN, 
			 s->search);
		break;
		
		case IST_DATA_BODY:
		g_string_printf(t,db_get_sql(SQL_ENCODE_ESCAPE), "k.messageblk");
		g_string_printf(q, "SELECT m.message_idnr,k.messageblk FROM %smessageblks k "
			"JOIN %sphysmessage p ON k.physmessage_id = p.id "
			"JOIN %smessages m ON p.id = m.physmessage_id "
			"WHERE mailbox_idnr = %llu "
			"AND status IN (%d,%d ) "
			"AND k.is_header = '0' "
			"GROUP BY m.message_idnr,k.messageblk "
			"HAVING %s %s '%%%s%%'",
			DBPFX, DBPFX, DBPFX,
			dbmail_mailbox_get_id(self),
			MESSAGE_STATUS_NEW, MESSAGE_STATUS_SEEN,
			t->str, db_get_sql(SQL_SENSITIVE_LIKE), s->search);
		break;

		case IST_SIZE_LARGER:
			gt_lt = '>';
		case IST_SIZE_SMALLER:
			if (!gt_lt) gt_lt = '<';

		g_string_printf(q, "SELECT m.message_idnr FROM %smessages m "
			"JOIN %sphysmessage p ON m.physmessage_id = p.id "
			"WHERE m.mailbox_idnr = %llu "
			"AND m.status IN (%d,%d) AND p.messagesize %c %llu "
			"ORDER BY message_idnr", DBPFX, DBPFX,
			dbmail_mailbox_get_id(self), 
			MESSAGE_STATUS_NEW, MESSAGE_STATUS_SEEN, 
			gt_lt, s->size);
		break;

		default:
		g_string_printf(q, "SELECT message_idnr FROM %smessages "
			"WHERE mailbox_idnr = %llu "
			"AND status IN (%d,%d) AND %s "
			"ORDER BY message_idnr", DBPFX, 
			dbmail_mailbox_get_id(self), 
			MESSAGE_STATUS_NEW, MESSAGE_STATUS_SEEN, 
			s->search); // FIXME: Sometimes s->search is ""
		break;
		
	}
	
	g_string_free(t,TRUE);
	if (db_query(q->str) == -1) {
		TRACE(TRACE_ERROR, "could not execute query");
		g_string_free(q,TRUE);
		return NULL;
	}
	g_string_free(q,TRUE);

	rows = db_num_rows();

	s->found = g_tree_new_full((GCompareDataFunc)ucmp,NULL,(GDestroyNotify)g_free, (GDestroyNotify)g_free);

	if (rows > 0) {
		
		
		for (i=0; i < rows; i++) {
			id = db_get_result_u64(i,0);
			if (! (w = g_tree_lookup(self->ids, &id))) {
				TRACE(TRACE_ERROR, "key missing in self->ids: [%llu]\n", id);
				continue;
			}
			assert(w);
			
			k = g_new0(u64_t,1);
			v = g_new0(u64_t,1);
			*k = id;
			*v = *w;

			g_tree_insert(s->found, k, v);
		}
	}
	db_free_result();

	return s->found;
}

struct filter_helper {
	gboolean uid;
	u64_t min;
	u64_t max;
	GTree *a;
};

static int filter_range(gpointer key, gpointer value, gpointer data)
{
	u64_t *k, *v;
	struct filter_helper *d = (struct filter_helper *)data;

	if (*(u64_t *)key < d->min) return FALSE; // skip
	if (*(u64_t *)key > d->max) return TRUE; // done

	k = g_new0(u64_t,1);
	v = g_new0(u64_t,2);

	*k = *(u64_t *)key;
	*v = *(u64_t *)value;
        	
	if (d->uid)
		g_tree_insert(d->a, k, v);
	else
		g_tree_insert(d->a, v, k);

	return FALSE;
}

static void find_range(GTree *c, u64_t l, u64_t r, GTree *a, gboolean uid)
{
	struct filter_helper data;

	data.uid = uid;
	data.min = l;
	data.max = r;
	data.a = a;

	g_tree_foreach(c, (GTraverseFunc)filter_range, &data);
}

GTree * dbmail_mailbox_get_set(struct DbmailMailbox *self, const char *set, gboolean uid)
{
	GList *ids = NULL, *sets = NULL;
	GString *t;
	char *rest;
	u64_t l, r, lo = 0, hi = 0;
	GTree *a, *b, *c;
	gboolean error = FALSE;
	
	b = NULL;

	if (! self->ids)
		dbmail_mailbox_open(self);
	
	if (! (self->ids && set))
		return b;

	b = g_tree_new_full((GCompareDataFunc)ucmp,NULL, (GDestroyNotify)g_free, (GDestroyNotify)g_free);

	if (g_tree_nnodes(self->ids) == 0)
		return b;

	TRACE(TRACE_DEBUG,"[%s] uid [%d]", set, uid);
	
	if (uid) {
		ids = g_tree_keys(self->ids);
		assert(ids);
		ids = g_list_last(ids);
		hi = *((u64_t *)ids->data);
		ids = g_list_first(ids);
		lo = *((u64_t *)ids->data);
		g_list_free(g_list_first(ids));
	} else {
		lo = 1;
		hi = g_tree_nnodes(self->ids);
	}
	
	a = g_tree_new_full((GCompareDataFunc)ucmp,NULL, (GDestroyNotify)g_free, (GDestroyNotify)g_free);

	t = g_string_new(set);
	
	sets = g_string_split(t,",");
	sets = g_list_first(sets);
	
	while(sets) {
		
		l = 0;
		r = 0;
		
		if (strlen((char *)sets->data) < 1)
			break;
		
		rest = sets->data;
		
		if (rest[0] == '*') {
			l = hi;
			r = l;
			if (strlen(rest) > 1)
				rest++;
		} else {
			if (! (l = dm_strtoull(sets->data,&rest,10))) {
				error = TRUE;
				break;
			}

			if (l == 0xffffffff) // outlook
				l = hi;

			l = max(l,lo);
			r = l;
		}
		
		if (rest[0]==':') {
			if (strlen(rest)>1)
				rest++;

			if (rest[0] == '*') 
				r = hi;
			else {
				
				if (! (r = dm_strtoull(rest,NULL,10))) {
					error = TRUE;
					break;
				}

				if (r == 0xffffffff) // outlook
					r = hi;
			}
			
			if (!r)
				break;
			
			if (r > hi)
				r = hi;
			
			if (r < lo)
				r = lo;
		}
	
		if (! (l && r))
			break;

		if (uid)
			c = self->ids;
		else
			c = self->msn;

		find_range(c, min(l,r), max(l,r), a, uid);

		if (g_tree_merge(b,a,IST_SUBSEARCH_OR)) {
			error = TRUE;
			TRACE(TRACE_ERROR, "cannot compare null trees");
			break;
		}
			
		
		if (! g_list_next(sets))
			break;

		sets = g_list_next(sets);
	}

	g_list_destroy(sets);
	g_string_free(t,TRUE);

	if (a)
		g_tree_destroy(a);

	if (error) {
		g_tree_destroy(b);
		b = NULL;
	}

	return b;
}

static gboolean _found_tree_copy(u64_t *key, u64_t *val, GTree *tree)
{
	u64_t *a,*b;
	a = g_new0(u64_t,1);
	b = g_new0(u64_t,1);
	*a = *key;
	*b = *val;
	g_tree_insert(tree, a, b);
	return FALSE;
}
static gboolean _do_search(GNode *node, struct DbmailMailbox *self)
{
	search_key_t *s = (search_key_t *)node->data;

	if (s->searched)
		return FALSE;
	
	switch (s->type) {
		case IST_SORT:
			return FALSE;
			break;
			
		case IST_SET:
			if (! (s->found = dbmail_mailbox_get_set(self, (const char *)s->search, 0)))
				return TRUE;
			break;
		case IST_UIDSET:
			if (! (s->found = dbmail_mailbox_get_set(self, (const char *)s->search, 1)))
				return TRUE;
			break;

		case IST_SIZE_LARGER:
		case IST_SIZE_SMALLER:
		case IST_HDRDATE_BEFORE:
		case IST_HDRDATE_SINCE:
		case IST_HDRDATE_ON:
		case IST_IDATE:
		case IST_FLAG:
		case IST_HDR:
		case IST_DATA_TEXT:
		case IST_DATA_BODY:
			mailbox_search(self, s);
			break;
			
		case IST_SUBSEARCH_NOT:
		case IST_SUBSEARCH_AND:
		case IST_SUBSEARCH_OR:
			g_node_children_foreach(node, G_TRAVERSE_ALL, (GNodeForeachFunc)_do_search, (gpointer)self);
			s->found = g_tree_new_full((GCompareDataFunc)ucmp,NULL,(GDestroyNotify)g_free, (GDestroyNotify)g_free);
			break;


		default:
			return TRUE;
	}

	s->searched = TRUE;
	
	TRACE(TRACE_DEBUG,"[%p] depth [%d] type [%d] rows [%d]\n",
		s, g_node_depth(node), s->type, s->found ? g_tree_nnodes(s->found): 0);

	return FALSE;
}	


static gboolean _merge_search(GNode *node, GTree *found)
{
	search_key_t *s = (search_key_t *)node->data;
	search_key_t *a, *b;
	GNode *x, *y;

	if (s->type == IST_SORT)
		return FALSE;

	if (s->merged == TRUE)
		return FALSE;


	switch(s->type) {
		case IST_SUBSEARCH_AND:
			g_node_children_foreach(node, G_TRAVERSE_ALL, (GNodeForeachFunc)_merge_search, (gpointer) found);
			break;
			
		case IST_SUBSEARCH_NOT:
			g_tree_foreach(found, (GTraverseFunc)_found_tree_copy, s->found);
			g_node_children_foreach(node, G_TRAVERSE_ALL, (GNodeForeachFunc)_merge_search, (gpointer) s->found);
			g_tree_merge(found, s->found, IST_SUBSEARCH_NOT);
			s->merged = TRUE;
			g_tree_destroy(s->found);
			s->found = NULL;

			break;
			
		case IST_SUBSEARCH_OR:
			x = g_node_nth_child(node,0);
			y = g_node_nth_child(node,1);
			a = (search_key_t *)x->data;
			b = (search_key_t *)y->data;

			if (a->type == IST_SUBSEARCH_AND) {
				g_tree_foreach(found, (GTraverseFunc)_found_tree_copy, a->found);
				g_node_children_foreach(x, G_TRAVERSE_ALL, (GNodeForeachFunc)_merge_search, (gpointer)a->found);
			}

			if (b->type == IST_SUBSEARCH_AND) {
				g_tree_foreach(found, (GTraverseFunc)_found_tree_copy, b->found);
				g_node_children_foreach(y, G_TRAVERSE_ALL, (GNodeForeachFunc)_merge_search, (gpointer)b->found);
			}
		
			g_tree_merge(a->found, b->found,IST_SUBSEARCH_OR);
			b->merged = TRUE;
			g_tree_destroy(b->found);
			b->found = NULL;

			g_tree_merge(s->found, a->found,IST_SUBSEARCH_OR);
			a->merged = TRUE;
			g_tree_destroy(a->found);
			a->found = NULL;

			g_tree_merge(found, s->found, IST_SUBSEARCH_AND);
			s->merged = TRUE;
			g_tree_destroy(s->found);
			s->found = NULL;

			break;
			
		default:
			g_tree_merge(found, s->found, IST_SUBSEARCH_AND);
			s->merged = TRUE;
			g_tree_destroy(s->found);
			s->found = NULL;

			break;
	}

	TRACE(TRACE_DEBUG,"[%p] leaf [%d] depth [%d] type [%d] found [%d]", 
			s, G_NODE_IS_LEAF(node), g_node_depth(node), s->type, found ? g_tree_nnodes(found): 0);

	return FALSE;
}
	
int dbmail_mailbox_sort(struct DbmailMailbox *self) 
{
	if (! self->search)
		return 0;
	
	g_node_traverse(g_node_get_root(self->search), G_PRE_ORDER, G_TRAVERSE_ALL, -1, 
			(GNodeTraverseFunc)_do_sort, (gpointer)self);
	
	return 0;
}


int dbmail_mailbox_search(struct DbmailMailbox *self) 
{
	if (! self->search)
		return 0;
	
	g_node_traverse(g_node_get_root(self->search), G_PRE_ORDER, G_TRAVERSE_ALL, -1, 
			(GNodeTraverseFunc)_do_search, (gpointer)self);

	g_node_traverse(g_node_get_root(self->search), G_PRE_ORDER, G_TRAVERSE_ALL, -1, 
			(GNodeTraverseFunc)_merge_search, (gpointer)self->ids);


	if (self->ids == NULL)
		TRACE(TRACE_DEBUG,"found no ids\n");
	else
		TRACE(TRACE_DEBUG,"found [%d] ids\n", self->ids ? g_tree_nnodes(self->ids): 0);
	
	return 0;
}

