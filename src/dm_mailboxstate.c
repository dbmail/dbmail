/*
  
 Copyright (c) 2008 NFG Net Facilities Group BV support@nfg.nl

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
extern db_param_t _db_params;
#define DBPFX _db_params.pfx

#define T MailboxState_T

struct T {
	u64_t id;
	u64_t uidnext;
	u64_t owner_id;
	u64_t seq;
	//
	unsigned no_select;
	unsigned no_children;
	unsigned no_inferiors;
	unsigned exists;
	unsigned recent;
	unsigned unseen;
	unsigned permission;
	// 
	gboolean is_public;
	gboolean is_users;
	gboolean is_inbox;
	//
	char *name;
	GTree *keywords;
};

/* */
T MailboxState_new(u64_t id)
{
	T M;
	
	M = g_malloc0(sizeof(*M));
	M->id = id;
	M->keywords = g_tree_new_full((GCompareDataFunc)g_ascii_strcasecmp, NULL,(GDestroyNotify)g_free,NULL);

	return M;
}

void MailboxState_setId(T M, u64_t id)
{
	M->id = id;
}
u64_t MailboxState_getId(T M)
{
	return M->id;
}
u64_t MailboxState_getSeq(T M)
{
	return M->seq;
}

void MailboxState_setExists(T M, u64_t exists)
{
	M->exists = exists;
}

unsigned MailboxState_getExists(T M)
{
	return M->exists;
}

void MailboxState_setRecent(T M, u64_t recent)
{
	M->recent = recent;
}

unsigned MailboxState_getRecent(T M)
{
	return M->recent;
}

u64_t MailboxState_getUidnext(T M)
{
	return M->uidnext;
}
void MailboxState_setOwner(T M, u64_t owner_id)
{
	M->owner_id = owner_id;
}
u64_t MailboxState_getOwner(T M)
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
	char *old = M->name;
	M->name = g_strdup(name);
	if (old) {
		g_free(old);
		old = NULL;
	}
}
const char * MailboxState_getName(T M)
{
	return M->name;
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


/*
 * closes the msg cache
 */

void MailboxState_free(T *M)
{
	T s = *M;
	s->id = 0;
	if (s->keywords) {
		g_tree_destroy(s->keywords);
		s->keywords = NULL;
	}
	if (s->name) {
		g_free(s->name);
		s->name = NULL;
	}
	g_free(s);
	s = NULL;
}

static int db_getmailbox_flags(T M)
{
	C c; R r; int t = DM_SUCCESS;
	g_return_val_if_fail(M->id,DM_EQUERY);
	
	c = db_con_get();
	TRY
		r = db_query(c, "SELECT permission FROM %smailboxes WHERE mailbox_idnr = %llu",
				DBPFX, M->id);
		if (db_result_next(r))
			M->permission = db_result_get_int(r, 0);

	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

static int db_getmailbox_metadata(T M, u64_t user_idnr)
{
	/* query mailbox for LIST results */
	C c; R r; int t = DM_SUCCESS;
	char *mbxname, *name, *pattern;
	struct mailbox_match *mailbox_like = NULL;
	GString *fqname, *qs;
	int i=0, prml;
	S stmt;
	INIT_QUERY;

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT owner_idnr, name, no_select, no_inferiors "
		 "FROM %smailboxes WHERE mailbox_idnr = %llu",
		 DBPFX, M->id);

	c = db_con_get();
	TRY
		r = db_query(c, query);
		if (db_result_next(r)) {
			/* owner_idnr */
			M->owner_id = db_result_get_u64(r, i++);
			
			/* name */
			name = g_strdup(db_result_get(r,i++));
			if (! user_idnr) user_idnr = M->owner_id;

			mbxname = mailbox_add_namespace(name, M->owner_id, user_idnr);
			fqname = g_string_new(mbxname);
			fqname = g_string_truncate(fqname,IMAP_MAX_MAILBOX_NAMELEN);
			M->name = fqname->str;
			g_string_free(fqname,FALSE);
			g_free(mbxname);

			/* no_select */
			M->no_select=db_result_get_bool(r,i++);
			/* no_inferior */
			M->no_inferiors=db_result_get_bool(r,i++);
			
			/* no_children */
			pattern = g_strdup_printf("%s/%%", name);
			mailbox_like = mailbox_match_new(pattern);
			g_free(pattern);
			g_free(name);
		}
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	END_TRY;

	if (t == DM_EQUERY) {
		db_con_close(c);
		return t;
	}

	db_con_clear(c);

	qs = g_string_new("");
	g_string_printf(qs, "SELECT COUNT(*) AS nr_children FROM %smailboxes WHERE owner_idnr = ? ", DBPFX);

	if (mailbox_like && mailbox_like->insensitive)
		g_string_append_printf(qs, "AND name %s ? ", db_get_sql(SQL_INSENSITIVE_LIKE));
	if (mailbox_like && mailbox_like->sensitive)
		g_string_append_printf(qs, "AND name %s ? ", db_get_sql(SQL_SENSITIVE_LIKE));

	t = DM_SUCCESS;
	TRY
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
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	mailbox_match_free(mailbox_like);
	g_string_free(qs, TRUE);

	return t;
}

static int db_getmailbox_count(T M)
{
	C c; R r; 
	volatile int t;
	unsigned exists = 0, seen = 0, recent = 0;

	g_return_val_if_fail(M->id,DM_EQUERY);

	/* count messages */
 	t = FALSE;
	c = db_con_get();
	TRY
		r = db_query(c, "SELECT 'a',COUNT(*) FROM %smessages WHERE mailbox_idnr=%llu "
				"AND (status < %d) UNION "
				"SELECT 'b',COUNT(*) FROM %smessages WHERE mailbox_idnr=%llu "
				"AND (status < %d) AND seen_flag=1 UNION "
				"SELECT 'c',COUNT(*) FROM %smessages WHERE mailbox_idnr=%llu "
				"AND (status < %d) AND recent_flag=1", 
				DBPFX, M->id, MESSAGE_STATUS_DELETE, // MESSAGE_STATUS_NEW, MESSAGE_STATUS_SEEN,
				DBPFX, M->id, MESSAGE_STATUS_DELETE, // MESSAGE_STATUS_NEW, MESSAGE_STATUS_SEEN,
				DBPFX, M->id, MESSAGE_STATUS_DELETE); // MESSAGE_STATUS_NEW, MESSAGE_STATUS_SEEN);
		if (db_result_next(r)) exists = (unsigned)db_result_get_int(r,1);
		if (db_result_next(r)) seen   = (unsigned)db_result_get_int(r,1);
		if (db_result_next(r)) recent = (unsigned)db_result_get_int(r,1);
		db_con_clear(c);
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	END_TRY;

	if (t == DM_EQUERY) {
		db_con_close(c);
		return t;
	}

 	M->exists = exists;
 	M->unseen = exists - seen;
 	M->recent = recent;
 
	TRACE(TRACE_DEBUG, "exists [%d] unseen [%d] recent [%d]", M->exists, M->unseen, M->recent);
	/* now determine the next message UID 
	 * NOTE:
	 * - expunged messages are selected as well in order to be able to restore them 
	 * - the next uit MUST NOT change unless messages are added to THIS mailbox
	 * */

	db_con_clear(c);
	t = FALSE;
	TRY
		r = db_query(c, "SELECT MAX(message_idnr)+1 FROM %smessages WHERE mailbox_idnr=%llu",DBPFX, M->id);
		if (db_result_next(r))
			M->uidnext = db_result_get_u64(r,0);
		else
			M->uidnext = 1;
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

static int db_getmailbox_keywords(T M)
{
	C c; R r; 
	volatile int t = DM_SUCCESS;
	const char *key;

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT DISTINCT(keyword) FROM %skeywords k "
				"JOIN %smessages m ON k.message_idnr=m.message_idnr "
				"JOIN %smailboxes b ON m.mailbox_idnr=b.mailbox_idnr "
				"WHERE b.mailbox_idnr=%llu", DBPFX, DBPFX, DBPFX, M->id);

		while (db_result_next(r)) {
			key = db_result_get(r,0);
			g_tree_insert(M->keywords, (gpointer)g_strdup(key), (gpointer)key);
		}

	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
		if (M->keywords) g_tree_destroy(M->keywords);
	FINALLY
		db_con_close(c);
	END_TRY;

	if (t == DM_EQUERY) return t;

	return t;
}

static int db_getmailbox_seq(T M)
{
	C c; R r; 
	volatile int t = DM_SUCCESS;

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT name,seq FROM %smailboxes WHERE mailbox_idnr=%llu", DBPFX, M->id);
		if (db_result_next(r)) {
			if (! M->name)
				M->name = g_strdup(db_result_get(r, 0));
			M->seq = db_result_get_u64(r,1);
		} else {
			t = DM_EQUERY;
		}
	CATCH(SQLException)
		LOG_SQLERROR;
		M->seq = 0;
	FINALLY
		db_con_close(c);
	END_TRY;

	TRACE(TRACE_DEBUG,"seq [%llu]", M->seq);

	if (! M->name) return DM_EQUERY;

	return t;
}

int MailboxState_reload(T M, u64_t userid)
{
	int res;
	u64_t oldseq;
	
	g_return_val_if_fail(M->id,DM_EQUERY);

	oldseq = M->seq;
	
	if ((res = db_getmailbox_seq(M)) != DM_SUCCESS)
		return res;

	if ( M->uidnext && (M->seq == oldseq) )
		return DM_SUCCESS;

	if ((res = db_getmailbox_flags(M)) != DM_SUCCESS)
		return res;
	if ((res = db_getmailbox_count(M)) != DM_SUCCESS)
		return res;
	if ((res = db_getmailbox_keywords(M)) != DM_SUCCESS)
		return res;
	if ((res = db_getmailbox_metadata(M, userid)) != DM_SUCCESS)
		return res;

	return DM_SUCCESS;
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
	return s;
}

int db_acl_has_right(MailboxState_T M, u64_t userid, const char *right_flag)
{
	C c; R r;
	int result = FALSE;
	u64_t owner_id, mboxid;

	mboxid = MailboxState_getId(M);

	TRACE(TRACE_DEBUG, "checking ACL [%s] for user [%llu] on mailbox [%llu]",
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
		TRACE(TRACE_DEBUG, "mailbox [%llu] is owned by user [%llu], giving all rights",
				mboxid, userid);
		return 1;
	}

	result = FALSE;
	c = db_con_get();
	TRY
		r = db_query(c, "SELECT * FROM %sacl WHERE user_id = %llu AND mailbox_id = %llu AND %s = 1", DBPFX, userid, mboxid, right_flag);
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

int db_acl_get_acl_map(MailboxState_T M, u64_t userid, struct ACLMap *map)
{
	int i, t = DM_SUCCESS;
	gboolean gotrow = FALSE;
	u64_t anyone;
	C c; R r; S s;

	g_return_val_if_fail(MailboxState_getId(M),DM_EGENERAL); 

	if (! (auth_user_exists(DBMAIL_ACL_ANYONE_USER, &anyone)))
		return DM_EQUERY;

	c = db_con_get();
	TRY
		s = db_stmt_prepare(c, "SELECT lookup_flag,read_flag,seen_flag,"
			"write_flag,insert_flag,post_flag,"
			"create_flag,delete_flag,administer_flag "
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


