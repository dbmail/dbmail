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
 */

/**
 *
 * implements DbmailMailbox object
 */

#include "dbmail.h"
#define THIS_MODULE "mailbox"

extern DBParam_T db_params;
extern Mempool_T small_pool;

#define DBPFX db_params.pfx

/* internal utilities */

/* class methods */

DbmailMailbox * dbmail_mailbox_new(Mempool_T pool, uint64_t id) {
	gboolean freepool = FALSE;
	if (!pool) {
		pool = mempool_open();
		freepool = TRUE;
	}

	DbmailMailbox *self = mempool_pop(pool, sizeof (DbmailMailbox));
	self->pool = pool;
	self->freepool = freepool;

	assert(id);
	assert(self);

	self->id = id;
	dbmail_mailbox_set_uid(self, FALSE);

	return self;
}

static gboolean _node_free(GNode *node, gpointer data) {
	DbmailMailbox *self = (DbmailMailbox *) data;
	search_key *s = (search_key *) node->data;
	if (s->found)
		g_tree_destroy(s->found);
	mempool_push(self->pool, s, sizeof (search_key));
	return FALSE;
}

void dbmail_mailbox_free(DbmailMailbox *self) {
	Mempool_T pool = self->pool;
	gboolean freepool = self->freepool;
	if (self->found) g_tree_destroy(self->found);
	if (self->sorted) g_list_destroy(self->sorted);
	if (self->search) {
		g_node_traverse(g_node_get_root(self->search), G_POST_ORDER, G_TRAVERSE_ALL, -1, (GNodeTraverseFunc) _node_free, self);
		g_node_destroy(self->search);
	}

	mempool_push(pool, self, sizeof (DbmailMailbox));
	if (freepool)
		mempool_close(&pool);
}

uint64_t dbmail_mailbox_get_id(DbmailMailbox *self) {
	assert(self->id > 0);
	return self->id;
}

void dbmail_mailbox_set_uid(DbmailMailbox *self, gboolean uid) {
	self->uid = uid;
}

gboolean dbmail_mailbox_get_uid(DbmailMailbox *self) {
	return self->uid;
}

int dbmail_mailbox_open(DbmailMailbox *self) {
	if ((self->mbstate = MailboxState_new(self->pool, self->id)) == NULL)
		return DM_EQUERY;
	return DM_SUCCESS;
}

#define FROM_STANDARD_DATE "Tue Oct 11 13:06:24 2005"

static String_T _message_get_envelope_date(DbmailMailbox *mailbox, const DbmailMessage *message) {
	struct tm gmt;
	String_T date;

	assert(message->internal_date);

	memset(&gmt, 0, sizeof (struct tm));
	if (gmtime_r(&message->internal_date, &gmt)) {
		char res[TIMESTRING_SIZE + 1];
		memset(res, 0, sizeof (res));
		strftime(res, TIMESTRING_SIZE, "%a %b %d %H:%M:%S %Y", &gmt);
		date = p_string_new(mailbox->pool, res);
	} else {
		date = p_string_new(mailbox->pool, FROM_STANDARD_DATE);
	}

	return date;
}

static size_t dump_message_to_stream(DbmailMailbox *self, DbmailMessage *message, GMimeStream *ostream) {
	size_t r = 0;
	gchar *s;
	String_T sender;
	String_T t;
	String_T date;
	InternetAddressList *ialist;
	InternetAddress *ia;


	g_return_val_if_fail(GMIME_IS_MESSAGE(message->content), 0);

	s = dbmail_message_to_string(message);

	if (!strncmp(s, "From ", 5)) {
		ialist = g_mime_message_get_sender(GMIME_MESSAGE(message->content));
		if (internet_address_list_length(ialist) > 0) {
			ia = internet_address_list_get_address (ialist, 0);
			sender = p_string_new(self->pool, internet_address_to_string (ia, NULL, TRUE));
			g_free(ia);
			TRACE(TRACE_DEBUG, "Sender is: [%s]", (char *)sender);
		} else {
			TRACE(TRACE_DEBUG, "Setting sender to nobody@foo");
			sender = p_string_new(self->pool, "nobody@foo");
		}
		g_object_unref(ialist);

		date = _message_get_envelope_date(self, message);
		t = p_string_new(self->pool, "From ");
		p_string_append_printf(t, "%s %s\n",
			p_string_str(sender), p_string_str(date));

		r = g_mime_stream_write_string(ostream, p_string_str(t));

		p_string_free(t, TRUE);
		p_string_free(sender, TRUE);
		p_string_free(date, TRUE);

	}

	r += g_mime_stream_write_string(ostream, s);
	r += g_mime_stream_write_string(ostream, "\n");

	g_free(s);
	return r;
}

static int _mimeparts_dump(DbmailMailbox *self, GMimeStream *ostream) {
	List_T ids = NULL;
	uint64_t msgid, physid, *id;
	DbmailMessage *m;
	GTree *uids;
	volatile int count = 0;
	PreparedStatement_T stmt;
	Connection_T c;
	ResultSet_T r;
	volatile int t = FALSE;

	uids = self->found;

	c = db_con_get();
	TRY
	stmt = db_stmt_prepare(c,
		"SELECT id,message_idnr FROM %sphysmessage p "
		"LEFT JOIN %smessages m ON p.id=m.physmessage_id "
		"LEFT JOIN %smailboxes b ON b.mailbox_idnr=m.mailbox_idnr "
		"WHERE b.mailbox_idnr=? ORDER BY message_idnr",
		DBPFX, DBPFX, DBPFX);
	db_stmt_set_u64(stmt, 1, self->id);
	r = db_stmt_query(stmt);

	ids = p_list_new(self->pool);
	while (db_result_next(r)) {
		physid = db_result_get_u64(r, 0);
		msgid = db_result_get_u64(r, 1);
		if (g_tree_lookup(uids, &msgid)) {
			id = mempool_pop(self->pool, sizeof (uint64_t));
			*id = physid;
			ids = p_list_append(ids, id);
		}
	}
	CATCH(SQLException)
	LOG_SQLERROR;
	t = DM_EQUERY;
	FINALLY
	db_con_close(c);
	END_TRY;

	if (t == DM_EQUERY) return t;

	ids = p_list_first(ids);

	while (ids) {
		physid = *(uint64_t *) p_list_data(ids);
		mempool_push(self->pool, p_list_data(ids), sizeof (uint64_t));
		m = dbmail_message_new(self->pool);
		m = dbmail_message_retrieve(m, physid);
		if (dump_message_to_stream(self, m, ostream) > 0)
			count++;
		dbmail_message_free(m);

		if (!p_list_next(ids)) break;
		ids = p_list_next(ids);
	}

	ids = p_list_first(ids);
	p_list_free(&ids);

	return count;
}

/* Caller must fclose the file pointer itself. */
int dbmail_mailbox_dump(DbmailMailbox *self, FILE *file) {
	int count = 0;
	GMimeStream *ostream;

	dbmail_mailbox_open(self);

	GTree *ids = self->found;

	if (ids == NULL || g_tree_nnodes(ids) == 0) {
		TRACE(TRACE_DEBUG, "cannot dump empty mailbox");
		return 0;
	}

	assert(ids);

	ostream = g_mime_stream_file_new(file);
	g_mime_stream_file_set_owner((GMimeStreamFile *) ostream, FALSE);

	count += _mimeparts_dump(self, ostream);

	g_object_unref(ostream);

	return count;
}

static gboolean _tree_foreach(gpointer key UNUSED, gpointer value, GString * data) {
	gboolean res = FALSE;
	uint64_t *id;
	GList *sublist = g_list_first((GList *) value);
	GString *t = g_string_new("");
	int m = g_list_length(sublist);

	sublist = g_list_first(sublist);
	while (sublist) {
		id = sublist->data;
		g_string_append_printf(t, "(%" PRIu64 ")", *id);

		if (!g_list_next(sublist))
			break;
		sublist = g_list_next(sublist);
	}
	if (m > 1)
		g_string_append_printf(data, "(%s)", t->str);
	else
		g_string_append_printf(data, "%s", t->str);

	g_string_free(t, TRUE);

	return res;
}

char * dbmail_mailbox_orderedsubject(DbmailMailbox *self) {
	GList *sublist = NULL;
	volatile uint64_t i = 0, idnr = 0;
	char *subj;
	char *res = NULL;
	uint64_t *id, *msn;
	GTree *tree;
	GString *threads;
	PreparedStatement_T stmt;
	Connection_T c;
	ResultSet_T r;
	volatile int t = FALSE;

	tree = g_tree_new_full((GCompareDataFunc) dm_strcmpdata, NULL, (GDestroyNotify) g_free, NULL);

	t = FALSE;
	c = db_con_get();
	TRY
	/* thread-roots (ordered) */
	stmt = db_stmt_prepare(c,
		"SELECT min(m.message_idnr),v.sortfield "
		"FROM %smessages m "
		"LEFT JOIN %sheader h USING (physmessage_id) "
		"LEFT JOIN %sheadername n ON h.headername_id = n.id "
		"LEFT JOIN %sheadervalue v ON h.headervalue_id = v.id "
		"WHERE m.mailbox_idnr=? "
		"AND n.headername = 'subject' AND m.status < %d "
		"GROUP BY v.sortfield",
		DBPFX, DBPFX, DBPFX, DBPFX,
		MESSAGE_STATUS_DELETE);
	db_stmt_set_u64(stmt, 1, self->id);
	r = db_stmt_query(stmt);

	i = 0;
	while (db_result_next(r)) {
		i++;
		idnr = db_result_get_u64(r, 0);
		if (!g_tree_lookup(self->found, (gconstpointer) & idnr))
			continue;
		subj = (char *) db_result_get(r, 1);
		g_tree_insert(tree, g_strdup(subj), NULL);
	}
	CATCH(SQLException)
	LOG_SQLERROR;
	t = DM_EQUERY;
	END_TRY;

	if ((t == DM_EQUERY) || (!i)) {
		g_tree_destroy(tree);
		db_con_close(c);
		return res;
	}

	db_con_clear(c);

	TRY
	/* full threads (unordered) */
	stmt = db_stmt_prepare(c,
		"SELECT m.message_idnr,v.sortfield "
		"FROM %smessages m "
		"LEFT JOIN %sheader h USING (physmessage_id) "
		"LEFT JOIN %sheadername n ON h.headername_id = n.id "
		"LEFT JOIN %sheadervalue v ON h.headervalue_id = v.id "
		"WHERE m.mailbox_idnr = ? "
		"AND n.headername = 'subject' AND m.status < %d "
		"ORDER BY v.sortfield, v.datefield",
		DBPFX, DBPFX, DBPFX, DBPFX,
		MESSAGE_STATUS_DELETE);
	db_stmt_set_u64(stmt, 1, self->id);
	r = db_stmt_query(stmt);

	i = 0;
	while (db_result_next(r)) {
		i++;
		idnr = db_result_get_u64(r, 0);
		if (!(msn = g_tree_lookup(self->found, (gconstpointer) & idnr)))
			continue;
		subj = (char *) db_result_get(r, 1);

		id = g_new0(uint64_t, 1);
		if (dbmail_mailbox_get_uid(self))
			*id = idnr;
		else
			*id = *msn;

		sublist = g_tree_lookup(tree, (gconstpointer) subj);
		sublist = g_list_append(sublist, id);
		g_tree_insert(tree, g_strdup(subj), sublist);
	}
	CATCH(SQLException)
	LOG_SQLERROR;
	t = DM_EQUERY;
	FINALLY
	db_con_close(c);
	END_TRY;

	if ((t == DM_EQUERY) || (!i)) {
		g_tree_destroy(tree);
		return res;
	}

	threads = g_string_new("");
	g_tree_foreach(tree, (GTraverseFunc) _tree_foreach, threads);
	res = threads->str;

	g_string_free(threads, FALSE);
	g_tree_destroy(tree);

	return res;
}

/*
 * Returns imap modseq response for a user's mailbox
 * 
 * This function came from and is used with dbmail_mailbox_ids_as_string
 */
char * dbmail_mailbox_imap_modseq_as_string(DbmailMailbox *self, gboolean uid) {
	TRACE(TRACE_DEBUG, "Call: dbmail_mailbox_imap_modseq_as_string");
	GString *t;
	gchar *s = NULL;
	GList *l = NULL;
	GTree *msginfo;
	GTree *msn;
	uint64_t maxseq = 0;

	if ((self->found == NULL) || g_tree_nnodes(self->found) <= 0) {
		TRACE(TRACE_DEBUG, "no ids found");
		return s;
	}

	t = g_string_new("");
	if (uid || dbmail_mailbox_get_uid(self)) {
		l = g_tree_keys(self->found);
	} else {
		l = g_tree_values(self->found);
	}

	msginfo = MailboxState_getMsginfo(self->mbstate);
	msn = MailboxState_getMsn(self->mbstate);

	while (l->data) {
		uint64_t *key = (uint64_t *) l->data;
		if (self->modseq) {
			uint64_t *id;
			if (uid || dbmail_mailbox_get_uid(self)) {
				id = key;
			} else {
				id = g_tree_lookup(msn, key);
			}

			MessageInfo *info = g_tree_lookup(msginfo, id);
			maxseq = max(maxseq, info->seq);
		}
		if (!g_list_next(l))
			break;
		l = g_list_next(l);
	}

	g_list_free(l);

	if (self->modseq)
		g_string_append_printf(t, " (MODSEQ %" PRIu64 ")", maxseq);

	s = t->str;
	g_string_free(t, FALSE);

	return g_strchomp(s);

}

/*
 * Returns users mailbox self->ids as a string concatenated with a seperator
 * 
 * Use with dbmail_mailbox_imap_modseq_as_string to generate imap response
 */
char * dbmail_mailbox_ids_as_string(
		DbmailMailbox *self,
		gboolean uid,
		const char *sep
	) {
	TRACE(TRACE_DEBUG, "Call: dbmail_mailbox_ids_as_string");
	GString *t;
	gchar *s = NULL;
	GList *l = NULL, *h = NULL;
	GTree *msginfo;
	GTree *msn;
	uint64_t maxseq = 0;

	if ((self->found == NULL) || g_tree_nnodes(self->found) <= 0) {
		TRACE(TRACE_DEBUG, "no ids found");
		return s;
	}

	t = g_string_new("");
	if (uid || dbmail_mailbox_get_uid(self)) {
		l = g_tree_keys(self->found);
	} else {
		l = g_tree_values(self->found);
	}

	h = l;

	msginfo = MailboxState_getMsginfo(self->mbstate);
	msn = MailboxState_getMsn(self->mbstate);

	while (l->data) {
		uint64_t *key = (uint64_t *) l->data;
		if (self->modseq) {
			uint64_t *id;
			if (uid || dbmail_mailbox_get_uid(self)) {
				id = key;
			} else {
				id = g_tree_lookup(msn, key);
			}

			MessageInfo *info = g_tree_lookup(msginfo, id);
			maxseq = max(maxseq, info->seq);
		}
		g_string_append_printf(t, "%" PRIu64 "", *key);
		if (!g_list_next(l))
			break;
		g_string_append_printf(t, "%s", sep);
		l = g_list_next(l);
	}

	g_list_free(h);

	s = t->str;
	g_string_free(t, FALSE);

	return g_strchomp(s);

}

char * dbmail_mailbox_sorted_as_string(DbmailMailbox *self) {
	GString *t;
	gchar *s = NULL;
	GList *l = NULL;
	gboolean uid;
	uint64_t *msn;

	l = g_list_first(self->sorted);
	if (!(g_list_length(l) > 0))
		return s;

	t = g_string_new("");
	uid = dbmail_mailbox_get_uid(self);

	while (l->data) {
		msn = g_tree_lookup(self->found, l->data);
		if (msn) {
			if (uid)
				g_string_append_printf(t, "%" PRIu64 " ", *(uint64_t *) l->data);
			else
				g_string_append_printf(t, "%" PRIu64 " ", *(uint64_t *) msn);
		}
		if (!g_list_next(l))
			break;
		l = g_list_next(l);
	}

	s = t->str;
	g_string_free(t, FALSE);

	return g_strchomp(s);
}

/* imap sorted search */
static int append_search(DbmailMailbox *self, search_key *value, gboolean descend) {
	GNode *n;

	if (self->search) {
		n = g_node_append_data(self->search, value);
	} else {
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

#define BUFSIZE 255

static void _append_join(char *join, char *table) {
	char tmp[BUFSIZE + 1];
	memset(tmp, 0, sizeof (tmp));
	g_snprintf(tmp, BUFSIZE, "LEFT JOIN %s%s ON m.physmessage_id=%s%s.physmessage_id ", DBPFX, table, DBPFX, table);
	g_strlcat(join, tmp, MAX_SEARCH_LEN);
}

static void _append_sort(char *order, char *field, gboolean reverse) {
	char tmp[BUFSIZE + 1];
	memset(tmp, 0, sizeof (tmp));
	g_snprintf(tmp, BUFSIZE, "%s%s,", field, reverse ? " DESC" : "");
	g_strlcat(order, tmp, MAX_SEARCH_LEN);
}

static int _handle_sort_args(DbmailMailbox *self, String_T *search_keys, search_key *value, uint64_t *idx) {
	value->type = IST_SORT;

	gboolean reverse = FALSE;

	if (!(search_keys && search_keys[*idx]))
		return -1;

	const char *key = p_string_str(search_keys[*idx]);

	if (MATCH(key, "reverse")) {
		reverse = TRUE;
		(*idx)++;
		key = p_string_str(search_keys[*idx]);
	}

	if (MATCH(key, "arrival")) {
		_append_sort(value->order, "internal_date", reverse);
		(*idx)++;
	} else if (MATCH(key, "size")) {
		_append_sort(value->order, "messagesize", reverse);
		(*idx)++;
	} else if (MATCH(key, "from")) {
		_append_join(value->table, "fromfield");
		_append_sort(value->order, "fromfield", reverse);
		(*idx)++;
	} else if (MATCH(key, "subject")) {
		_append_join(value->table, "subjectfield");
		_append_sort(value->order, "sortfield", reverse);
		(*idx)++;
	} else if (MATCH(key, "cc")) {
		_append_join(value->table, "ccfield");
		_append_sort(value->order, "ccfield", reverse);
		(*idx)++;
	} else if (MATCH(key, "to")) {
		_append_join(value->table, "tofield");
		_append_sort(value->order, "tofield", reverse);
		(*idx)++;
	} else if (MATCH(key, "date")) {
		_append_join(value->table, "datefield");
		_append_sort(value->order, "sortfield", reverse);
		(*idx)++;
	} else if (MATCH(key, "("))
		(*idx)++;

	else if (MATCH(key, ")"))
		(*idx)++;

	else if (MATCH(key, "utf-8")) {
		(*idx)++;
		append_search(self, value, 0);
		return 1;
	} else if (MATCH(key, "us-ascii")) {
		(*idx)++;
		append_search(self, value, 0);
		return 1;
	} else if (MATCH(key, "iso-8859-1")) {
		(*idx)++;
		append_search(self, value, 0);
		return 1;
	} else
		return -1; /* done */

	return 0;
}

static void pop_search(DbmailMailbox *self) {
	// switch back to parent 
	if (self->search && self->search->parent)
		self->search = self->search->parent;
}

static int _handle_search_args(DbmailMailbox *self, String_T *search_keys, uint64_t *idx) {
	int result = 0;

	if (!(search_keys && search_keys[*idx]))
		return 1;

	char *p = NULL, *t = NULL;
	const char *key = p_string_str(search_keys[*idx]);

	search_key *value = mempool_pop(self->pool, sizeof (search_key));

	/* SEARCH */

	TRACE(TRACE_DEBUG, "key [%s]", key);

	if (MATCH(key, "all")) {
		value->type = IST_UIDSET;
		strcpy(value->search, "1:*");
		(*idx)++;

	}
#define RETURN_IF_FAIL(x, y) \
	if (! (x)) { \
		mempool_push(self->pool, value, sizeof(search_key)); \
		return y; \
	}

	else if (MATCH(key, "uid")) {
		RETURN_IF_FAIL(search_keys[*idx + 1], -1);
		RETURN_IF_FAIL(check_msg_set(p_string_str(search_keys[*idx + 1])), -1);
		value->type = IST_UIDSET;
		(*idx)++;
		strncpy(value->search, p_string_str(search_keys[(*idx)]), MAX_SEARCH_LEN - 1);
		(*idx)++;
	}/*
	 * FLAG search keys
	 */

	else if (MATCH(key, "answered")) {
		value->type = IST_FLAG;
		strncpy(value->search, "answered_flag=1", MAX_SEARCH_LEN - 1);
		(*idx)++;

	} else if (MATCH(key, "deleted")) {
		value->type = IST_FLAG;
		strncpy(value->search, "deleted_flag=1", MAX_SEARCH_LEN - 1);
		(*idx)++;

	} else if (MATCH(key, "flagged")) {
		value->type = IST_FLAG;
		strncpy(value->search, "flagged_flag=1", MAX_SEARCH_LEN - 1);
		(*idx)++;

	} else if (MATCH(key, "recent")) {
		value->type = IST_FLAG;
		strncpy(value->search, "recent_flag=1", MAX_SEARCH_LEN - 1);
		(*idx)++;

	} else if (MATCH(key, "seen")) {
		value->type = IST_FLAG;
		strncpy(value->search, "seen_flag=1", MAX_SEARCH_LEN - 1);
		(*idx)++;

	} else if (MATCH(key, "draft")) {
		value->type = IST_FLAG;
		strncpy(value->search, "draft_flag=1", MAX_SEARCH_LEN - 1);
		(*idx)++;

	} else if (MATCH(key, "new")) {
		value->type = IST_FLAG;
		strncpy(value->search, "(seen_flag=0 AND recent_flag=1)", MAX_SEARCH_LEN - 1);
		(*idx)++;

	} else if (MATCH(key, "old")) {
		value->type = IST_FLAG;
		strncpy(value->search, "recent_flag=0", MAX_SEARCH_LEN - 1);
		(*idx)++;

	} else if (MATCH(key, "unanswered")) {
		value->type = IST_FLAG;
		strncpy(value->search, "answered_flag=0", MAX_SEARCH_LEN - 1);
		(*idx)++;

	} else if (MATCH(key, "undeleted")) {
		value->type = IST_FLAG;
		strncpy(value->search, "deleted_flag=0", MAX_SEARCH_LEN - 1);
		(*idx)++;

	} else if (MATCH(key, "unflagged")) {
		value->type = IST_FLAG;
		strncpy(value->search, "flagged_flag=0", MAX_SEARCH_LEN - 1);
		(*idx)++;

	} else if (MATCH(key, "unseen")) {
		value->type = IST_FLAG;
		strncpy(value->search, "seen_flag=0", MAX_SEARCH_LEN - 1);
		(*idx)++;

	} else if (MATCH(key, "undraft")) {
		value->type = IST_FLAG;
		strncpy(value->search, "draft_flag=0", MAX_SEARCH_LEN - 1);
		(*idx)++;

	}
#define IMAP_SET_SEARCH  (*idx)++; \
		if ((p = dbmail_iconv_str_to_db(p_string_str(search_keys[*idx]), self->charset)) == NULL) {  \
			TRACE(TRACE_WARNING, "search_key [%s] is not charset [%s]", p_string_str(search_keys[*idx]), self->charset); \
		} else { \
			strncpy(value->search, p, MAX_SEARCH_LEN-1); \
			g_free(p); \
		} \
		g_free(t); \
		(*idx)++

		/* 
		 * keyword search
		 */

	else if (MATCH(key, "keyword")) {
		RETURN_IF_FAIL(search_keys[*idx + 1], -1);
		value->type = IST_KEYWORD;
		IMAP_SET_SEARCH;
	} else if (MATCH(key, "unkeyword")) {
		RETURN_IF_FAIL(search_keys[*idx + 1], -1);
		value->type = IST_UNKEYWORD;
		IMAP_SET_SEARCH;
	}/*
	 * HEADER search keys
	 */
	else if (MATCH(key, "bcc")) {
		RETURN_IF_FAIL(search_keys[*idx + 1], -1);
		value->type = IST_HDR;
		strncpy(value->hdrfld, "bcc", MIME_FIELD_MAX - 1);
		IMAP_SET_SEARCH;

	} else if (MATCH(key, "cc")) {
		RETURN_IF_FAIL(search_keys[*idx + 1], -1);
		value->type = IST_HDR;
		strncpy(value->hdrfld, "cc", MIME_FIELD_MAX - 1);
		IMAP_SET_SEARCH;

	} else if (MATCH(key, "from")) {
		RETURN_IF_FAIL(search_keys[*idx + 1], -1);
		value->type = IST_HDR;
		strncpy(value->hdrfld, "from", MIME_FIELD_MAX - 1);
		IMAP_SET_SEARCH;

	} else if (MATCH(key, "to")) {
		RETURN_IF_FAIL(search_keys[*idx + 1], -1);
		value->type = IST_HDR;
		strncpy(value->hdrfld, "to", MIME_FIELD_MAX - 1);
		IMAP_SET_SEARCH;

	} else if (MATCH(key, "subject")) {
		RETURN_IF_FAIL(search_keys[*idx + 1], -1);
		value->type = IST_HDR;
		strncpy(value->hdrfld, "subject", MIME_FIELD_MAX - 1);
		IMAP_SET_SEARCH;

	} else if (MATCH(key, "header")) {
		RETURN_IF_FAIL(search_keys[*idx + 1], -1);
		RETURN_IF_FAIL(search_keys[*idx + 2], -1);
		value->type = IST_HDR;

		const char *hdr = p_string_str(search_keys[*idx + 1]);
		char *hdrfld = g_ascii_strdown(hdr, strlen(hdr));
		strncpy(value->hdrfld, hdrfld, MIME_FIELD_MAX - 1);
		g_free(hdrfld);

		strncpy(value->search, p_string_str(search_keys[*idx + 2]), MAX_SEARCH_LEN - 1);
		(*idx) += 3;

	} else if (MATCH(key, "sentbefore")) {
		RETURN_IF_FAIL(search_keys[*idx + 1], -1);
		value->type = IST_HDRDATE_BEFORE;
		strncpy(value->hdrfld, "datefield", MIME_FIELD_MAX - 1);
		IMAP_SET_SEARCH;

	} else if (MATCH(key, "senton")) {
		RETURN_IF_FAIL(search_keys[*idx + 1], -1);
		value->type = IST_HDRDATE_ON;
		strncpy(value->hdrfld, "datefield", MIME_FIELD_MAX - 1);
		IMAP_SET_SEARCH;

	} else if (MATCH(key, "sentsince")) {
		RETURN_IF_FAIL(search_keys[*idx + 1], -1);
		value->type = IST_HDRDATE_SINCE;
		strncpy(value->hdrfld, "datefield", MIME_FIELD_MAX - 1);
		IMAP_SET_SEARCH;
	}/*
	 * INTERNALDATE keys
	 */

	else if (MATCH(key, "before")) {
		char s[SQL_INTERNALDATE_LEN];
		memset(s, 0, sizeof (s));
		RETURN_IF_FAIL(search_keys[*idx + 1], -1);
		RETURN_IF_FAIL(check_date(p_string_str(search_keys[*idx + 1])), -1);
		value->type = IST_IDATE;
		(*idx)++;
		date_imap2sql(p_string_str(search_keys[*idx]), s);
		g_snprintf(value->search, MAX_SEARCH_LEN - 1, "p.internal_date < '%s'", s);
		(*idx)++;

	} else if (MATCH(key, "on")) {
		char s[SQL_INTERNALDATE_LEN], d[MIME_FIELD_MAX];
		memset(s, 0, sizeof (s));
		memset(d, 0, sizeof (d));
		RETURN_IF_FAIL(search_keys[*idx + 1], -1);
		RETURN_IF_FAIL(check_date(p_string_str(search_keys[*idx + 1])), -1);
		value->type = IST_IDATE;
		(*idx)++;
		date_imap2sql(p_string_str(search_keys[*idx]), s);
		g_snprintf(d, MIME_FIELD_MAX - 1, db_get_sql(SQL_TO_DATE), "p.internal_date");
		g_snprintf(value->search, MAX_SEARCH_LEN - 1, "%s = '%s'", d, s);
		(*idx)++;

	} else if (MATCH(key, "since")) {
		char s[SQL_INTERNALDATE_LEN];
		memset(s, 0, sizeof (s));
		RETURN_IF_FAIL(search_keys[*idx + 1], -1);
		RETURN_IF_FAIL(check_date(p_string_str(search_keys[*idx + 1])), -1);
		value->type = IST_IDATE;
		(*idx)++;
		date_imap2sql(p_string_str(search_keys[*idx]), s);
		g_snprintf(value->search, MAX_SEARCH_LEN - 1, "p.internal_date > '%s'", s);
		(*idx)++;

	} else if (MATCH(key, "older")) {
		uint64_t seconds;
		char partial[DEF_FRAGSIZE];
		memset(partial, 0, sizeof (partial));
		RETURN_IF_FAIL(search_keys[*idx + 1], -1);
		errno = 0;
		seconds = dm_strtoull(p_string_str(search_keys[*idx + 1]), NULL, 10);
		int ser = errno;
		if (ser) {
			TRACE(TRACE_DEBUG, "%s", strerror(ser));
			return -1;
		}
		value->type = IST_IDATE;
		(*idx)++;
		g_snprintf(partial, DEF_FRAGSIZE - 1, db_get_sql(SQL_WITHIN), seconds);
		g_snprintf(value->search, MAX_SEARCH_LEN - 1, "p.internal_date < %s", partial);
		(*idx)++;

	} else if (MATCH(key, "younger")) {
		uint64_t seconds;
		char partial[DEF_FRAGSIZE];
		memset(partial, 0, sizeof (partial));
		RETURN_IF_FAIL(search_keys[*idx + 1], -1);
		errno = 0;
		seconds = dm_strtoull(p_string_str(search_keys[*idx + 1]), NULL, 10);
		int ser = errno;
		if (ser) {
			TRACE(TRACE_DEBUG, "%s", strerror(ser));
			return -1;
		}
		value->type = IST_IDATE;
		(*idx)++;
		g_snprintf(partial, DEF_FRAGSIZE - 1, db_get_sql(SQL_WITHIN), seconds);
		g_snprintf(value->search, MAX_SEARCH_LEN - 1, "p.internal_date > %s", partial);
		(*idx)++;

	} else if (MATCH(key, "modseq")) {
		const char *token;
		gboolean valid = false;
		uint64_t seq = 0;
		int i = 0;
		RETURN_IF_FAIL(search_keys[*idx + 1], -1);
		(*idx)++;
		for (i = 0; i <= 2; i++) {
			char *rest;
			token = p_string_str(search_keys[*idx + i]);
			TRACE(TRACE_DEBUG, "check token [%d:%s]", i, token);
			if (!token)
				break;
			if (i == 1)
				continue;
			seq = dm_strtoull(token, &rest, 10);
			if (rest != token) {
				valid = true;
				break;
			}
		}
		(*idx) += i + 1;
		RETURN_IF_FAIL(valid, -1);
		self->modseq = seq;
		self->condstore = true;
	}		/*
	 * DATA-keys
	 */

	else if (MATCH(key, "body")) {
		RETURN_IF_FAIL(search_keys[*idx + 1], -1);
		value->type = IST_DATA_BODY;
		IMAP_SET_SEARCH;

	} else if (MATCH(key, "text")) {
		RETURN_IF_FAIL(search_keys[*idx + 1], -1);
		value->type = IST_DATA_TEXT;
		IMAP_SET_SEARCH;
	}/*
	 * SIZE keys
	 */

	else if (MATCH(key, "larger")) {
		RETURN_IF_FAIL(search_keys[*idx + 1], -1);
		value->type = IST_SIZE_LARGER;
		(*idx)++;
		value->size = strtoull(p_string_str(search_keys[(*idx)]), NULL, 10);
		(*idx)++;

	} else if (MATCH(key, "smaller")) {
		RETURN_IF_FAIL(search_keys[*idx + 1], -1);
		value->type = IST_SIZE_SMALLER;
		(*idx)++;
		value->size = strtoull(p_string_str(search_keys[(*idx)]), NULL, 10);
		(*idx)++;

	}/*
	 * NOT, OR, ()
	 */

	else if (MATCH(key, "not")) {
		const char *nextkey;

		RETURN_IF_FAIL(search_keys[*idx + 1], -1);

		nextkey = p_string_str(search_keys[*idx + 1]);

		if (MATCH(nextkey, "answered")) {
			value->type = IST_FLAG;
			strncpy(value->search, "answered_flag=0", MAX_SEARCH_LEN - 1);
			(*idx) += 2;

		} else if (MATCH(nextkey, "deleted")) {
			value->type = IST_FLAG;
			strncpy(value->search, "deleted_flag=0", MAX_SEARCH_LEN - 1);
			(*idx) += 2;

		} else if (MATCH(nextkey, "flagged")) {
			value->type = IST_FLAG;
			strncpy(value->search, "flagged_flag=0", MAX_SEARCH_LEN - 1);
			(*idx) += 2;

		} else if (MATCH(nextkey, "recent")) {
			value->type = IST_FLAG;
			strncpy(value->search, "recent_flag=0", MAX_SEARCH_LEN - 1);
			(*idx) += 2;

		} else if (MATCH(nextkey, "seen")) {
			value->type = IST_FLAG;
			strncpy(value->search, "seen_flag=0", MAX_SEARCH_LEN - 1);
			(*idx) += 2;

		} else if (MATCH(nextkey, "draft")) {
			value->type = IST_FLAG;
			strncpy(value->search, "draft_flag=0", MAX_SEARCH_LEN - 1);
			(*idx) += 2;

		} else if (MATCH(nextkey, "new")) {
			value->type = IST_FLAG;
			strncpy(value->search, "(seen_flag=1 AND recent_flag=0)", MAX_SEARCH_LEN - 1);
			(*idx) += 2;

		} else if (MATCH(nextkey, "old")) {
			value->type = IST_FLAG;
			strncpy(value->search, "recent_flag=1", MAX_SEARCH_LEN - 1);
			(*idx) += 2;

		} else {
			value->type = IST_SUBSEARCH_NOT;
			(*idx)++;

			append_search(self, value, 1);
			if ((result = _handle_search_args(self, search_keys, idx)) < 0)
				return result;
			pop_search(self);

			return 0;
		}

	} else if (MATCH(key, "or")) {
		value->type = IST_SUBSEARCH_OR;
		(*idx)++;

		append_search(self, value, 1);
		if ((result = _handle_search_args(self, search_keys, idx)) < 0)
			return result;
		if ((result = _handle_search_args(self, search_keys, idx)) < 0)
			return result;
		pop_search(self);

		return 0;

	} else if (MATCH(key, "(")) {
		value->type = IST_SUBSEARCH_AND;
		(*idx)++;

		append_search(self, value, 1);
		while ((result = dbmail_mailbox_build_imap_search(self, search_keys, idx, 0)) == 0);
		pop_search(self);

		return 0;

	} else if (MATCH(key, ")")) {
		(*idx)++;
		mempool_push(self->pool, value, sizeof (search_key));
		return 1;

	} else if (check_msg_set(key)) {
		value->type = IST_SET;
		strncpy(value->search, key, MAX_SEARCH_LEN - 1);
		(*idx)++;

		/* ignore the charset. Let the database handle this */
	} else if (MATCH(key, "charset")) {
		(*idx)++; // FIXME: check for valid charset here
		self->charset = p_string_str(search_keys[*idx]);
		TRACE(TRACE_DEBUG, "using charset [%s] for searching", self->charset);
		(*idx)++;
	} else {
		/* unknown search key */
		TRACE(TRACE_DEBUG, "unknown search key [%s]", key);
		mempool_push(self->pool, value, sizeof (search_key));
		return -1;
	}

	if (value->type)
		append_search(self, value, 0);
	else
		mempool_push(self->pool, value, sizeof (search_key));

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
int dbmail_mailbox_build_imap_search(DbmailMailbox *self, String_T *search_keys, uint64_t *idx, search_order order) {
	int result = 0;
	search_key * value, * s;

	if (!(search_keys && search_keys[*idx]))
		return 1;

	/* default initial key for ANDing */
	value = mempool_pop(self->pool, sizeof (search_key));
	value->type = IST_SET;

	if (check_msg_set(p_string_str(search_keys[*idx]))) {
		strncpy(value->search, p_string_str(search_keys[*idx]), MAX_SEARCH_LEN - 1);
		(*idx)++;
	} else {
		/* match all messages if no initial sequence set is defined */
		strncpy(value->search, "1:*", MAX_SEARCH_LEN - 1);
	}
	append_search(self, value, 0);

	/* SORT */
	switch (order) {
		case SEARCH_SORTED:
			value = mempool_pop(self->pool, sizeof (search_key));
			value->type = IST_SORT;
			s = value;
			while (((result = _handle_sort_args(self, search_keys, value, idx)) == 0) && search_keys[*idx]);
			if (result < 0)
				mempool_push(self->pool, s, sizeof (search_key));
			break;
		case SEARCH_THREAD_ORDEREDSUBJECT:
		case SEARCH_THREAD_REFERENCES:
			(*idx)++;
			TRACE(TRACE_DEBUG, "search_key: [%s]", p_string_str(search_keys[*idx]));
			// eat the charset arg
			if (MATCH(p_string_str(search_keys[*idx]), "utf-8"))
				(*idx)++;
			else if (MATCH(p_string_str(search_keys[*idx]), "us-ascii"))
				(*idx)++;
			else if (MATCH(p_string_str(search_keys[*idx]), "iso-8859-1"))
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
	while (search_keys[*idx] && ((result = _handle_search_args(self, search_keys, idx)) == 0));

	TRACE(TRACE_DEBUG, "done [%d] at idx [%" PRIu64 "]", result, *idx);
	return result;
}

static gboolean _do_sort(GNode *node, DbmailMailbox *self) {
	TRACE(TRACE_DEBUG, "Call: _do_sort");
	GString *q;
	uint64_t tid, *id;
	Connection_T c;
	ResultSet_T r;
	volatile int t = FALSE;
	search_key *s = (search_key *) node->data;
	GTree *z;

	TRACE(TRACE_DEBUG, "type [%d]", s->type);

	if (s->type != IST_SORT) return FALSE;

	if (s->searched) return FALSE;

	q = g_string_new("");
	g_string_printf(q, "SELECT m.message_idnr FROM %smessages m "
		"LEFT JOIN %sphysmessage p ON m.physmessage_id=p.id "
		"%s"
		"WHERE m.mailbox_idnr = %" PRIu64 " AND m.status < %d "
		"ORDER BY %smessage_idnr", DBPFX, DBPFX, s->table,
		dbmail_mailbox_get_id(self), MESSAGE_STATUS_DELETE, s->order);

	if (self->sorted) {
		g_list_destroy(self->sorted);
		self->sorted = NULL;
	}

	z = g_tree_new((GCompareFunc) ucmp);
	c = db_con_get();
	TRY
	r = db_query(c, q->str);
	while (db_result_next(r)) {
		tid = db_result_get_u64(r, 0);
		if (g_tree_lookup(self->found, &tid) && (!g_tree_lookup(z, &tid))) {
			id = g_new0(uint64_t, 1);
			*id = tid;
			g_tree_insert(z, id, id);
			self->sorted = g_list_prepend(self->sorted, id);
		}
	}
	CATCH(SQLException)
	LOG_SQLERROR;
	t = DM_EQUERY;
	FINALLY
	db_con_close(c);
	g_tree_destroy(z);
	END_TRY;

	if (t == DM_EQUERY) return TRUE;

	self->sorted = g_list_reverse(self->sorted);

	g_string_free(q, TRUE);

	s->searched = TRUE;

	return FALSE;
}

static GTree * mailbox_search(DbmailMailbox *self, search_key *s) {
	TRACE(TRACE_DEBUG, "Call: mailbox_search");
	uint64_t *k, *v, *w;
	uint64_t id;
	char gt_lt = 0;
	const char *op;
	char partial[DEF_FRAGSIZE];
	Connection_T c;
	ResultSet_T r;
	PreparedStatement_T st = NULL;
	GTree *ids;
	volatile char *inset = NULL;
	/* helper for some operations in TREE mode search */
	char *cond = NULL;
	cond = malloc(30);
	memset(cond, 0, 30);
	int searchPerformed = 0;
	/* if the query will be performed in sql mode */
	int sql;
	sql = 1;

	GString *t;
	String_T q;

	int mailbox_search_strategy = config_get_value_default_int("mailbox_search_strategy", "IMAP", 1);
	TRACE(TRACE_DEBUG, "mailbox_search_strategy %d", mailbox_search_strategy);

	if (self->found && g_tree_nnodes(self->found) <= 200) {
		char *setlist = dbmail_mailbox_ids_as_string(self, TRUE, ",");
		if (setlist) {
			inset = g_strdup_printf("AND m.message_idnr IN (%s)", setlist);
			g_free(setlist);
		}
	}

	c = db_con_get();
	t = g_string_new("");
	q = p_string_new(self->pool, "");
	s->found = g_tree_new_full((GCompareDataFunc) ucmpdata, NULL, (GDestroyNotify) uint64_free, (GDestroyNotify) uint64_free);



	TRY
		/* this searches cannot be made via state */
	switch (s->type) {
		case IST_HDRDATE_ON:
		case IST_HDRDATE_SINCE:
		case IST_HDRDATE_BEFORE:
		{
			searchPerformed = 1;
			char qs[DEF_FRAGSIZE];
			char field[DEF_FRAGSIZE];
			char d[SQL_INTERNALDATE_LEN];
			char date[DEF_FRAGSIZE];
			memset(d, 0, sizeof (d));
			memset(qs, 0, sizeof (qs));
			memset(date, 0, sizeof (date));
			memset(field, 0, sizeof (field));

			g_snprintf(field, DEF_FRAGSIZE - 1, db_get_sql(SQL_TO_DATE), s->hdrfld);
			date_imap2sql(s->search, d);
			g_snprintf(qs, DEF_FRAGSIZE - 1, "'%s'", d);
			g_snprintf(date, DEF_FRAGSIZE - 1, db_get_sql(SQL_TO_DATE), qs);

			if (s->type == IST_HDRDATE_SINCE)
				op = ">=";
			else if (s->type == IST_HDRDATE_BEFORE)
				op = "<";
			else
				op = "=";
			TRACE(TRACE_DEBUG, "IST_HDRDATE_ON/IST_HDRDATE_SINCE/IST_HDRDATE_BEFORE sql (%s) %s", op, field);
			p_string_printf(q, "SELECT message_idnr FROM %smessages m "
				"LEFT JOIN %sheader h USING (physmessage_id) "
				"LEFT JOIN %sheadername n ON h.headername_id = n.id "
				"LEFT JOIN %sheadervalue v ON h.headervalue_id = v.id "
				"WHERE m.mailbox_idnr=? AND m.status < ? "
				"%s "
				"AND n.headername = 'date' "
				"AND %s %s %s ORDER BY message_idnr",
				DBPFX, DBPFX, DBPFX, DBPFX,
				inset ? inset : "",
				field, op, date);

			st = db_stmt_prepare(c, p_string_str(q));
			db_stmt_set_u64(st, 1, dbmail_mailbox_get_id(self));
			db_stmt_set_int(st, 2, MESSAGE_STATUS_DELETE);
		}

			break;

		case IST_HDR:
			searchPerformed = 1;
			TRACE(TRACE_DEBUG, "IST_HDR sql");
			p_string_printf(q, "SELECT message_idnr FROM %smessages m "
				"LEFT JOIN %sheader h USING (physmessage_id) "
				"LEFT JOIN %sheadername n ON h.headername_id = n.id "
				"LEFT JOIN %sheadervalue v ON h.headervalue_id = v.id "
				"WHERE mailbox_idnr=? AND status < ? "
				"%s "
				"AND n.headername = '%s' AND v.headervalue %s ? "
				"ORDER BY message_idnr",
				DBPFX, DBPFX, DBPFX, DBPFX,
				inset ? inset : "",
				s->hdrfld, db_get_sql(SQL_INSENSITIVE_LIKE));

			st = db_stmt_prepare(c, p_string_str(q));
			db_stmt_set_u64(st, 1, dbmail_mailbox_get_id(self));
			db_stmt_set_int(st, 2, MESSAGE_STATUS_DELETE);
			memset(partial, 0, sizeof (partial));
			if (snprintf(partial, DEF_FRAGSIZE - 1, "%%%s%%", s->search) < 0)
				abort();
			db_stmt_set_str(st, 3, partial);

			break;

		case IST_DATA_TEXT:
			searchPerformed = 1;
			TRACE(TRACE_DEBUG, "IST_DATA_TEXT sql");
			p_string_printf(q, "SELECT DISTINCT m.message_idnr "
				"FROM %smimeparts k "
				"LEFT JOIN %spartlists l ON k.id=l.part_id "
				"LEFT JOIN %sphysmessage p ON l.physmessage_id=p.id "
				"LEFT JOIN %sheader h ON h.physmessage_id=p.id "
				"LEFT JOIN %sheadervalue v ON h.headervalue_id=v.id "
				"LEFT JOIN %smessages m ON m.physmessage_id=p.id "
				"WHERE m.mailbox_idnr = ? AND m.status < ? "
				"%s "
				"AND (v.headervalue %s ? OR k.data %s ?) "
				"ORDER BY m.message_idnr",
				DBPFX, DBPFX, DBPFX, DBPFX, DBPFX, DBPFX,
				inset ? inset : "",
				db_get_sql(SQL_INSENSITIVE_LIKE),
				db_get_sql(SQL_SENSITIVE_LIKE)); // pgsql will trip over ilike against bytea 

			st = db_stmt_prepare(c, p_string_str(q));
			db_stmt_set_u64(st, 1, dbmail_mailbox_get_id(self));
			db_stmt_set_int(st, 2, MESSAGE_STATUS_DELETE);

			memset(partial, 0, sizeof (partial));
			if (snprintf(partial, DEF_FRAGSIZE - 1, "%%%s%%", s->search) < 0)
				abort();
			db_stmt_set_str(st, 3, partial);
			db_stmt_set_str(st, 4, partial);

			break;

		case IST_DATA_BODY:
			searchPerformed = 1;
			TRACE(TRACE_DEBUG, "IST_DATA_BODY sql %s", t->str);
			g_string_printf(t, db_get_sql(SQL_ENCODE_ESCAPE), "p.data");
			p_string_printf(q, "SELECT DISTINCT m.message_idnr FROM %smimeparts p "
				"LEFT JOIN %spartlists l ON p.id=l.part_id "
				"LEFT JOIN %sphysmessage s ON l.physmessage_id=s.id "
				"LEFT JOIN %smessages m ON m.physmessage_id=s.id "
				"LEFT JOIN %smailboxes b ON m.mailbox_idnr = b.mailbox_idnr "
				"WHERE b.mailbox_idnr=? AND m.status < ? "
				"%s "
				"AND (l.part_key > 1 OR l.is_header=0) "
				"AND %s %s ? "
				"ORDER BY m.message_idnr",
				DBPFX, DBPFX, DBPFX, DBPFX, DBPFX,
				inset ? inset : "",
				t->str, db_get_sql(SQL_SENSITIVE_LIKE)); // pgsql will trip over ilike against bytea 

			st = db_stmt_prepare(c, p_string_str(q));
			db_stmt_set_u64(st, 1, dbmail_mailbox_get_id(self));
			db_stmt_set_int(st, 2, MESSAGE_STATUS_DELETE);

			memset(partial, 0, sizeof (partial));
			if (snprintf(partial, DEF_FRAGSIZE - 1, "%%%s%%", s->search) < 0)
				abort();

			db_stmt_set_str(st, 3, partial);

			break;

		case IST_KEYWORD:
		case IST_UNKEYWORD:
			searchPerformed = 1;
			TRACE(TRACE_DEBUG, "IST_KEYWORD/IST_UNKEYWORD sql %s", s->search);
			p_string_printf(q, "SELECT m.message_idnr FROM %smessages m "
				"JOIN %skeywords k ON m.message_idnr=k.message_idnr "
				"WHERE mailbox_idnr=? AND status < ? "
				"%s "
				"AND k.keyword = ? ORDER BY message_idnr",
				DBPFX, DBPFX,
				inset ? inset : "");

			st = db_stmt_prepare(c, p_string_str(q));
			db_stmt_set_u64(st, 1, dbmail_mailbox_get_id(self));
			db_stmt_set_int(st, 2, MESSAGE_STATUS_DELETE);
			db_stmt_set_str(st, 3, s->search);
			break;
	}


	if (searchPerformed == 0 && mailbox_search_strategy == 1) {
		/* general queries have not been executed and strategy is sql */
		switch (s->type) {
			case IST_IDATE:
				TRACE(TRACE_DEBUG, "IST_IDATE sql %s", s->search);
				/* implemented search by state, avoiding queries in DB */
				p_string_printf(q, "SELECT message_idnr FROM %smessages m "
					"LEFT JOIN %sphysmessage p ON m.physmessage_id=p.id "
					"WHERE mailbox_idnr = ? AND status < ? "
					"%s "
					"AND %s "
					"ORDER BY message_idnr",
					DBPFX, DBPFX,
					inset ? inset : "",
					s->search);

				st = db_stmt_prepare(c, p_string_str(q));
				db_stmt_set_u64(st, 1, dbmail_mailbox_get_id(self));
				db_stmt_set_int(st, 2, MESSAGE_STATUS_DELETE);

				break;
			case IST_SIZE_LARGER:
				searchPerformed = 1;
				gt_lt = '>';
				// fallthrough

			case IST_SIZE_SMALLER:
				if (!gt_lt) gt_lt = '<';
				TRACE(TRACE_DEBUG, "IST_SIZE_SMALLER/IST_SIZE_LARGER sql %ld", s->size);
				p_string_printf(q, "SELECT m.message_idnr FROM %smessages m "
					"LEFT JOIN %sphysmessage p ON m.physmessage_id = p.id "
					"WHERE m.mailbox_idnr = ? AND m.status < ? "
					"%s "
					"AND p.rfcsize %c ? "
					"ORDER BY message_idnr",
					DBPFX, DBPFX,
					inset ? inset : "",
					gt_lt);

				st = db_stmt_prepare(c, p_string_str(q));
				db_stmt_set_u64(st, 1, dbmail_mailbox_get_id(self));
				db_stmt_set_int(st, 2, MESSAGE_STATUS_DELETE);
				db_stmt_set_u64(st, 3, s->size);

				break;


			default:
				TRACE(TRACE_DEBUG, "IST* sql %s", s->search);
				/* other searches */
				p_string_printf(q, "SELECT message_idnr FROM %smessages "
					"WHERE mailbox_idnr = ? AND status < ? AND %s "
					"ORDER BY message_idnr", DBPFX,
					s->search); // FIXME: Sometimes s->search is ""

				st = db_stmt_prepare(c, p_string_str(q));
				db_stmt_set_u64(st, 1, dbmail_mailbox_get_id(self));
				db_stmt_set_int(st, 2, MESSAGE_STATUS_DELETE);
		}
	}
	if (searchPerformed == 0 && mailbox_search_strategy == 2) {
		int IST_MBS_COND = 0;
		/* general queries have not been executed and strategy is tree/state */
		/* handle old default case, we need to test it the hard way */
		if (strcmp(s->search, "answered_flag=1") == 0) {
			TRACE(TRACE_DEBUG, "IST* answered_flag=1");
			sql = 0;
			IST_MBS_COND = 1;
		}
		if (strcmp(s->search, "deleted_flag=1") == 0) {
			TRACE(TRACE_DEBUG, "IST* deleted_flag=1");
			sql = 0;
			IST_MBS_COND = 2;
		}
		if (strcmp(s->search, "flagged_flag=1") == 0) {
			TRACE(TRACE_DEBUG, "IST* flagged_flag=1");
			sql = 0;
			IST_MBS_COND = 3;
		}
		if (strcmp(s->search, "recent_flag=1") == 0) {
			TRACE(TRACE_DEBUG, "IST* recent_flag=1");
			sql = 0;
			IST_MBS_COND = 4;
		}
		if (strcmp(s->search, "seen_flag=1") == 0) {
			TRACE(TRACE_DEBUG, "IST* seen_flag=1");
			sql = 0;
			IST_MBS_COND = 5;
		}
		if (strcmp(s->search, "draft_flag=1") == 0) {
			TRACE(TRACE_DEBUG, "IST* draft_flag=1");
			sql = 0;
			IST_MBS_COND = 6;
		}
		if (strcmp(s->search, "(seen_flag=0 AND recent_flag=1)") == 0) {
			TRACE(TRACE_DEBUG, "IST* (seen_flag=0 AND recent_flag=1)");
			sql = 0;
			IST_MBS_COND = 7;
		}
		if (strcmp(s->search, "recent_flag=0") == 0) {
			TRACE(TRACE_DEBUG, "IST* recent_flag=1");
			sql = 0;
			IST_MBS_COND = 8;
		}
		if (strcmp(s->search, "answered_flag=0") == 0) {
			TRACE(TRACE_DEBUG, "IST* answered_flag=1");
			sql = 0;
			IST_MBS_COND = 9;
		}
		if (strcmp(s->search, "deleted_flag=0") == 0) {
			TRACE(TRACE_DEBUG, "IST* deleted_flag=1");
			sql = 0;
			IST_MBS_COND = 10;
		}
		if (strcmp(s->search, "flagged_flag=0") == 0) {
			TRACE(TRACE_DEBUG, "IST* flagged_flag=1");
			sql = 0;
			IST_MBS_COND = 11;
		}
		if (strcmp(s->search, "seen_flag=0") == 0) {
			TRACE(TRACE_DEBUG, "IST* seen_flag=1");
			sql = 0;
			IST_MBS_COND = 12;
		}
		if (strcmp(s->search, "draft_flag=0") == 0) {
			TRACE(TRACE_DEBUG, "IST* draft_flag=1");
			sql = 0;
			IST_MBS_COND = 13;
		}
		/* handle IST_DATE */
		if (strstr(s->search, "p.internal_date")) {
			/* the equivalent in query is at IST_IDATE, see above */
			if (strstr(s->search, ">")) {
				sql = 0;
				IST_MBS_COND = 14;
				strcpy(cond, strstr(s->search, ">") + 1);
				TRACE(TRACE_DEBUG, "IST_IDATE [>]");
			}
			if (strstr(s->search, "=")) {
				sql = 0;
				IST_MBS_COND = 15;
				strcpy(cond, strstr(s->search, "=") + 1);
				TRACE(TRACE_DEBUG, "IST_IDATE [=]");
			}
			if (strstr(s->search, "<")) {
				sql = 0;
				IST_MBS_COND = 16;
				strcpy(cond, strstr(s->search, "<") + 1);
				TRACE(TRACE_DEBUG, "IST_IDATE [<]");
			}
			if (IST_MBS_COND != 0) {
				p_trim(cond, " '><="); //trimming, just a safety precaution
				TRACE(TRACE_DEBUG, "IST_IDATE %s -> %s ", s->search, cond);
			}

		}
		if (s->type == IST_SIZE_LARGER) {
			TRACE(TRACE_DEBUG, "IST_SIZE_LARGER");
			sql = 0;
			IST_MBS_COND = 17;

		}
		if (s->type == IST_SIZE_SMALLER) {
			TRACE(TRACE_DEBUG, "IST_SIZE_SMALLER");
			sql = 0;
			IST_MBS_COND = 18;
		}		
		//TRACE(TRACE_DEBUG,"IST_IDATE %s -> %d %d", s->search, sql,IST_MBS_COND);
		if (sql == 0) {
			int foundItems = 0;
			ids = MailboxState_getIds(self->mbstate);
			GList *uids = g_tree_keys(ids);
			uids = g_list_first(uids);
			/* creating another tree by rebuilding it against a condition */
			/* we shoud have used g_tree_foreach?! may have been faster and more memory efficient */
			/* @todo  g_tree_foreach */
			while (uids) {
				uint64_t id = *(uint64_t *) uids->data;
				/* no need to check it, is the same list*/
				/*if (! (w = g_tree_lookup(ids, &id))) {
					TRACE(TRACE_ERR, "key missing in ids: [%" PRIu64 "]", id);
					if (! g_list_next(uids)) break;
					uids = g_list_next(uids);
					continue;
				}*/
				MessageInfo *msginfo = g_tree_lookup(MailboxState_getMsginfo(self->mbstate), &id);

				int found = 0;
				switch (IST_MBS_COND) {
					case 1: found = (int) (msginfo->flags[IMAP_FLAG_ANSWERED] == 1);
						break;
					case 2: found = (int) (msginfo->flags[IMAP_FLAG_DELETED] == 1);
						break;
					case 3: found = (int) (msginfo->flags[IMAP_FLAG_FLAGGED] == 1);
						break;
					case 4: found = (int) (msginfo->flags[IMAP_FLAG_RECENT] == 1);
						break;
					case 5: found = (int) (msginfo->flags[IMAP_FLAG_SEEN] == 1);
						break;
					case 6: found = (int) (msginfo->flags[IMAP_FLAG_DRAFT] == 1);
						break;
					case 7: found = (int) (msginfo->flags[IMAP_FLAG_SEEN] == 0 && msginfo->flags[IMAP_FLAG_RECENT] == 1);
						break;
					case 8: found = (int) (msginfo->flags[IMAP_FLAG_RECENT] == 0);
						break;
					case 9: found = (int) (msginfo->flags[IMAP_FLAG_ANSWERED] == 0);
						break;
					case 10: found = (int) (msginfo->flags[IMAP_FLAG_DELETED] == 0);
						break;
					case 11: found = (int) (msginfo->flags[IMAP_FLAG_FLAGGED] == 0);
						break;
					case 12: found = (int) (msginfo->flags[IMAP_FLAG_SEEN] == 0);
						break;
					case 13: found = (int) (msginfo->flags[IMAP_FLAG_DRAFT] == 0);
						break;
					case 14: found = (int) (strcmp(msginfo->internaldate, cond) > 0);
						break;
					case 15: found = (int) (strcmp(msginfo->internaldate, cond) == 0);
						break;
					case 16: found = (int) (strcmp(msginfo->internaldate, cond) < 0);
						break;
						//IST_SIZE_LARGER
					case 17: found = (int) (msginfo->rfcsize > s->size);
						break;
						//IST_SIZE_SMALLER
					case 18: found = (int) (msginfo->rfcsize < s->size);
						break;

				}
				if (found == 1) {
					//TRACE(TRACE_DEBUG, "Found (%d) %ld vs %ld ",IST_MBS_COND,id,msginfo->uid); 
					k = mempool_pop(small_pool, sizeof (uint64_t));
					v = mempool_pop(small_pool, sizeof (uint64_t));
					*k = id;
					*v = id;

					g_tree_insert(s->found, k, v);
					foundItems++;
				}
				if (!g_list_next(uids)) break;
				uids = g_list_next(uids);
			}
			g_list_free(g_list_first(uids));
			if (cond) {
				TRACE(TRACE_DEBUG, "IST RESULT TREE (message_search_strategy=2) found %s (%s), found  %d",
					s->search,
					cond,
					foundItems);
			} else {
				TRACE(TRACE_DEBUG, "IST RESULT TREE (message_search_strategy=2) found %s, found  %d", s->search, foundItems);
			}
		} else {
			TRACE(TRACE_WARNING, "IST unhandled search for message_search_strategy=2 so fall back (should be handled)");
			p_string_printf(q, "SELECT message_idnr FROM %smessages "
				"WHERE mailbox_idnr = ? AND status < ? AND %s "
				"ORDER BY message_idnr", DBPFX,
				s->search); // FIXME: Sometimes s->search is ""

			st = db_stmt_prepare(c, p_string_str(q));
			db_stmt_set_u64(st, 1, dbmail_mailbox_get_id(self));
			db_stmt_set_int(st, 2, MESSAGE_STATUS_DELETE);
		}


	}
	/* due to the fact that may be some issues related to the redirecting of queries for strategy = 2*/
	/* we may need to test for st not being null */
	if (st && sql == 1) {
		int foundItems = 0;
		r = db_stmt_query(st);

		ids = MailboxState_getIds(self->mbstate);
		while (db_result_next(r)) {
			id = db_result_get_u64(r, 0);
			if (!(w = g_tree_lookup(ids, &id))) {
				TRACE(TRACE_ERR, "key missing in ids: [%" PRIu64 "]\n", id);
				continue;
			}
			assert(w);

			k = mempool_pop(small_pool, sizeof (uint64_t));
			v = mempool_pop(small_pool, sizeof (uint64_t));
			*k = id;
			*v = *w;
			g_tree_insert(s->found, k, v);
			foundItems++;
		}
		TRACE(TRACE_DEBUG, "IST RESULT SQL found %s, found  %d", s->search, foundItems);
	}
	if (s->type == IST_UNKEYWORD) {
		GTree *old = NULL;
		GTree *invert = g_tree_new_full((GCompareDataFunc) ucmpdata, NULL, (GDestroyNotify) uint64_free, (GDestroyNotify) uint64_free);
		GList *uids = g_tree_keys(ids);
		uids = g_list_first(uids);
		while (uids) {
			uint64_t id = *(uint64_t *) uids->data;
			if (!(w = g_tree_lookup(ids, &id))) {
				TRACE(TRACE_ERR, "key missing in ids: [%" PRIu64 "]", id);

				if (!g_list_next(uids)) break;
				uids = g_list_next(uids);
				continue;
			}

			assert(w);

			if (g_tree_lookup(s->found, &id)) {
				TRACE(TRACE_DEBUG, "skip key [%" PRIu64 "]", id);

				if (!g_list_next(uids)) break;
				uids = g_list_next(uids);
				continue;
			}

			k = mempool_pop(small_pool, sizeof (uint64_t));
			v = mempool_pop(small_pool, sizeof (uint64_t));
			*k = id;
			*v = *w;

			g_tree_insert(invert, k, v);


			if (!g_list_next(uids)) break;
			uids = g_list_next(uids);

		}

		old = s->found;
		s->found = invert;
		g_tree_destroy(old);
		g_list_free(g_list_first(uids));
	}

	CATCH(SQLException)
	LOG_SQLERROR;
	FINALLY
	db_con_close(c);
	END_TRY;

	if (inset)
		g_free((char *) inset);

	p_string_free(q, TRUE);
	g_string_free(t, TRUE);
	free(cond);

	return s->found;
}

static int checkset(const char *s) {
	int i;
	for (i = 0; s[i]; i++) {
		if (!strchr("0123456789:,*", s[i]))
			return 0;
	}
	return 1;
}

struct filter_modseq_helper {
	GTree *msginfo;
	uint64_t modseq;
	GList *remove;
};

static int filter_modseq(gpointer key, gpointer UNUSED value, gpointer data) {
	struct filter_modseq_helper *d = (struct filter_modseq_helper *) data;
	uint64_t *id;

	id = (uint64_t *) key;
	MessageInfo *info = g_tree_lookup(d->msginfo, id);
	if (!info)
		return TRUE;

	if (info->seq <= d->modseq)
		d->remove = g_list_prepend(d->remove, key);
	return FALSE;
}

GTree * find_modseq(DbmailMailbox *self, GTree *in) {
	if (!self->modseq)
		return in;
	GList *remove;
	struct filter_modseq_helper data;
	data.msginfo = MailboxState_getMsginfo(self->mbstate);
	data.modseq = self->modseq;
	data.remove = NULL;

	g_tree_foreach(in, (GTraverseFunc) filter_modseq, &data);
	remove = data.remove;
	while (remove) {
		uint64_t *key = (uint64_t *) remove->data;
		g_tree_remove(in, key);
		if (!g_list_next(remove))
			break;
		remove = g_list_next(remove);
	}

	return in;
}

GTree * dbmail_mailbox_get_set(DbmailMailbox *self, const char *set, gboolean uid) {
	GTree *uids;
	GTree *b;

	TRACE(TRACE_DEBUG, "[%s] uid [%d]", set, uid);

	if (!self->mbstate)
		return NULL;

	assert(self && self->mbstate && set);

	uids = MailboxState_getIds(self->mbstate);
	if ((!uid) && (g_tree_nnodes(uids) == 0))
		return NULL;

	if (!checkset(set)) // invalid chars
		return NULL;


	b = MailboxState_get_set(self->mbstate, set, uid);
	return find_modseq(self, b);
}

static gboolean _found_tree_copy(uint64_t *key, uint64_t *val, GTree *tree) {
	uint64_t *a, *b;
	a = g_new0(uint64_t, 1);
	b = g_new0(uint64_t, 1);
	*a = *key;
	*b = *val;
	g_tree_insert(tree, a, b);
	return FALSE;
}

static gboolean _shallow_tree_copy(uint64_t *key, uint64_t *val, GTree *tree) {
	g_tree_insert(tree, key, val);
	return FALSE;
}

static gboolean _prescan_search(GNode *node, DbmailMailbox *self) {
	search_key *s = (search_key *) node->data;

	if (s->searched) return FALSE;

	switch (s->type) {
		case IST_SET:
			if (!(s->found = dbmail_mailbox_get_set(self, (const char *) s->search, 0)))
				return TRUE;
			break;
		case IST_UIDSET:
			if (!(s->found = dbmail_mailbox_get_set(self, (const char *) s->search, 1)))
				return TRUE;
			break;
		default:
			return FALSE;

	}
	s->searched = TRUE;

	g_tree_merge(self->found, s->found, IST_SUBSEARCH_AND);
	s->merged = TRUE;

	TRACE(TRACE_DEBUG, "[%p] depth [%d] type [%d] rows [%d]\n",
		s, g_node_depth(node), s->type, s->found ? g_tree_nnodes(s->found) : 0);

	g_tree_destroy(s->found);
	s->found = NULL;

	return FALSE;
}

static gboolean _do_search(GNode *node, DbmailMailbox *self) {
	search_key *s = (search_key *) node->data;

	if (s->searched) return FALSE;

	switch (s->type) {
		case IST_SORT:
			return FALSE;
			break;

		case IST_SET:
			if (!(s->found = dbmail_mailbox_get_set(self, (const char *) s->search, 0)))
				return TRUE;
			break;
		case IST_UIDSET:
			if (!(s->found = dbmail_mailbox_get_set(self, (const char *) s->search, 1)))
				return TRUE;
			break;

		case IST_KEYWORD:
		case IST_UNKEYWORD:
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
			g_node_children_foreach(node, G_TRAVERSE_ALL, (GNodeForeachFunc) _do_search, (gpointer) self);
			s->found = g_tree_new_full((GCompareDataFunc) ucmpdata, NULL, (GDestroyNotify) g_free, (GDestroyNotify) g_free);
			break;


		default:
			return TRUE;
	}

	s->searched = TRUE;

	TRACE(TRACE_DEBUG, "[%p] depth [%d] type [%d] rows [%d]\n",
		s, g_node_depth(node), s->type, s->found ? g_tree_nnodes(s->found) : 0);

	return FALSE;
}

static gboolean _merge_search(GNode *node, GTree *found) {
	search_key *s = (search_key *) node->data;
	search_key *a, *b;
	GNode *x, *y;

	if (s->type == IST_SORT)
		return FALSE;

	if (s->merged == TRUE)
		return FALSE;


	switch (s->type) {
		case IST_SUBSEARCH_AND:
			g_node_children_foreach(node, G_TRAVERSE_ALL, (GNodeForeachFunc) _merge_search, (gpointer) found);
			break;

		case IST_SUBSEARCH_NOT:
			g_tree_foreach(found, (GTraverseFunc) _found_tree_copy, s->found);
			g_node_children_foreach(node, G_TRAVERSE_ALL, (GNodeForeachFunc) _merge_search, (gpointer) s->found);
			g_tree_merge(found, s->found, IST_SUBSEARCH_NOT);
			s->merged = TRUE;
			g_tree_destroy(s->found);
			s->found = NULL;

			break;

		case IST_SUBSEARCH_OR:
			x = g_node_nth_child(node, 0);
			y = g_node_nth_child(node, 1);
			a = (search_key *) x->data;
			b = (search_key *) y->data;

			if (a->type == IST_SUBSEARCH_AND) {
				g_tree_foreach(found, (GTraverseFunc) _found_tree_copy, a->found);
				g_node_children_foreach(x, G_TRAVERSE_ALL, (GNodeForeachFunc) _merge_search, (gpointer) a->found);
			}

			if (b->type == IST_SUBSEARCH_AND) {
				g_tree_foreach(found, (GTraverseFunc) _found_tree_copy, b->found);
				g_node_children_foreach(y, G_TRAVERSE_ALL, (GNodeForeachFunc) _merge_search, (gpointer) b->found);
			}

			g_tree_merge(a->found, b->found, IST_SUBSEARCH_OR);
			b->merged = TRUE;
			g_tree_destroy(b->found);
			b->found = NULL;

			g_tree_merge(s->found, a->found, IST_SUBSEARCH_OR);
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

	TRACE(TRACE_DEBUG, "[%p] leaf [%d] depth [%d] type [%d] found [%d]",
		s, G_NODE_IS_LEAF(node), g_node_depth(node), s->type, found ? g_tree_nnodes(found) : 0);

	return FALSE;
}

int dbmail_mailbox_sort(DbmailMailbox *self) {
	if (!self->search) return 0;

	g_node_traverse(g_node_get_root(self->search), G_PRE_ORDER, G_TRAVERSE_ALL, -1,
		(GNodeTraverseFunc) _do_sort, (gpointer) self);

	return 0;
}

int dbmail_mailbox_search(DbmailMailbox *self) {
	TRACE(TRACE_DEBUG, "Call: dbmail_mailbox_search");
	GTree *ids;
	if (!self->search) return 0;

	if (!self->mbstate)
		dbmail_mailbox_open(self);

	if (self->found) g_tree_destroy(self->found);
	self->found = g_tree_new_full((GCompareDataFunc) ucmpdata, NULL, NULL, NULL);

	ids = MailboxState_getIds(self->mbstate);

	g_tree_foreach(ids, (GTraverseFunc) _shallow_tree_copy, self->found);

	g_node_traverse(g_node_get_root(self->search), G_LEVEL_ORDER, G_TRAVERSE_ALL, 2,
		(GNodeTraverseFunc) _prescan_search, (gpointer) self);

	g_node_traverse(g_node_get_root(self->search), G_PRE_ORDER, G_TRAVERSE_ALL, -1,
		(GNodeTraverseFunc) _do_search, (gpointer) self);

	g_node_traverse(g_node_get_root(self->search), G_PRE_ORDER, G_TRAVERSE_ALL, -1,
		(GNodeTraverseFunc) _merge_search, (gpointer) self->found);

	if (self->found == NULL)
		TRACE(TRACE_DEBUG, "found no ids\n");
	else
		TRACE(TRACE_DEBUG, "found [%d] ids\n", self->found ? g_tree_nnodes(self->found) : 0);

	return 0;
}

