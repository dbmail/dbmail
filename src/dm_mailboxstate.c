/*
  
 Copyright (c) 2004-2012 NFG Net Facilities Group BV support@nfg.nl

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

#include "dbmail.h"
#include "dm_mailboxstate.h"

#define THIS_MODULE "MailboxState"

/*
 */
extern DBParam_T db_params;
extern const char *imap_flag_desc_escaped[];
#define DBPFX db_params.pfx

#define T MailboxState_T

struct T {
	Mempool_T pool;
	gboolean freepool;
	uint64_t id;
	uint64_t uidnext;
	uint64_t owner_id;
	uint64_t seq;
	//
	unsigned no_select;
	unsigned no_children;
	unsigned no_inferiors;
	unsigned recent;
	unsigned exists;
	unsigned unseen;
	unsigned permission;
	// 
	gboolean is_subscribed;
	gboolean is_public;
	gboolean is_users;
	gboolean is_inbox;
	//
	String_T name;
	GTree *keywords;
	GTree *msginfo;
	GTree *ids;
	GTree *msn;
	GTree *recent_queue;
};

static void state_load_metadata(T M, Connection_T c);
static void MailboxState_setMsginfo(T M, GTree *msginfo);
/* */

static void MailboxState_uid_msn_new(T M)
{
	if (M->msn) g_tree_destroy(M->msn);
	M->msn = g_tree_new_full((GCompareDataFunc)ucmpdata,NULL,NULL,NULL);

	if (M->ids) g_tree_destroy(M->ids);
	M->ids = g_tree_new_full((GCompareDataFunc)ucmpdata,NULL,NULL,(GDestroyNotify)g_free);
}
static void MessageInfo_free(MessageInfo *m)
{
	g_list_destroy(m->keywords);
	g_free(m);
}

static T state_load_messages(T M, Connection_T c)
{
	unsigned nrows = 0, i = 0, j;
	const char *query_result, *keyword;
	MessageInfo *result;
	GTree *msginfo;
	uint64_t *uid, id = 0;
	ResultSet_T r;
	Field_T frag;
	INIT_QUERY;

	date2char_str("internal_date", &frag);
	snprintf(query, DEF_QUERYSIZE,
			"SELECT seen_flag, answered_flag, deleted_flag, flagged_flag, "
			"draft_flag, recent_flag, %s, rfcsize, message_idnr FROM %smessages m "
			"LEFT JOIN %sphysmessage p ON p.id = m.physmessage_id "
			"WHERE m.mailbox_idnr = %lu AND m.status IN (%d,%d) ORDER BY message_idnr ASC",
			frag, DBPFX, DBPFX, M->id, MESSAGE_STATUS_NEW, MESSAGE_STATUS_SEEN);

	msginfo = g_tree_new_full((GCompareDataFunc)ucmpdata, NULL,(GDestroyNotify)g_free,(GDestroyNotify)MessageInfo_free);

	r = db_query(c,query);

	i = 0;
	while (db_result_next(r)) {
		i++;

		id = db_result_get_u64(r,IMAP_NFLAGS + 2);

		uid = g_new0(uint64_t,1); *uid = id;

		result = g_new0(MessageInfo,1);

		/* id */
		result->uid = id;

		/* mailbox_id */
		result->mailbox_id = M->id;

		/* flags */
		for (j = 0; j < IMAP_NFLAGS; j++)
			result->flags[j] = db_result_get_bool(r,j);

		/* internal date */
		query_result = db_result_get(r,IMAP_NFLAGS);
		strncpy(result->internaldate,
				(query_result) ? query_result :
				"01-Jan-1970 00:00:01 +0100",
				IMAP_INTERNALDATE_LEN);

		/* rfcsize */
		result->rfcsize = db_result_get_u64(r,IMAP_NFLAGS + 1);

		g_tree_insert(msginfo, uid, result); 

	}

	if (! i) { // empty mailbox
		MailboxState_setMsginfo(M, msginfo);
		return M;
	}

	db_con_clear(c);

	memset(query,0,sizeof(query));
	snprintf(query, DEF_QUERYSIZE,
		"SELECT k.message_idnr, keyword FROM %skeywords k "
		"LEFT JOIN %smessages m ON k.message_idnr=m.message_idnr "
		"LEFT JOIN %smailboxes b ON m.mailbox_idnr=b.mailbox_idnr "
		"WHERE b.mailbox_idnr = %lu AND m.status IN (%d,%d)",
		DBPFX, DBPFX, DBPFX,
		M->id, MESSAGE_STATUS_NEW, MESSAGE_STATUS_SEEN);

	nrows = 0;
	r = db_query(c,query);
	while (db_result_next(r)) {
		nrows++;
		id = db_result_get_u64(r,0);
		keyword = db_result_get(r,1);
		if ((result = g_tree_lookup(msginfo, &id)) != NULL)
			result->keywords = g_list_append(result->keywords, g_strdup(keyword));
	}

	if (! nrows) TRACE(TRACE_DEBUG, "no keywords");

	MailboxState_setMsginfo(M, msginfo);

	return M;
}

gboolean _compare_data(gconstpointer a, gconstpointer b, gpointer UNUSED data)
{
	return strcmp((const char *)a,(const char *)b);
}

T MailboxState_new(Mempool_T pool, uint64_t id)
{
	T M; Connection_T c;
	volatile int t = DM_SUCCESS;
	gboolean freepool = FALSE;

	if (! pool) {
		pool = mempool_open();
		freepool = TRUE;
	}

	M = mempool_pop(pool, sizeof(*M));
	M->pool = pool;
	M->freepool = freepool;

	if (! id) return M;

	M->id = id;
	M->recent_queue = g_tree_new((GCompareFunc)ucmp);
	M->keywords     = g_tree_new_full((GCompareDataFunc)_compare_data,NULL,g_free,NULL);

	c = db_con_get();
	TRY
		db_begin_transaction(c); // we need read-committed isolation
		state_load_metadata(M, c);
		state_load_messages(M, c);
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_commit_transaction(c);
		db_con_close(c);
	END_TRY;

	if (t == DM_EQUERY) {
		TRACE(TRACE_ERR, "Error opening mailbox");
		MailboxState_free(&M);
	}

	return M;
}

void MailboxState_remap(T M)
{
	GList *ids = NULL;
	uint64_t *uid, *msn = NULL, rows = 1;
	MessageInfo *msginfo;

	MailboxState_uid_msn_new(M);

	ids = g_tree_keys(M->msginfo);
	ids = g_list_first(ids);
	while (ids) {
		uid = (uint64_t *)ids->data;

		msginfo = g_tree_lookup(M->msginfo, uid);

		msn = g_new0(uint64_t,1);
		*msn = msginfo->msn = rows++;

		g_tree_insert(M->ids, uid, msn);
		g_tree_insert(M->msn, msn, uid);

		if (! g_list_next(ids)) break;
		ids = g_list_next(ids);
	}

	g_list_free(g_list_first(ids));
}
	
GTree * MailboxState_getMsginfo(T M)
{
	return M->msginfo;
}

static void MailboxState_setMsginfo(T M, GTree *msginfo)
{
	GTree *oldmsginfo = M->msginfo;
	M->msginfo = msginfo;
	MailboxState_remap(M);
	if (oldmsginfo) g_tree_destroy(oldmsginfo);
}

void MailboxState_addMsginfo(T M, uint64_t uid, MessageInfo *msginfo)
{
	uint64_t *id = g_new0(uint64_t,1);
	*id = uid;
	g_tree_insert(M->msginfo, id, msginfo); 
	if (msginfo->flags[IMAP_FLAG_RECENT] == 1)
		M->recent++;
	MailboxState_build_recent(M);
	MailboxState_remap(M);
}

int MailboxState_removeUid(T M, uint64_t uid)
{
	if (! g_tree_remove(M->msginfo, &uid)) {
		TRACE(TRACE_WARNING,"trying to remove unknown UID [%lu]", uid);
		return DM_EGENERAL;
	}

	M->exists--;

	MailboxState_remap(M);

	return DM_SUCCESS;
}

GTree * MailboxState_getIds(T M)
{
	return M->ids;
}

GTree * MailboxState_getMsn(T M)
{
	return M->msn;
}

void MailboxState_setId(T M, uint64_t id)
{
	M->id = id;
}
uint64_t MailboxState_getId(T M)
{
	return M->id;
}

uint64_t MailboxState_getSeq(T M)
{
	return M->seq;
}

unsigned MailboxState_getExists(T M)
{
	int real = g_tree_nnodes(M->msginfo);
	if (real > (int)M->exists)
		M->exists = (unsigned)real;
	return M->exists;
}

unsigned MailboxState_getRecent(T M)
{
	return M->recent;
}

uint64_t MailboxState_getUidnext(T M)
{
	return M->uidnext;
}
void MailboxState_setOwner(T M, uint64_t owner_id)
{
	M->owner_id = owner_id;
}
uint64_t MailboxState_getOwner(T M)
{
	return M->owner_id;
}

void MailboxState_setPermission(T M, int permission)
{
	M->permission = permission;
}

unsigned MailboxState_getPermission(T M)
{
	return M->permission;
}

void MailboxState_setName(T M, const char *name)
{
	String_T old = M->name;
	M->name = p_string_new(M->pool, name);
	if (old)
		p_string_free(old, TRUE);
}

const char * MailboxState_getName(T M)
{
	return p_string_str(M->name);
}

void MailboxState_setIsUsers(T M, gboolean t)
{
	M->is_users = t;
}

gboolean MailboxState_isUsers(T M)
{
	return M->is_users;
}

void MailboxState_setIsPublic(T M, gboolean t)
{
	M->is_public = t;
}
gboolean MailboxState_isPublic(T M)
{
	return M->is_public;
}
	
gboolean MailboxState_hasKeyword(T M, const char *keyword)
{
	if (g_tree_lookup(M->keywords, (gpointer)keyword))
		return TRUE;
	return FALSE;
}
void MailboxState_addKeyword(T M, const char *keyword)
{
	char *kw = g_strdup(keyword);
	g_tree_insert(M->keywords, kw, kw);
}

void MailboxState_setNoSelect(T M, gboolean no_select)
{
	M->no_select = no_select;
}

gboolean MailboxState_noSelect(T M)
{
	return M->no_select;
}
void MailboxState_setNoChildren(T M, gboolean no_children)
{
	M->no_children = no_children;
}

gboolean MailboxState_noChildren(T M)
{
	return M->no_children;
}

gboolean MailboxState_noInferiors(T M)
{
	return M->no_inferiors;
}

unsigned MailboxState_getUnseen(T M)
{
	return M->unseen;
}

static gboolean _free_recent_queue(gpointer key, gpointer UNUSED value, gpointer data)
{
	T M = (T)data;
	mempool_push(M->pool, key, sizeof(uint64_t));
	return FALSE;
}

void MailboxState_free(T *M)
{
	T s = *M;
	if (s->name) 
		p_string_free(s->name, TRUE);

	g_tree_destroy(s->keywords);
	s->keywords = NULL;

	if (s->msn) g_tree_destroy(s->msn);
	s->msn = NULL;

	if (s->ids) g_tree_destroy(s->ids);		
	s->ids = NULL;

	if (s->msginfo) g_tree_destroy(s->msginfo);
	s->msginfo = NULL;

	if (s->recent_queue) {
		g_tree_foreach(s->recent_queue, (GTraverseFunc)_free_recent_queue, s);
		g_tree_destroy(s->recent_queue);
	}
	s->recent_queue = NULL;

	if (s->freepool) {
		Mempool_T pool = s->pool;
		mempool_close(&pool);
	} else {
		mempool_push(s->pool, s, sizeof(*s));
	}

	s = NULL;
}

static void db_getmailbox_permission(T M, Connection_T c)
{
	ResultSet_T r;
	g_return_if_fail(M->id);

	r = db_query(c, "SELECT permission FROM %smailboxes WHERE mailbox_idnr = %lu",
			DBPFX, M->id);
	if (db_result_next(r))
		M->permission = db_result_get_int(r, 0);
}

static void db_getmailbox_info(T M, Connection_T c)
{
	/* query mailbox for LIST results */
	ResultSet_T r;
	char *mbxname, *name, *pattern;
	struct mailbox_match *mailbox_like = NULL;
	GString *fqname, *qs;
	int i=0, prml;
	PreparedStatement_T stmt;
	INIT_QUERY;

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT "
		 "CASE WHEN user_id IS NULL THEN 0 ELSE 1 END, " // subscription
		 "owner_idnr, name, no_select, no_inferiors "
		 "FROM %smailboxes b LEFT OUTER JOIN %ssubscription s ON "
		 "b.mailbox_idnr = s.mailbox_id WHERE b.mailbox_idnr = %lu",
		 DBPFX, DBPFX, M->id);

	r = db_query(c, query);
	if (db_result_next(r)) {

		/* subsciption */
		M->is_subscribed = db_result_get_bool(r, i++);

		/* owner_idnr */
		M->owner_id = db_result_get_u64(r, i++);

		/* name */
		name = g_strdup(db_result_get(r,i++));
		if (MATCH(name, "INBOX")) {
			M->is_inbox = TRUE;
			M->is_subscribed = TRUE;
		}

		mbxname = mailbox_add_namespace(name, M->owner_id, M->owner_id);
		fqname = g_string_new(mbxname);
		fqname = g_string_truncate(fqname,IMAP_MAX_MAILBOX_NAMELEN);
		MailboxState_setName(M, fqname->str);
		g_string_free(fqname,TRUE);
		g_free(mbxname);

		/* no_select */
		M->no_select=db_result_get_bool(r,i++);

		/* no_inferior */
		M->no_inferiors=db_result_get_bool(r,i++);

		/* no_children search pattern*/
		pattern = g_strdup_printf("%s/%%", name);
		mailbox_like = mailbox_match_new(pattern);
		g_free(pattern);
		g_free(name);
	}

	db_con_clear(c);

	qs = g_string_new("");
	g_string_printf(qs, "SELECT COUNT(*) AS nr_children FROM %smailboxes WHERE owner_idnr = ? ", DBPFX);

	if (mailbox_like && mailbox_like->insensitive)
		g_string_append_printf(qs, "AND name %s ? ", db_get_sql(SQL_INSENSITIVE_LIKE));
	if (mailbox_like && mailbox_like->sensitive)
		g_string_append_printf(qs, "AND name %s ? ", db_get_sql(SQL_SENSITIVE_LIKE));

	stmt = db_stmt_prepare(c, qs->str);
	prml = 1;
	db_stmt_set_u64(stmt, prml++, M->owner_id);

	if (mailbox_like && mailbox_like->insensitive)
		db_stmt_set_str(stmt, prml++, mailbox_like->insensitive);
	if (mailbox_like && mailbox_like->sensitive)
		db_stmt_set_str(stmt, prml++, mailbox_like->sensitive);

	r = db_stmt_query(stmt);
	if (db_result_next(r)) {
		int nr_children = db_result_get_int(r,0);
		M->no_children=nr_children ? 0 : 1;
	} else {
		M->no_children=1;
	}

	mailbox_match_free(mailbox_like);
	g_string_free(qs, TRUE);
}

static void db_getmailbox_count(T M, Connection_T c)
{
	ResultSet_T r; 
	unsigned result[3];

	result[0] = result[1] = result[2] = 0;

	g_return_if_fail(M->id);

	/* count messages */
	r = db_query(c, "SELECT 0,COUNT(*) FROM %smessages WHERE mailbox_idnr=%lu "
			"AND (status < %d) UNION "
			"SELECT 1,COUNT(*) FROM %smessages WHERE mailbox_idnr=%lu "
			"AND (status < %d) AND seen_flag=1 UNION "
			"SELECT 2,COUNT(*) FROM %smessages WHERE mailbox_idnr=%lu "
			"AND (status < %d) AND recent_flag=1",
			DBPFX, M->id, MESSAGE_STATUS_DELETE, // MESSAGE_STATUS_NEW, MESSAGE_STATUS_SEEN,
			DBPFX, M->id, MESSAGE_STATUS_DELETE, // MESSAGE_STATUS_NEW, MESSAGE_STATUS_SEEN,
			DBPFX, M->id, MESSAGE_STATUS_DELETE); // MESSAGE_STATUS_NEW, MESSAGE_STATUS_SEEN);
	if (db_result_next(r))
		result[db_result_get_int(r,0)] = (unsigned)db_result_get_int(r,1);
	if (db_result_next(r))
		result[db_result_get_int(r,0)] = (unsigned)db_result_get_int(r,1);
	if (db_result_next(r))
		result[db_result_get_int(r,0)] = (unsigned)db_result_get_int(r,1);

	M->exists = result[0];
	M->unseen = result[0] - result[1];
	M->recent = result[2];
 
	TRACE(TRACE_DEBUG, "exists [%d] unseen [%d] recent [%d]", M->exists, M->unseen, M->recent);
	/* now determine the next message UID 
	 * NOTE:
	 * - expunged messages are selected as well in order to be able to restore them 
	 * - the next uit MUST NOT change unless messages are added to THIS mailbox
	 * */

	if (M->exists == 0) {
		M->uidnext = 1;
		return;
	}

	r = db_query(c, "SELECT MAX(message_idnr)+1 FROM %smessages WHERE mailbox_idnr=%lu",DBPFX, M->id);
	if (db_result_next(r))
		M->uidnext = db_result_get_u64(r,0);
	else
		M->uidnext = 1;
}

static void db_getmailbox_keywords(T M, Connection_T c)
{
	ResultSet_T r; 
	const char *key;

	r = db_query(c, "SELECT DISTINCT(keyword) FROM %skeywords k "
			"LEFT JOIN %smessages m ON k.message_idnr=m.message_idnr "
			"LEFT JOIN %smailboxes b ON m.mailbox_idnr=b.mailbox_idnr "
			"WHERE b.mailbox_idnr=%lu", DBPFX, DBPFX, DBPFX, M->id);

	while (db_result_next(r)) {
		key = g_strdup(db_result_get(r,0));
		g_tree_insert(M->keywords, (gpointer)key, (gpointer)key);
	}
}

static void db_getmailbox_seq(T M, Connection_T c)
{
	ResultSet_T r; 

	r = db_query(c, "SELECT name,seq FROM %smailboxes WHERE mailbox_idnr=%lu", DBPFX, M->id);
	if (db_result_next(r)) {
		if (! M->name)
			M->name = p_string_new(M->pool, db_result_get(r, 0));
		M->seq = db_result_get_u64(r,1);
		TRACE(TRACE_DEBUG,"id: [%lu] name: [%s] seq [%lu]", M->id, p_string_str(M->name), M->seq);
	} else {
		TRACE(TRACE_ERR,"Aii. No such mailbox mailbox_idnr: [%lu]", M->id);
	}
}

int MailboxState_info(T M)
{
	volatile int t = DM_SUCCESS;
	Connection_T c = db_con_get();
	TRY
		db_begin_transaction(c);
		db_getmailbox_info(M, c);
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_commit_transaction(c);
		db_con_close(c);
	END_TRY;

	return t;
}

static void state_load_metadata(T M, Connection_T c)
{
	uint64_t oldseq;

	g_return_if_fail(M->id);

	oldseq = M->seq;
	db_getmailbox_seq(M, c);
	if (M->uidnext && (M->seq == oldseq)) 
		return;

	db_getmailbox_permission(M, c);
	db_getmailbox_count(M, c);
	db_getmailbox_keywords(M, c);
	db_getmailbox_info(M, c);

	TRACE(TRACE_DEBUG, "[%s] exists [%d] recent [%d]", 
			p_string_str(M->name), M->exists, M->recent);
}

int MailboxState_count(T M)
{
	Connection_T c;
	volatile int t = DM_SUCCESS;

	c = db_con_get();
	TRY
		db_begin_transaction(c);
		db_getmailbox_count(M, c);
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_commit_transaction(c);
		db_con_close(c);
	END_TRY;

	return t;
}

char * MailboxState_flags(T M)
{
	char *s = NULL;
	GString *string = g_string_new("\\Seen \\Answered \\Deleted \\Flagged \\Draft");
	assert(M);

	if (M->keywords) {
		GList *k = g_tree_keys(M->keywords);
		GString *keywords = g_list_join(k," ");
		g_string_append_printf(string, " %s", keywords->str);
		g_string_free(keywords,TRUE);
		g_list_free(g_list_first(k));
	}

	s = string->str;
	g_string_free(string, FALSE);
	return g_strchomp(s);
}

int MailboxState_hasPermission(T M, uint64_t userid, const char *right_flag)
{
	Connection_T c; ResultSet_T r;
	volatile int result = FALSE;
	uint64_t owner_id, mboxid;

	mboxid = MailboxState_getId(M);

	TRACE(TRACE_DEBUG, "checking ACL [%s] for user [%lu] on mailbox [%lu]",
			right_flag, userid, mboxid);

	/* If we don't know who owns the mailbox, look it up. */
	owner_id = MailboxState_getOwner(M);
	if (! owner_id) {
		result = db_get_mailbox_owner(mboxid, &owner_id);
		MailboxState_setOwner(M, owner_id);
		if (! result > 0)
			return result;
	}

	if (owner_id == userid) {
		TRACE(TRACE_DEBUG, "mailbox [%lu] is owned by user [%lu], giving all rights",
				mboxid, userid);
		return 1;
	}

	result = FALSE;
	c = db_con_get();
	TRY
		r = db_query(c, "SELECT * FROM %sacl WHERE user_id = %lu AND mailbox_id = %lu AND %s = 1", DBPFX, userid, mboxid, right_flag);
		if (db_result_next(r))
			result = TRUE;
	CATCH(SQLException)
		LOG_SQLERROR;
		result = DM_EQUERY;
	FINALLY	
		db_con_close(c);
	END_TRY;

	return result;
}

int MailboxState_getAcl(T M, uint64_t userid, struct ACLMap *map)
{
	int i;
	volatile int t = DM_SUCCESS;
	gboolean gotrow = FALSE;
	uint64_t anyone;
	Connection_T c; ResultSet_T r; PreparedStatement_T s;

	g_return_val_if_fail(MailboxState_getId(M),DM_EGENERAL); 

	if (! (auth_user_exists(DBMAIL_ACL_ANYONE_USER, &anyone)))
		return DM_EQUERY;

	c = db_con_get();
	TRY
		s = db_stmt_prepare(c, "SELECT lookup_flag,read_flag,seen_flag,"
			"write_flag,insert_flag,post_flag,"
			"create_flag,delete_flag,deleted_flag,expunge_flag,administer_flag "
			"FROM %sacl "
			"WHERE mailbox_id = ? AND user_id = ?",DBPFX);
		db_stmt_set_u64(s, 1, MailboxState_getId(M));
		db_stmt_set_u64(s, 2, userid);
		r = db_stmt_query(s);
		if (! db_result_next(r)) {
			/* else check the 'anyone' user */
			db_stmt_set_u64(s, 2, anyone);
			r = db_stmt_query(s);
			if (db_result_next(r))
				gotrow = TRUE;
		} else {
			gotrow = TRUE;
		}

		if (gotrow) {
			i = 0;
			map->lookup_flag	= db_result_get_bool(r,i++);
			map->read_flag		= db_result_get_bool(r,i++);
			map->seen_flag		= db_result_get_bool(r,i++);
			map->write_flag		= db_result_get_bool(r,i++);
			map->insert_flag	= db_result_get_bool(r,i++);
			map->post_flag		= db_result_get_bool(r,i++);
			map->create_flag	= db_result_get_bool(r,i++);
			map->delete_flag	= db_result_get_bool(r,i++);
			map->deleted_flag	= db_result_get_bool(r,i++);
			map->expunge_flag	= db_result_get_bool(r,i++);
			map->administer_flag	= db_result_get_bool(r,i++);
		}
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}


static gboolean mailbox_build_recent(uint64_t *uid, MessageInfo *msginfo, T M)
{
	if (msginfo->flags[IMAP_FLAG_RECENT]) {
		uint64_t *copy = mempool_pop(M->pool, sizeof(uint64_t));
		*copy = *uid;
		g_tree_insert(M->recent_queue, copy, copy);
	}
	return FALSE;
}

int MailboxState_build_recent(T M)
{
        if (MailboxState_getPermission(M) == IMAPPERM_READWRITE && MailboxState_getMsginfo(M)) {
		GTree *info = MailboxState_getMsginfo(M);
		g_tree_foreach(info, (GTraverseFunc)mailbox_build_recent, M);
		TRACE(TRACE_DEBUG, "build list of [%d] [%d] recent messages...", 
				g_tree_nnodes(info), g_tree_nnodes(M->recent_queue));
	}

	return 0;
}

static int _update_recent(GList *slices)
{
	INIT_QUERY;
	Connection_T c;
	volatile int t = FALSE;

	if (! (slices = g_list_first(slices)))
		return t;

	c = db_con_get();
	TRY
		db_begin_transaction(c);
		while (slices) {
			db_exec(c, "UPDATE %smessages SET recent_flag = 0 WHERE message_idnr IN (%s) AND recent_flag = 1", DBPFX, (gchar *)slices->data);
			if (! g_list_next(slices)) break;
			slices = g_list_next(slices);
		}
		db_commit_transaction(c);
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
		db_rollback_transaction(c);
	FINALLY
		db_con_close(c);
		g_list_destroy(slices);
	END_TRY;

	return t;
}

int MailboxState_flush_recent(T M) 
{
	GList *recent;

	if (M && MailboxState_getPermission(M) != IMAPPERM_READWRITE) 
		return DM_SUCCESS;

	TRACE(TRACE_DEBUG,"flush [%d] recent messages", g_tree_nnodes(M->recent_queue));

	if (! g_tree_nnodes(M->recent_queue))
		return DM_SUCCESS;

	recent = g_tree_keys(M->recent_queue);

	_update_recent(g_list_slices_u64(recent,100));

	g_list_free(g_list_first(recent));

	if ( (M) && (MailboxState_getId(M)) )
		db_mailbox_seq_update(MailboxState_getId(M));

	return 0;
}

static gboolean mailbox_clear_recent(uint64_t *uid, MessageInfo *msginfo, T M)
{
	msginfo->flags[IMAP_FLAG_RECENT] = 0;
	gpointer value;
	gpointer orig_key;
	if (g_tree_lookup_extended(M->recent_queue, uid, &orig_key, &value)) {
		mempool_push(M->pool, orig_key, sizeof(uint64_t));
	}
	g_tree_remove(M->recent_queue, uid);
	return FALSE;
}

int MailboxState_clear_recent(T M)
{
        if (MailboxState_getPermission(M) == IMAPPERM_READWRITE && MailboxState_getMsginfo(M)) {
		GTree *info = MailboxState_getMsginfo(M);
		g_tree_foreach(info, (GTraverseFunc)mailbox_clear_recent, M);
	}

	return 0;
}

GList * MailboxState_message_flags(T M, MessageInfo *msginfo)
{
	GList *t, *sublist = NULL;
	int j;
	uint64_t uid = msginfo->uid;

	for (j = 0; j < IMAP_NFLAGS; j++) {
		if (msginfo->flags[j])
			sublist = g_list_append(sublist,g_strdup((gchar *)imap_flag_desc_escaped[j]));
	}
	if ((msginfo->flags[IMAP_FLAG_RECENT] == 0) && g_tree_lookup(M->recent_queue, &uid)) {
		TRACE(TRACE_DEBUG,"set \\recent flag");
		sublist = g_list_append(sublist, g_strdup((gchar *)imap_flag_desc_escaped[IMAP_FLAG_RECENT]));
	}

	t = g_list_first(msginfo->keywords);
	while (t) {
		if (MailboxState_hasKeyword(M, t->data))
			sublist = g_list_append(sublist, g_strdup((gchar *)t->data));
		if (! g_list_next(t)) break;
		t = g_list_next(t);
	}
	
	return sublist;
}

GTree * MailboxState_steal_recent(T M)
{
	GTree *recent_queue = M->recent_queue;
	M->recent_queue = NULL;
	return recent_queue;
}

int MailboxState_merge_recent(T M, GTree *recent_queue)
{
	g_tree_merge(M->recent_queue, recent_queue, IST_SUBSEARCH_OR);
	g_tree_foreach(recent_queue, (GTraverseFunc)_free_recent_queue, M);
	g_tree_destroy(recent_queue);
	M->recent = g_tree_nnodes(M->recent_queue);
	return 0;
}
