/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
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
 * \file authsql.c
 * \brief implements SQL authentication. Prototypes of these functions
 * can be found in auth.h . 
 */

#include "dbmail.h"
#define THIS_MODULE "auth"

extern DBParam_T db_params;
#define DBPFX db_params.pfx
#define P ConnectionPool_T
#define S PreparedStatement_T
#define R ResultSet_T
#define C Connection_T
#define U URL_T


int auth_connect()
{
	/* this function is only called after a connection has been made
	 * if, in the future this is not the case, db.h should export a 
	 * function that enables checking for the database connection
	 */
	return 0;
}

int auth_disconnect()
{
	return 0;
}

int auth_user_exists(const char *username, uint64_t * user_idnr)
{
	return db_user_exists(username, user_idnr);
}

GList * auth_get_known_users(void)
{
	GList * users = NULL;
	C c; R r; 

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT userid FROM %susers ORDER BY userid",DBPFX);
		while (db_result_next(r)) 
			users = g_list_append(users, g_strdup(db_result_get(r, 0)));
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY
		db_con_close(c);
	END_TRY;
	
	return users;
}

GList * auth_get_known_aliases(void)
{
	GList * aliases = NULL;
	C c; R r;

	c = db_con_get();
	TRY
		r = db_query(c,"SELECT alias FROM %saliases ORDER BY alias",DBPFX);
		while (db_result_next(r))
			aliases = g_list_append(aliases, g_strdup(db_result_get(r,0)));
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY
		db_con_close(c);
	END_TRY;
	
	return aliases;
}

int auth_getclientid(uint64_t user_idnr, uint64_t * client_idnr)
{
	assert(client_idnr != NULL);
	*client_idnr = 0;
	C c; R r; volatile int t = TRUE;

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT client_idnr FROM %susers WHERE user_idnr = %" PRIu64 "",DBPFX, user_idnr);
		if (db_result_next(r))
			*client_idnr = db_result_get_u64(r,0);
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

int auth_getmaxmailsize(uint64_t user_idnr, uint64_t * maxmail_size)
{
	assert(maxmail_size != NULL);
	*maxmail_size = 0;
	C c; R r; volatile int t = TRUE;
	
	c = db_con_get();
	TRY
		r = db_query(c, "SELECT maxmail_size FROM %susers WHERE user_idnr = %" PRIu64 "",DBPFX, user_idnr);
		if (db_result_next(r))
			*maxmail_size = db_result_get_u64(r,0);
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;
	
	return t;
}


char *auth_getencryption(uint64_t user_idnr)
{
	char *res = NULL;
	C c; R r;
	
	assert(user_idnr > 0);
	c = db_con_get();
	TRY
		r = db_query(c, "SELECT encryption_type FROM %susers WHERE user_idnr = %" PRIu64 "",DBPFX, user_idnr);
		if (db_result_next(r))
			res = g_strdup(db_result_get(r,0));
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY
		db_con_close(c);
	END_TRY;

	return res;
}


static GList *user_get_deliver_to(const char *username)
{
	INIT_QUERY;
	C c; R r; S s;
	GList *d = NULL;

	snprintf(query, DEF_QUERYSIZE-1,
		 "SELECT deliver_to FROM %saliases "
		 "WHERE lower(alias) = lower(?) "
		 "AND lower(alias) <> lower(deliver_to)",
		 DBPFX);
	
	c = db_con_get();
	TRY
		s = db_stmt_prepare(c, query);
		db_stmt_set_str(s, 1, username);

		r = db_stmt_query(s);
		while (db_result_next(r))
			d = g_list_prepend(d, g_strdup(db_result_get(r,0)));
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY
		db_con_close(c);
	END_TRY;

	return d;
}


int auth_check_user_ext(const char *username, GList **userids, GList **fwds, int checks)
{
	int occurences = 0;
	GList *d = NULL;
	char *endptr;
	uint64_t id, *uid;

	if (checks > 20) {
		TRACE(TRACE_ERR,"too many checks. Possible loop detected.");
		return 0;
	}

	TRACE(TRACE_DEBUG, "[%d] checking user [%s] in alias table", checks, username);

	d = user_get_deliver_to(username);

	if (! d) {
		if (checks == 0) {
			TRACE(TRACE_DEBUG, "user %s not in aliases table", username);
			return 0;
		}
		/* found the last one, this is the deliver to
		 * but checks needs to be bigger then 0 because
		 * else it could be the first query failure */
		id = strtoull(username, &endptr, 10);
		if (*endptr == 0) {
			TRACE(TRACE_DEBUG, "checking user [%lu] occurences [%d]", id, occurences);
			/* check user active */
			if (db_user_active(id)){
				/* numeric deliver-to --> this is a userid */
				uid = g_new0(uint64_t,1);
				*uid = id;
				*(GList **)userids = g_list_prepend(*(GList **)userids, uid);
				occurences=1;
				TRACE(TRACE_DEBUG, "adding [%s] to deliver_to address occurences [%d]", username, occurences);
			}else{
				TRACE(TRACE_DEBUG, "user [%s] is not active", username);
			}
		} else {
			*(GList **)fwds = g_list_prepend(*(GList **)fwds, g_strdup(username));
			TRACE(TRACE_DEBUG, "adding [%s] to deliver_to address occurences [%d]", username, occurences);
		}
		return occurences;
	} 
 
	while (d) {
		/* do a recursive search for deliver_to */
		char *deliver_to = (char *)d->data;
		TRACE(TRACE_DEBUG, "checking user %s to %s", username, deliver_to);
		
		occurences += auth_check_user_ext(deliver_to, userids, fwds, checks+1);

		if (! g_list_next(d)) break;
		d = g_list_next(d);
	}

	g_list_destroy(d);

	return occurences;
}

int auth_adduser(const char *username, const char *password, const char *enctype,
		 uint64_t clientid, uint64_t maxmail, uint64_t * user_idnr)
{
	*user_idnr=0; 
	return db_user_create(username, password, enctype, clientid, maxmail, user_idnr);
}


int auth_delete_user(const char *username)
{
	return db_user_delete(username);
}


int auth_change_username(uint64_t user_idnr, const char *new_name)
{
	return db_user_rename(user_idnr, new_name);
}

int auth_change_password(uint64_t user_idnr, const char *new_pass, const char *enctype)
{
	C c; S s; volatile int t = FALSE;
	const char *encoding = enctype?enctype:"";

	if (strlen(new_pass) > 128) {
		TRACE(TRACE_ERR, "new password length is insane");
		return -1;
	}

	c = db_con_get();
	TRY
		s = db_stmt_prepare(c, "UPDATE %susers SET passwd = ?, encryption_type = ? WHERE user_idnr=?", DBPFX);
		db_stmt_set_str(s, 1, new_pass);
		db_stmt_set_str(s, 2, encoding);
		db_stmt_set_u64(s, 3, user_idnr);
		db_stmt_exec(s);
		t = TRUE;
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

int auth_change_clientid(uint64_t user_idnr, uint64_t new_cid)
{
	return db_update("UPDATE %susers SET client_idnr = %" PRIu64 " WHERE user_idnr=%" PRIu64 "", DBPFX, new_cid, user_idnr);
}

int auth_change_mailboxsize(uint64_t user_idnr, uint64_t new_size)
{
	return db_change_mailboxsize(user_idnr, new_size);
}

#define CONSTNULL(a) ((! a) || (a && (! a[0])))

int auth_validate(ClientBase_T *ci, const char *username, const char *password, uint64_t * user_idnr)
{
	char real_username[DM_USERNAME_LEN];
	const char *tuser;
	int result;

	memset(real_username,0,sizeof(real_username));

	assert(user_idnr != NULL);
	*user_idnr = 0;

	tuser = username;
	if (CONSTNULL(tuser) || CONSTNULL(password)) {
		if (ci && ci->auth) { // CRAM-MD5
			tuser = (char *)Cram_getUsername(ci->auth);
		} else {
			TRACE(TRACE_DEBUG, "username or password is empty");
			return FALSE;
		}
	}

	/* the shared mailbox user should not log in! */
	if (strcmp(tuser, PUBLIC_FOLDER_USER) == 0) return 0;

	strncpy(real_username, tuser, DM_USERNAME_LEN-1);
	if (db_use_usermap()) {  /* use usermap */
		result = db_usermap_resolve(ci, tuser, real_username);
		if (result == DM_EGENERAL)
			return 0;
		if (result == DM_EQUERY)
			return DM_EQUERY;
	}
	
	/* lookup the user_idnr */
	if (! auth_user_exists(real_username, user_idnr)) 
		return FALSE;

	if (! db_user_active(*user_idnr))
		return FALSE;

	int valid = 0;
	if (! (valid = db_user_validate(ci, "passwd", user_idnr, password))) {
		if ((valid = db_user_validate(ci, "spasswd", user_idnr, password))) 
			db_user_security_trigger(*user_idnr);
	}
	if (! valid)
		*user_idnr = 0;

	return valid;

}

uint64_t auth_md5_validate(ClientBase_T *ci UNUSED, char *username,
		unsigned char *md5_apop_he, char *apop_stamp)
{
	/* returns useridnr on OK, 0 on validation failed, -1 on error */
	char checkstring[FIELDSIZE];
	char hash[FIELDSIZE];
	uint64_t user_idnr = 0;
	const char *dbpass;
	C c; R r;
	volatile int t = FALSE;

	/* lookup the user_idnr */
	if (! auth_user_exists(username, &user_idnr))
		return DM_EQUERY;

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT passwd FROM %susers WHERE user_idnr = %" PRIu64 "", DBPFX, user_idnr);
		if (db_result_next(r)) { /* user found */
			/* now authenticate using MD5 hash comparisation  */
			dbpass = db_result_get(r,0); /* value holds the password */

			TRACE(TRACE_DEBUG, "apop_stamp=[%s], userpw=[%s]", apop_stamp, dbpass);

			memset(hash, 0, sizeof(hash));
			memset(checkstring, 0, sizeof(checkstring));
			g_snprintf(checkstring, FIELDSIZE-1, "%s%s", apop_stamp, dbpass);
			dm_md5(checkstring, hash);

			TRACE(TRACE_DEBUG, "checkstring for md5 [%s] -> result [%s]", checkstring, hash);
			TRACE(TRACE_DEBUG, "validating md5_apop_we=[%s] md5_apop_he=[%s]", hash, md5_apop_he);

			if (strcmp((char *)md5_apop_he, hash) == 0) {
				TRACE(TRACE_NOTICE, "user [%s] is validated using APOP", username);
			} else {
				user_idnr = 0; // failed
			}
		} else {
			user_idnr = 0;
		}
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	if (t == DM_EQUERY) return t;

	if (user_idnr == 0)
		TRACE(TRACE_NOTICE, "user [%s] could not be validated", username);
	else
		db_user_log_login(user_idnr);

	return user_idnr;
}

char *auth_get_userid(uint64_t user_idnr)
{
	C c; R r;
	char *result = NULL;
	c = db_con_get();

	TRY
		r = db_query(c, "SELECT userid FROM %susers WHERE user_idnr = %" PRIu64 "", DBPFX, user_idnr);
		if (db_result_next(r))
			result = g_strdup(db_result_get(r,0));
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY
		db_con_close(c);
	END_TRY;

	return result;
}

/**
 * Check the user and return the error code
 * return 0 on no error, others if different
 */
int auth_check_userid(uint64_t user_idnr)
{
	C c; R r; volatile gboolean t = TRUE;

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT userid FROM %susers WHERE user_idnr = %" PRIu64 "", DBPFX, user_idnr);
		if (db_result_next(r)){
			/* user found */
			if (db_user_active(user_idnr)){
				/* user active, then no error */
				t = 0;	
			}
		}
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

int auth_addalias(uint64_t user_idnr, const char *alias, uint64_t clientid)
{
	C c; R r; S s; volatile int t = FALSE;
	INIT_QUERY;

	/* check if this alias already exists */
	snprintf(query, DEF_QUERYSIZE-1,
		 "SELECT alias_idnr FROM %saliases "
		 "WHERE lower(alias) = lower(?) AND deliver_to = ? "
		 "AND client_idnr = ?",DBPFX);

	c = db_con_get();
	TRY
		s = db_stmt_prepare(c,query);
		db_stmt_set_str(s, 1, alias);
		db_stmt_set_u64(s, 2, user_idnr);
		db_stmt_set_u64(s, 3, clientid);

		r = db_stmt_query(s);

		if (db_result_next(r)) {
			TRACE(TRACE_INFO, "alias [%s] for user [%" PRIu64 "] already exists", alias, user_idnr);
			t = TRUE;
		}
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	END_TRY;

	if (t) {
		db_con_close(c);
		return t;
	}

	db_con_clear(c);

	TRY
		s = db_stmt_prepare(c, "INSERT INTO %saliases (alias,deliver_to,client_idnr) VALUES (?,?,?)",DBPFX);
		db_stmt_set_str(s, 1, alias);
		db_stmt_set_u64(s, 2, user_idnr);
		db_stmt_set_u64(s, 3, clientid);

		db_stmt_exec(s);
		t = TRUE;
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

int auth_addalias_ext(const char *alias,
		    const char *deliver_to, uint64_t clientid)
{
	C c; R r; S s; 
	volatile int t = FALSE;
	INIT_QUERY;

	c = db_con_get();
	TRY
		/* check if this alias already exists */
		if (clientid != 0) {
			snprintf(query, DEF_QUERYSIZE-1,
				 "SELECT alias_idnr FROM %saliases "
				 "WHERE lower(alias) = lower(?) AND "
				 "lower(deliver_to) = lower(?) "
				 "AND client_idnr = ? ",DBPFX);

			s = db_stmt_prepare(c, query);
			db_stmt_set_str(s, 1, alias);
			db_stmt_set_str(s, 2, deliver_to);
			db_stmt_set_u64(s, 3, clientid);

		} else {
			snprintf(query, DEF_QUERYSIZE-1,
				 "SELECT alias_idnr FROM %saliases "
				 "WHERE lower(alias) = lower(?) "
				 "AND lower(deliver_to) = lower(?) ",DBPFX);

			s = db_stmt_prepare(c,query);
			db_stmt_set_str(s, 1, alias);
			db_stmt_set_str(s, 2, deliver_to);
		}

		r = db_stmt_query(s);
		if (db_result_next(r)) {
			TRACE(TRACE_INFO, "alias [%s] --> [%s] already exists", alias, deliver_to);
			t = TRUE;
		}
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	END_TRY;

	if (t) {
		db_con_close(c);
		return t;
	}

	db_con_clear(c);

	TRY
		s = db_stmt_prepare(c, "INSERT INTO %saliases (alias,deliver_to,client_idnr) VALUES (?,?,?)",DBPFX);
		db_stmt_set_str(s, 1, alias);
		db_stmt_set_str(s, 2, deliver_to);
		db_stmt_set_u64(s, 3, clientid);

		db_stmt_exec(s);
		t = TRUE;
	CATCH(SQLException)
		LOG_SQLERROR;
		t = DM_EQUERY;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

int auth_removealias(uint64_t user_idnr, const char *alias)
{
	C c; S s; volatile gboolean t = FALSE;
	
	c = db_con_get();
	TRY
		s = db_stmt_prepare(c, "DELETE FROM %saliases WHERE deliver_to=? AND lower(alias) = lower(?)",DBPFX);
		/* convert to string to string, due to the nature of deliver_to field */
		char user_idnr_str[12]; 
		sprintf(user_idnr_str, "%ld", user_idnr);
		db_stmt_set_str(s, 1, user_idnr_str);
		db_stmt_set_str(s, 2, alias);
		db_stmt_exec(s);
		t = TRUE;
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

int auth_removealias_ext(const char *alias, const char *deliver_to)
{
	C c; S s; volatile gboolean t = FALSE;

	c = db_con_get();
	TRY
		s = db_stmt_prepare(c, "DELETE FROM %saliases WHERE lower(deliver_to) = lower(?) AND lower(alias) = lower(?)", DBPFX);
		db_stmt_set_str(s, 1, deliver_to);
		db_stmt_set_str(s, 2, alias);
		db_stmt_exec(s);
		t = TRUE;
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY
		db_con_close(c);
	END_TRY;

	return t;
}

GList * auth_get_user_aliases(uint64_t user_idnr)
{
	C c; R r;
	GList *l = NULL;

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT alias FROM %saliases WHERE deliver_to = '%" PRIu64 "' "
				"UNION SELECT a2.alias FROM %saliases a1 JOIN %saliases a2 "
				"ON (a1.alias = a2.deliver_to) "
				"WHERE a1.deliver_to='%" PRIu64 "' AND a2.deliver_to IS NOT NULL "
				"ORDER BY alias DESC",
				DBPFX, user_idnr, DBPFX, DBPFX, user_idnr);
		while (db_result_next(r))
			l = g_list_prepend(l,g_strdup(db_result_get(r,0)));
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY
		db_con_close(c);
	END_TRY;

	return l;
}

GList * auth_get_aliases_ext(const char *alias)
{
	C c; R r;
	GList *l = NULL;

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT deliver_to FROM %saliases WHERE alias = '%s' ORDER BY alias DESC",DBPFX, alias);
		while (db_result_next(r))
			l = g_list_prepend(l,g_strdup(db_result_get(r,0)));
	CATCH(SQLException)
		LOG_SQLERROR;
	FINALLY
		db_con_close(c);
	END_TRY;

	return l;
}

gboolean auth_requires_shadow_user(void)
{
	return FALSE;
}

#undef P
#undef S
#undef R
#undef C
#undef U


