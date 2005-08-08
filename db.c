/* $Id$ */
/*
  Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl

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
 * \file db.c
 *
 * implement database functionality. This used to split out
 * between MySQL and PostgreSQL, but this is now integrated. 
 * Only the actual calls to the database APIs are still in
 * place in the mysql/ and pgsql/ directories
 */

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <sys/types.h>
#include <assert.h>
#include "db.h"
#include "dbmail.h"
#include "dbmail-message.h"
#include "auth.h"
#include "misc.h"

static const char *db_flag_desc[] = {
	"seen_flag",
	"answered_flag",
	"deleted_flag",
	"flagged_flag",
	"draft_flag",
	"recent_flag"
};

#define MAX_COLUMN_LEN 50
#define MAX_DATE_LEN 50

extern const char *TO_CHAR;
extern const char *TO_DATE;
extern const char *SQL_CURRENT_TIMESTAMP;
extern const char *SQL_REPLYCACHE_EXPIRE;

extern db_param_t _db_params;

#define DBPFX _db_params.pfx
/** list of tables used in dbmail */
#define DB_NTABLES 22
const char *DB_TABLENAMES[DB_NTABLES] = {
	"users", "aliases", "mailboxes",
	"messages", "physmessage", "messageblks",
	"acl", "subscription", "pbsp",
	"auto_notifications", "auto_replies",
	"headername", "headervalue",
	"subjectfield", "datefield", "referencesfield",
	"fromfield", "tofield", "replytofield",
	"ccfield", "replycache", "usermap"
};

/** can be used for making queries to db backend */
char query[DEF_QUERYSIZE]; 

/* size of buffer for writing messages to a client */
#define WRITE_BUFFER_SIZE 2048

/** static functions */
/** set quotum used for user user_idnr to curmail_size */
static int db_set_quotum_used(u64_t user_idnr, u64_t curmail_size);
/** add to quotum used */
static int db_add_quotum_used(u64_t user_idnr, u64_t add_size);
/** subtract from quotum used */
static int db_subtract_quotum_used(u64_t user_idnr, u64_t sub_size);
/** check if the message will fit within or exceed the quotum */
static int db_check_quotum_used(u64_t user_idnr, u64_t msg_size);

/** list all mailboxes owned by user owner_idnr */
static int db_list_mailboxes_by_regex(u64_t owner_idnr,
				      int only_subscribed, const char * pattern,
				      u64_t ** mailboxes,
				      unsigned int *nr_mailboxes);
/** get size of a message */
static int db_get_message_size(u64_t message_idnr, u64_t * message_size);
/** find a mailbox with a specific owner */
static int db_findmailbox_owner(const char *name, u64_t owner_idnr,
				u64_t * mailbox_idnr);
/** get the total size of messages in a mailbox. Does not work recursively! */
static int db_get_mailbox_size(u64_t mailbox_idnr, int only_deleted,
			       u64_t * mailbox_size);
/**
 * constructs a string for use in queries. This is used to not be dependent
 * on the date representations a database can handle. Unfortunately, MySQL
 * only implements a function to handle this in version > 4.1.1. PostgreSQL
 * implements the TO_DATE function, which handles this very well.
 */
static char *char2date_str(const char *date);

/**
 * check if the user_idnr is the same as that of the DBMAIL_DELIVERY_USERNAME
 * \param user_idnr user idnr to check
 * \return
 *     - -1 on error
 *     -  0 of different user
 *     -  1 if same user (user_idnr belongs to DBMAIL_DELIVERY_USERNAME
 */
static int user_idnr_is_delivery_user_idnr(u64_t user_idnr);

/**
 * set the first 5 characters of a mailbox name to "INBOX" if the name is
 * "inbox" or "inbox/somemailbox". This is used
 * when a mailbox with a name like "inbox" or "inbox/someMailbox" is used
 * to make sure the "inbox" part is always uppercase.
 * \param name name of the mailbox. strlen(name) must be bigger or equal
 *             strlen("INBOX")
 */
void convert_inbox_to_uppercase(char *name);

/*
 * check to make sure the database has been upgraded
 */
int db_check_version(void)
{
	snprintf(query, DEF_QUERYSIZE, "SELECT 1=1 FROM %sphysmessage LIMIT 1 OFFSET 0", DBPFX);
	if (db_query(query) == -1) {
		trace(TRACE_FATAL, "%s,%s: pre-2.0 database incompatible. You need to run the conversion script",
				__FILE__,__func__);
		return DM_EQUERY;
	}
	db_free_result();
	
	snprintf(query, DEF_QUERYSIZE, "SELECT 1=1 FROM %sheadervalue LIMIT 1 OFFSET 0", DBPFX);
	if (db_query(query) == -1) {
		trace(TRACE_FATAL, "%s,%s: 2.0 database incompatible. You need to add the header tables.",
				__FILE__,__func__);
		return DM_EQUERY;
	}
	db_free_result();
	
	return DM_SUCCESS;
}

/* test existence of usermap table */
int db_use_usermap(void)
{
	static int use_usermap = -1;
	if (use_usermap == -1) {
		snprintf(query, DEF_QUERYSIZE, "SELECT userid FROM %susermap WHERE 1 = 2",
				DBPFX);

		use_usermap = 0;
		
		if (db_query(query) != -1) {
			use_usermap = 1;
			db_free_result();
		}
		
		trace(TRACE_DEBUG, "%s,%s: %s usermap lookups",
				__FILE__, __func__,
				use_usermap ? "enabling" : "disabling" );
	}
	return use_usermap;
}
 

int db_begin_transaction()
{
	snprintf(query, DEF_QUERYSIZE, "BEGIN");
	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: error beginning transaction",
		      __FILE__, __func__);
		return DM_EQUERY;
	}
	return DM_SUCCESS;
}

int db_commit_transaction()
{
	snprintf(query, DEF_QUERYSIZE, "COMMIT");
	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: error committing transaction."
		      "Because we do not want to leave the database in "
		      "an inconsistent state, we will perform a rollback now",
		      __FILE__, __func__);
		db_rollback_transaction();
		return DM_EQUERY;
	}
	return DM_SUCCESS;
}

int db_rollback_transaction()
{
	snprintf(query, DEF_QUERYSIZE, "ROLLBACK");
	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: error rolling back transaction. "
		      "Disconnecting from database (this will implicitely "
		      "cause a Transaction Rollback.",
		      __FILE__, __func__);
		db_disconnect();
		/* and reconnect again */
		db_connect();
	}
	return DM_SUCCESS;
}

int db_get_physmessage_id(u64_t message_idnr, u64_t * physmessage_id)
{
	assert(physmessage_id != NULL);
	*physmessage_id = 0;

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT physmessage_id FROM %smessages "
		 "WHERE message_idnr = '%llu'", DBPFX, message_idnr);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: error getting physmessage_id",
		      __FILE__, __func__);
		return DM_EQUERY;
	}

	if (db_num_rows() < 1) {
		db_free_result();
		return DM_EGENERAL;
	}

	*physmessage_id = db_get_result_u64(0, 0);

	db_free_result();

	return DM_SUCCESS;
}


int db_get_quotum_used(u64_t user_idnr, u64_t * curmail_size)
{
	assert(curmail_size != NULL);

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT curmail_size FROM %susers "
		 "WHERE user_idnr = '%llu'", DBPFX, user_idnr);
	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: error getting used quotum for "
		      "user [%llu]", __FILE__, __func__, user_idnr);
		return DM_EQUERY;
	}

	*curmail_size = db_get_result_u64(0, 0);
	db_free_result();
	return DM_EGENERAL;
}

/* this is a local (static) function */
int db_set_quotum_used(u64_t user_idnr, u64_t curmail_size)
{
	snprintf(query, DEF_QUERYSIZE,
		 "UPDATE %susers SET curmail_size = '%llu' "
		 "WHERE user_idnr = '%llu'", DBPFX, curmail_size, user_idnr);
	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: error setting used quotum of "
		      "[%llu] for user [%llu]",
		      __FILE__, __func__, curmail_size, user_idnr);
		return DM_EQUERY;
	}
	return DM_SUCCESS;
}

int db_add_quotum_used(u64_t user_idnr, u64_t add_size)
{
	int result;
	trace(TRACE_DEBUG, "%s,%s: adding %llu to mailsize",
	      __FILE__, __func__, add_size);
	result = user_idnr_is_delivery_user_idnr(user_idnr);
	if (result < 0) {
		trace(TRACE_ERROR, "%s,%s: call to "
		      "user_idnr_is_delivery_user_idnr() failed",
		      __FILE__, __func__);
		return DM_EQUERY;
	}
	/* don't do anything if this DBMAIL_DELIVERY_USERNAME's user_idnr
	 * is given */
	if (result == 1) 
		return DM_SUCCESS;
		
	snprintf(query, DEF_QUERYSIZE,
		 "UPDATE %susers SET curmail_size = curmail_size + '%llu' "
		 "WHERE user_idnr = '%llu'", DBPFX, add_size, user_idnr);
	if (db_query(query) == DM_EQUERY) {
		trace(TRACE_ERROR, "%s,%s: error adding [%llu] to quotum "
		      "of user [%llu]", __FILE__, __func__,
		      add_size, user_idnr);
		return DM_EQUERY;
	}
	return DM_SUCCESS;
}

int db_subtract_quotum_used(u64_t user_idnr, u64_t sub_size)
{
	int result;

	trace(TRACE_DEBUG, "%s,%s: subtracting %llu from mailsize",
	      __FILE__, __func__, sub_size);
	result = user_idnr_is_delivery_user_idnr(user_idnr);
	if (result < 0) {
		trace(TRACE_ERROR, "%s,%s: call to "
		      "user_idnr_is_delivery_user_idnr() failed",
		      __FILE__, __func__);
		return DM_EQUERY;
	}
	/* don't do anything if this DBMAIL_DELIVERY_USERNAME's user_idnr
	 * is given */
	if (result == 1) 
		return DM_SUCCESS;

	snprintf(query, DEF_QUERYSIZE,
		 "UPDATE %susers SET curmail_size = curmail_size - '%llu' "
		 "WHERE user_idnr = '%llu'", DBPFX, sub_size, user_idnr);
	if (db_query(query) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: error subtracting [%llu] from quotum "
		      "of user [%llu]", __FILE__, __func__, sub_size,
		      user_idnr);
		return DM_EQUERY;
	}
	return DM_SUCCESS;
}

int db_check_quotum_used(u64_t user_idnr, u64_t msg_size)
{
	snprintf(query, DEF_QUERYSIZE,
		 "SELECT 1 FROM %susers "
		 "WHERE user_idnr = '%llu' "
		 "AND (maxmail_size > 0) "
		 "AND (curmail_size + '%llu' > maxmail_size)",
		 DBPFX, user_idnr, msg_size);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: error checking quotum for "
		      "user [%llu]", __FILE__, __func__, user_idnr);
		return DM_EQUERY;
	}

	/* If there is a quotum defined, and the inequality is true,
	 * then the message would therefore exceed the quotum,
	 * and so the function returns non-zero. */
	if (db_num_rows() > 0) {
		db_free_result();
		return DM_EGENERAL;
	}
	db_free_result();
	return DM_SUCCESS;
}

int db_calculate_quotum_all()
{
	u64_t *user_idnrs;
			/**< will hold all user_idnr for which the quotum
			   has to be set again */
	u64_t *curmail_sizes;
			   /**< will hold current mailsizes */
	int i;
	int n;
	    /**< number of records returned */
	int result;

	/* the following query looks really weird, with its 
	 * NOT (... IS NOT NULL), but it must be like this, because
	 * the normal query with IS NULL does not work on MySQL
	 * for some reason.
	 */
	snprintf(query, DEF_QUERYSIZE,
		 "SELECT usr.user_idnr, sum(pm.messagesize), usr.curmail_size "
		 "FROM %susers usr LEFT JOIN %smailboxes mbx "
		 "ON mbx.owner_idnr = usr.user_idnr "
		 "LEFT JOIN %smessages msg "
		 "ON msg.mailbox_idnr = mbx.mailbox_idnr "
		 "LEFT JOIN %sphysmessage pm "
		 "ON pm.id = msg.physmessage_id "
		 "AND msg.status < '%d' "
		 "GROUP BY usr.user_idnr, usr.curmail_size "
		 "HAVING ((SUM(pm.messagesize) <> usr.curmail_size) OR "
		 "(NOT (SUM(pm.messagesize) IS NOT NULL) "
		 "AND usr.curmail_size <> 0))", DBPFX,DBPFX,
			DBPFX,DBPFX,MESSAGE_STATUS_DELETE);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: error findng quotum used",
		      __FILE__, __func__);
		return DM_EQUERY;
	}

	n = db_num_rows();
	result = n;
	if (n == 0) {
		trace(TRACE_DEBUG, "%s,%s: quotum is already up to date",
		      __FILE__, __func__);
		return DM_SUCCESS;
	}

	if (!(user_idnrs = (u64_t *) dm_malloc(n * sizeof(u64_t)))) {
		trace(TRACE_ERROR,
		      "%s,%s: malloc failed. Probably out of memory..",
		      __FILE__, __func__);
		return -2;
	}
	if (!(curmail_sizes = (u64_t *) dm_malloc(n * sizeof(u64_t)))) {
		trace(TRACE_ERROR,
		      "%s,%s: malloc failed: Probably out of memort..",
		      __FILE__, __func__);
		dm_free(user_idnrs);
		return -2;
	}
	memset(user_idnrs, 0, n * sizeof(u64_t));
	memset(curmail_sizes, 0, n * sizeof(u64_t));

	for (i = 0; i < n; i++) {
		user_idnrs[i] = db_get_result_u64(i, 0);
		curmail_sizes[i] = db_get_result_u64(i, 1);
	}
	db_free_result();

	/* now update the used quotum for all users that need to be updated */
	for (i = 0; i < n; i++) {
		if (db_set_quotum_used(user_idnrs[i], curmail_sizes[i]) ==
		    -1) {
			trace(TRACE_ERROR,
			      "%s,%s: error setting quotum used, "
			      "trying to continue", __FILE__,
			      __func__);
			result = -1;
		}
	}

	/* free allocated memory */
	dm_free(user_idnrs);
	dm_free(curmail_sizes);
	return result;
}


int db_calculate_quotum_used(u64_t user_idnr)
{
	u64_t quotum = 0;

	snprintf(query, DEF_QUERYSIZE, "SELECT SUM(pm.messagesize) "
		 "FROM %sphysmessage pm, %smessages m, %smailboxes mb "
		 "WHERE m.physmessage_id = pm.id "
		 "AND m.mailbox_idnr = mb.mailbox_idnr "
		 "AND mb.owner_idnr = '%llu' " "AND m.status < '%d'",
		 DBPFX,DBPFX,DBPFX,user_idnr, MESSAGE_STATUS_DELETE);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not execute query",
		      __FILE__, __func__);
		return DM_EQUERY;
	}
	if (db_num_rows() < 1)
		trace(TRACE_WARNING, "%s,%s: SUM did not give result, "
		      "assuming empty mailbox", __FILE__, __func__);
	else {
		quotum = db_get_result_u64(0, 0);
	}
	db_free_result();
	trace(TRACE_DEBUG, "%s, found quotum usage of [%llu] bytes",
	      __func__, quotum);
	/* now insert the used quotum into the users table */
	if (db_set_quotum_used(user_idnr, quotum) == -1) {
		if (db_query(query) == -1) {
			trace(TRACE_ERROR,
			      "%s,%s: error setting quotum for user [%llu]",
			      __FILE__, __func__, user_idnr);
			return DM_EQUERY;
		}
	}
	return DM_SUCCESS;
}

int db_get_sievescript_byname(u64_t user_idnr, char *scriptname, char **script)
{
	const char *query_result = NULL;
	snprintf(query, DEF_QUERYSIZE,
				"SELECT script from %ssievescripts where "
				"owner_idnr = %llu' and name = '%s'",
				DBPFX,user_idnr, scriptname);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: error getting sievescript by name",
				__FILE__, __func__);
		return DM_EQUERY;
	}

	if (db_num_rows() < 1) {
		db_free_result();
		*script = NULL;
		return DM_SUCCESS;
	}

	query_result = db_get_result(0, 0);

	if (!query_result) {
		db_free_result();
		*script = NULL;
		return DM_EQUERY;
	}

	*script = dm_strdup(query_result);
	db_free_result();

	return DM_SUCCESS;
}

int db_get_sievescript_active(u64_t user_idnr, char **scriptname)
{
	int n;
	snprintf(query, DEF_QUERYSIZE,
		"SELECT name from %ssievescripts where "
		"owner_idnr = %llu and name = '%s' and active = 1",
		DBPFX, user_idnr, *scriptname);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, 
		"%s,%s: error getting active sievescript by name",
		__FILE__, __func__);
		return DM_EQUERY;
	}
	n = db_num_rows();
	db_free_result();
	return n;
}

int db_get_sievescript_listall(u64_t user_idnr, struct dm_list *scriptlist)
{
	int i,n;
	struct ssinfo *info;
	dm_list_init(scriptlist);
	snprintf(query, DEF_QUERYSIZE,
		"SELECT name,active from %ssievescripts where "
		"owner_idnr = %llu",
		DBPFX,user_idnr);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR,
		"%s,%s: error getting all sievescripts",
		__FILE__, __func__);
		return DM_EQUERY;
	}

	i = 0;
	n = db_num_rows();

	while(i < n) {
		info = (struct ssinfo *)dm_malloc(sizeof(struct ssinfo));
		info->name = dm_strdup(db_get_result(i, 0));   
		info->active = (int)db_get_result(i, 1);
		dm_list_nodeadd(scriptlist,info,sizeof(struct ssinfo));	
		i++;
	}

	db_free_result();
	return DM_SUCCESS;
}

int db_replace_sievescript(u64_t user_idnr, char *scriptname, char *script)
{
	snprintf(query, DEF_QUERYSIZE,
		"UPDATE %ssievescripts set script = '%s' "
		"where owner_idnr = %llu and name = '%s'",
		DBPFX,script,user_idnr,scriptname);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: error replacing sievescript '%s' "
			"for user_idnr [%llu]", __FILE__, __func__,
			scriptname, user_idnr);
		return DM_EQUERY;
	}

	return DM_SUCCESS;
}

int db_add_sievescript(u64_t user_idnr, char *scriptname, char *script)
{
	snprintf(query, DEF_QUERYSIZE,
		"INSERT into %ssievescripts values (%llu,'%s','%s',0)",
		DBPFX,user_idnr,scriptname,script);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: error adding sievescript '%s' "
		"for user_idnr [%llu]", __FILE__, __func__,
		scriptname, user_idnr);
		return DM_EQUERY;
	}

	return DM_SUCCESS;
}

int db_deactivate_sievescript(u64_t user_idnr, char *scriptname)
{
	snprintf(query, DEF_QUERYSIZE,
		"UPDATE %ssievescripts set active = 0 "
		"where owner_idnr = %llu and name = '%s'",
		DBPFX,user_idnr,scriptname);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: error deactivating sievescript '%s' "
		"for user_idnr [%llu]", __FILE__, __func__,
		scriptname, user_idnr);
		return DM_EQUERY;
	}

	return DM_SUCCESS;
}

int db_activate_sievescript(u64_t user_idnr, char *scriptname)
{
	snprintf(query, DEF_QUERYSIZE,
		"UPDATE %ssievescripts set active = 1 "
		"where owner_idnr = %llu and name = '%s'",
		DBPFX,user_idnr,scriptname);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: error activating sievescript '%s' "
		"for user_idnr [%llu]", __FILE__, __func__,
		scriptname, user_idnr);
		return DM_EQUERY;
	}

	return DM_SUCCESS;
}

int db_delete_sievescript(u64_t user_idnr, char *scriptname)
{
	snprintf(query, DEF_QUERYSIZE,
		"DELETE from %ssievescripts "
		"where owner_idnr = %llu and name = '%s'",
		DBPFX,user_idnr,scriptname);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: error deleting sievescript '%s' "
			"for user_idnr [%llu]", __FILE__, __func__,
			scriptname, user_idnr);
		return DM_EQUERY;
	}

	return DM_SUCCESS;
}

int db_check_sievescript_quota(u64_t user_idnr, u64_t scriptlen)
{
	/* TODO function db_check_sievescript_quota */
	trace(TRACE_DEBUG, "%s,%s: updating %llu sievescript quota with %llu",
		__FILE__, __func__, user_idnr, scriptlen);
	return DM_SUCCESS;
}

int db_set_sievescript_quota(u64_t user_idnr, u64_t quotasize)
{
	/* TODO function db_set_sievescript_quota */
	trace(TRACE_DEBUG, "%s,%s: setting %llu sievescript quota with %llu",
		__FILE__, __func__, user_idnr, quotasize);
	return DM_SUCCESS;
}

int db_get_sievescript_quota(u64_t user_idnr, u64_t * quotasize)
{
	/* TODO function db_get_sievescript_quota */
	trace(TRACE_DEBUG, "%s,%s: getting sievescript quota for %llu",
		__FILE__, __func__, user_idnr);
	*quotasize = 0;
	return DM_SUCCESS;
}

int db_get_notify_address(u64_t user_idnr, char **notify_address)
{
	const char *query_result = NULL;

	assert(notify_address != NULL);
	*notify_address = NULL;

	snprintf(query, DEF_QUERYSIZE, "SELECT notify_address "
		 "FROM %sauto_notifications WHERE user_idnr = %llu",
		 DBPFX,user_idnr);

	if (db_query(query) == -1) {
		/* query failed */
		trace(TRACE_ERROR, "%s,%s: query failed", __FILE__,
		      __func__);
		return DM_EQUERY;
	}
	if (db_num_rows() > 0) {
		query_result = db_get_result(0, 0);
		if (query_result && strlen(query_result) > 0) {
			*notify_address = dm_strdup(query_result);
			trace(TRACE_DEBUG, "%s,%s: found address [%s]",
			      __FILE__, __func__, *notify_address);
		}
	}

	db_free_result();
	return DM_SUCCESS;
}

int db_get_reply_body(u64_t user_idnr, char **reply_body)
{
	const char *query_result;
	*reply_body = NULL;

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT reply_body FROM %sauto_replies "
		 "WHERE user_idnr = %llu "
		 "AND (start_date IS NULL OR start_date <= NOW()) "
		 "AND (stop_date IS NULL OR stop_date >= NOW())", DBPFX,user_idnr);
	
	if (db_query(query) == -1) {
		/* query failed */
		trace(TRACE_ERROR, "%s,%s: query failed", __FILE__,
		      __func__);
		return DM_EQUERY;
	}
	if (db_num_rows() > 0) {
		query_result = db_get_result(0, 0);
		if (query_result && strlen(query_result) > 0) {
			*reply_body = dm_strdup(query_result);
			trace(TRACE_DEBUG, "%s,%s: found reply_body [%s]",
			      __FILE__, __func__, *reply_body);
		}
	}
	db_free_result();
	return DM_SUCCESS;
}

u64_t db_get_mailbox_from_message(u64_t message_idnr)
{
	u64_t mailbox_idnr;

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT mailbox_idnr FROM %smessages "
		 "WHERE message_idnr = '%llu'", DBPFX,message_idnr);

	if (db_query(query) == -1) {
		/* query failed */
		trace(TRACE_ERROR, "%s,%s: query failed", __FILE__,
		      __func__);
		return DM_EQUERY;
	}

	if (db_num_rows() < 1) {
		trace(TRACE_DEBUG, "%s,%s: No mailbox found for message",
		      __FILE__, __func__);
		db_free_result();
		return DM_SUCCESS;
	}
	mailbox_idnr = db_get_result_u64(0, 0);
	db_free_result();
	return mailbox_idnr;
}

u64_t db_get_useridnr(u64_t message_idnr)
{
	const char *query_result;
	u64_t user_idnr;

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT %smailboxes.owner_idnr FROM %smailboxes, %smessages "
		 "WHERE %smailboxes.mailbox_idnr = %smessages.mailbox_idnr "
		 "AND %smessages.message_idnr = '%llu'", DBPFX,DBPFX,DBPFX,
		DBPFX,DBPFX,DBPFX,message_idnr);
	if (db_query(query) == -1) {
		/* query failed */
		trace(TRACE_ERROR, "%s,%s: query failed", __FILE__,
		      __func__);
		return DM_EQUERY;
	}

	if (db_num_rows() < 1) {
		trace(TRACE_DEBUG, "%s,%s: No owner found for message",
		      __FILE__, __func__);
		db_free_result();
		return DM_SUCCESS;
	}
	query_result = db_get_result(0, 0);
	user_idnr = db_get_result_u64(0, 0);
	db_free_result();
	return user_idnr;
}

int db_insert_physmessage_with_internal_date(timestring_t internal_date,
					     u64_t * physmessage_id)
{
	char *to_date_str;
	assert(physmessage_id != NULL);

	*physmessage_id = 0;
	to_date_str = char2date_str(internal_date);

	snprintf(query, DEF_QUERYSIZE,
		 "INSERT INTO %sphysmessage (messagesize, internal_date) "
		 "VALUES ('0', %s)", DBPFX,to_date_str);
	dm_free(to_date_str);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: insertion of physmessage failed", __FILE__,
		      __func__);
		return DM_EQUERY;
	}
	*physmessage_id = db_insert_result("physmessage_id");

	return DM_EGENERAL;
}

int db_insert_physmessage(u64_t * physmessage_id)
{
	assert(physmessage_id != NULL);

	*physmessage_id = 0;

	snprintf(query, DEF_QUERYSIZE,
		 "INSERT INTO %sphysmessage (messagesize, internal_date) "
		 "VALUES ('0', %s)", DBPFX, SQL_CURRENT_TIMESTAMP);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: query failed", __FILE__,
		      __func__);
		return DM_EQUERY;
	}

	*physmessage_id = db_insert_result("physmessage_id");

	return DM_EGENERAL;
}

int db_message_set_unique_id(u64_t message_idnr, const char *unique_id)
{
	assert(unique_id);
	
	snprintf(query, DEF_QUERYSIZE,
		 "UPDATE %smessages SET unique_id = '%s', status = '%d' "
		 "WHERE message_idnr = '%llu'", DBPFX, unique_id, MESSAGE_STATUS_NEW,
		 message_idnr);
	if (db_query(query) == DM_EQUERY) {
		trace(TRACE_ERROR, "%s,%s: setting unique id for message "
		      "[%llu] failed", __FILE__, __func__,
		      message_idnr);
		return DM_EQUERY;
	}
	return DM_SUCCESS;
}

int db_physmessage_set_sizes(u64_t physmessage_id, u64_t message_size,
			     u64_t rfc_size)
{
	snprintf(query, DEF_QUERYSIZE,
		 "UPDATE %sphysmessage SET "
		 "messagesize = '%llu', rfcsize = '%llu' "
		 "WHERE id = '%llu'", DBPFX, message_size, rfc_size, physmessage_id);

	if (db_query(query) < 0) {
		trace(TRACE_ERROR, "%s,%s: error setting messagesize and "
		      "rfcsize for physmessage [%llu]",
		      __FILE__, __func__, physmessage_id);
		return DM_EQUERY;
	}
	return DM_SUCCESS;
}

int db_update_message(u64_t message_idnr, const char *unique_id,
		      u64_t message_size, u64_t rfc_size)
{
	assert(unique_id);
	u64_t physmessage_id = 0;

	if (db_message_set_unique_id(message_idnr, unique_id)) {
		trace(TRACE_STOP, "%s,%s: FATAL: db_message_set_unique_ id failed for [%llu]", 
				__FILE__, __func__,
				message_idnr);
	}

	/* update the fields in the physmessage table */
	if (db_get_physmessage_id(message_idnr, &physmessage_id)) 
		return DM_EQUERY;

	if (db_physmessage_set_sizes(physmessage_id, message_size, rfc_size)) 
		return DM_EQUERY;

	if (db_add_quotum_used(db_get_useridnr(message_idnr), message_size)) {
		trace(TRACE_ERROR,
		      "%s,%s: error calculating quotum "
		      "used for user [%llu]. Database might be "
		      "inconsistent. run dbmail-util", __FILE__,
		      __func__, db_get_useridnr(message_idnr));
		return DM_EQUERY;
	}
	return DM_SUCCESS;
}

int db_insert_message_block_physmessage(const char *block,
					u64_t block_size,
					u64_t physmessage_id,
					u64_t * messageblk_idnr,
					unsigned is_header)
{
	char *escaped_query = NULL;
	unsigned maxesclen = (READ_BLOCK_SIZE + 1) * 2 + DEF_QUERYSIZE;
	unsigned startlen = 0;
	unsigned esclen = 0;

	assert(messageblk_idnr != NULL);
	*messageblk_idnr = 0;

	if (block == NULL) {
		trace(TRACE_ERROR, "%s,%s: got NULL as block. Insertion not possible",
		      __FILE__, __func__);
		return DM_EQUERY;
	}

	if (block_size > READ_BLOCK_SIZE) {
		trace(TRACE_ERROR, "%s,%s: blocksize [%llu], maximum is [%ld]",
		      __FILE__, __func__, block_size, READ_BLOCK_SIZE);
		return DM_EQUERY;
	}

	escaped_query = (char *) dm_malloc(sizeof(char) * maxesclen);
	if (!escaped_query) {
		trace(TRACE_ERROR, "%s,%s: not enough memory", 
				__FILE__, __func__);
		return DM_EQUERY;
	}
	memset(escaped_query, '\0', maxesclen);
	startlen = snprintf(escaped_query, maxesclen,
		     "INSERT INTO %smessageblks"
		     "(is_header, messageblk,blocksize, physmessage_id)"
		     "VALUES ('%u','",DBPFX, is_header);
	
	/* escape & add data */
	esclen = db_escape_string(&escaped_query[startlen], block, block_size);

	snprintf(&escaped_query[esclen + startlen],
		 maxesclen - esclen - startlen, "', '%llu', '%llu')",
		 block_size, physmessage_id);

	if (db_query(escaped_query) == DM_EQUERY) {
		dm_free(escaped_query);
		return DM_EQUERY;
	}

	/* all done, clean up & exit */
	dm_free(escaped_query);

	*messageblk_idnr = db_insert_result("messageblk_idnr");
	return DM_SUCCESS;
}

int db_insert_message_block(const char *block, u64_t block_size,
			    u64_t message_idnr, u64_t * messageblk_idnr, unsigned is_header)
{
	u64_t physmessage_id;

	assert(messageblk_idnr != NULL);
	*messageblk_idnr = 0;
	if (block == NULL) {
		trace(TRACE_ERROR,
		      "%s,%s: got NULL as block, insertion not possible\n",
		      __FILE__, __func__);
		return DM_EQUERY;
	}

	if (db_get_physmessage_id(message_idnr, &physmessage_id) == DM_EQUERY) {
		trace(TRACE_ERROR, "%s,%s: error getting physmessage_id",
		      __FILE__, __func__);
		return DM_EQUERY;
	}

	if (db_insert_message_block_physmessage
	    (block, block_size, physmessage_id, messageblk_idnr, is_header) < 0) {
		trace(TRACE_ERROR,
		      "%s,%s: error inserting messageblks for "
		      "physmessage [%llu]", __FILE__, __func__,
		      physmessage_id);
		return DM_EQUERY;
	}
	return DM_SUCCESS;
}

int db_log_ip(const char *ip)
{
	u64_t id = 0;

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT idnr FROM %spbsp WHERE ipnumber = '%s'", DBPFX, ip);
	if (db_query(query) == DM_EQUERY) {
		trace(TRACE_ERROR, "%s,%s: could not access ip-log table "
		      "(pop/imap-before-smtp): %s", __FILE__, __func__,
		      ip);
		return DM_EQUERY;
	}

	id = db_get_result_u64(0, 0);

	db_free_result();

	if (id) {
		/* this IP is already in the table, update the 'since' field */
		snprintf(query, DEF_QUERYSIZE, "UPDATE %spbsp "
			 "SET since = %s WHERE idnr='%llu'",
			 DBPFX, SQL_CURRENT_TIMESTAMP, id);

		if (db_query(query) == DM_EQUERY) {
			trace(TRACE_ERROR,
			      "%s,%s: could not update ip-log "
			      "(pop/imap-before-smtp)", __FILE__,
			      __func__);
			return DM_EQUERY;
		}
	} else {
		/* IP not in table, insert row */
		snprintf(query, DEF_QUERYSIZE,
			 "INSERT INTO %spbsp (since, ipnumber) "
			 "VALUES (%s, '%s')", DBPFX, SQL_CURRENT_TIMESTAMP, ip);
		if (db_query(query) == DM_EQUERY) {
			trace(TRACE_ERROR,
			      "%s,%s: could not log IP number to dbase "
			      "(pop/imap-before-smtp)", __FILE__,
			      __func__);
			return DM_EQUERY;
		}
	}

	trace(TRACE_DEBUG, "%s,%s: ip [%s] logged\n", __FILE__,
	      __func__, ip);

	return DM_SUCCESS;
}

int db_count_iplog(const char *lasttokeep, u64_t *affected_rows)
{
	char *escaped_lasttokeep = (char *)dm_malloc(2*strlen(lasttokeep)+1);

	assert(affected_rows != NULL);
	*affected_rows = 0;

	if (db_escape_string(escaped_lasttokeep, lasttokeep, strlen(lasttokeep))) {
		trace(TRACE_ERROR, "%s,%s: error escaping last to keep.",
			__FILE__, __func__);
		return DM_EQUERY;
	}
	snprintf(query, DEF_QUERYSIZE,
		 "SELECT * FROM %spbsp WHERE since < '%s'", DBPFX, escaped_lasttokeep);
	dm_free(escaped_lasttokeep);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s:%s: error executing query",
		      __FILE__, __func__);
		return DM_EQUERY;
	}
	*affected_rows = db_get_affected_rows();

	return DM_SUCCESS;
}

int db_cleanup_iplog(const char *lasttokeep, u64_t *affected_rows)
{
 	assert(affected_rows != NULL);
 	*affected_rows = 0;

	snprintf(query, DEF_QUERYSIZE,
		 "DELETE FROM %spbsp WHERE since < '%s'", DBPFX, lasttokeep);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s:%s: error executing query",
		      __FILE__, __func__);
		return DM_EQUERY;
	}
	*affected_rows = db_get_affected_rows();

	return DM_SUCCESS;
}

int db_cleanup()
{
	return db_do_cleanup(DB_TABLENAMES, DB_NTABLES);
}

int db_empty_mailbox(u64_t user_idnr)
{
	u64_t *mboxids = NULL;
	unsigned n, i;
	int result = 0;

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT mailbox_idnr FROM %smailboxes WHERE owner_idnr='%llu'",
		 DBPFX, user_idnr);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: error executing query",
		      __FILE__, __func__);
		return DM_EQUERY;
	}
	n = db_num_rows();
	if (n == 0) {
		db_free_result();
		trace(TRACE_WARNING,
		      "%s,%s: user [%llu] does not have any mailboxes?",
		      __FILE__, __func__, user_idnr);
		return DM_SUCCESS;
	}

	mboxids = (u64_t *) dm_malloc(n * sizeof(u64_t));
	if (!mboxids) {
		trace(TRACE_ERROR, "%s,%s: not enough memory",
		      __FILE__, __func__);
		db_free_result();
		return -2;
	}
	memset(mboxids, 0, n * sizeof(u64_t));

	for (i = 0; i < n; i++) {
		mboxids[i] = db_get_result_u64(i, 0);
	}
	db_free_result();

	for (i = 0; i < n; i++) {
		if (db_delete_mailbox(mboxids[i], 1, 1) == -1) {
			trace(TRACE_ERROR,
			      "%s,%s: error emptying mailbox [%llu]",
			      __FILE__, __func__, mboxids[i]);
			result = -1;
		}
	}
	dm_free(mboxids);
	return result;
}

int db_icheck_messageblks(struct dm_list *lost_list)
{
	const char *query_result;
	u64_t messageblk_idnr;
	int i, n;
	dm_list_init(lost_list);

	/* get all lost message blocks. Instead of doing all kinds of 
	 * nasty stuff here, we let the RDBMS handle all this. Problem
	 * is that MySQL cannot handle subqueries. This is handled by
	 * a left join select query.
	 * This query will select all message block idnr that have no
	 * associated physmessage in the physmessage table.
	 */
	snprintf(query, DEF_QUERYSIZE,
		 "SELECT mb.messageblk_idnr FROM %smessageblks mb "
		 "LEFT JOIN %sphysmessage pm ON "
		 "mb.physmessage_id = pm.id " "WHERE pm.id IS NULL",DBPFX,DBPFX);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: Could not execute query",
		      __FILE__, __func__);
		return DM_EQUERY;
	}

	n = db_num_rows();
	if (n < 1) {
		trace(TRACE_DEBUG, "%s,%s: no lost messageblocks",
		      __FILE__, __func__);
		db_free_result();
		return DM_SUCCESS;
	}

	/* FIXME: this is actually properly designed... */
	for (i = 0; i < n; i++) {
		query_result = db_get_result(i, 0);
		if (query_result)
			messageblk_idnr = strtoull(query_result, NULL, 10);
		else
			continue;

		trace(TRACE_INFO, "%s,%s: found lost block id [%llu]",
		      __FILE__, __func__, messageblk_idnr);
		if (!dm_list_nodeadd
		    (lost_list, &messageblk_idnr, sizeof(u64_t))) {
			trace(TRACE_ERROR,
			      "%s,%s: could not add block to list",
			      __FILE__, __func__);
			dm_list_free(&lost_list->start);
			db_free_result();
			return -2;
		}
	}
	db_free_result();
	return DM_SUCCESS;
}

int db_icheck_messages(struct dm_list *lost_list)
{
	u64_t message_idnr;
	const char *query_result;
	int i, n;

	dm_list_init(lost_list);

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT msg.message_idnr FROM %smessages msg "
		 "LEFT JOIN %smailboxes mbx ON "
		 "msg.mailbox_idnr=mbx.mailbox_idnr "
		 "WHERE mbx.mailbox_idnr IS NULL",DBPFX,DBPFX);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not execute query",
		      __FILE__, __func__);
		return -2;
	}

	n = db_num_rows();
	if (n < 1) {
		trace(TRACE_DEBUG, "%s,%s: no lost messages",
		      __FILE__, __func__);
		db_free_result();
		return DM_SUCCESS;
	}

	/* FIXME: this is actually properly designed... */
	for (i = 0; i < n; i++) {
		query_result = db_get_result(i, 0);
		if (query_result)
			message_idnr = strtoull(query_result, NULL, 10);
		else
			continue;

		trace(TRACE_INFO, "%s,%s: found lost message id [%llu]",
		      __FILE__, __func__, message_idnr);
		if (!dm_list_nodeadd(lost_list, &message_idnr, sizeof(u64_t))) {
			trace(TRACE_ERROR,
			      "%s,%s: could not add message to list",
			      __FILE__, __func__);
			dm_list_free(&lost_list->start);
			db_free_result();
			return -2;
		}
	}
	db_free_result();
	return DM_SUCCESS;
}

int db_icheck_mailboxes(struct dm_list *lost_list)
{
	u64_t mailbox_idnr;
	const char *query_result;
	int i, n;

	dm_list_init(lost_list);

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT mbx.mailbox_idnr FROM %smailboxes mbx "
		 "LEFT JOIN %susers usr ON "
		 "mbx.owner_idnr=usr.user_idnr "
		 "WHERE usr.user_idnr is NULL",DBPFX,DBPFX);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not execute query",
		      __FILE__, __func__);
		return -2;
	}

	n = db_num_rows();
	if (n < 1) {
		trace(TRACE_DEBUG, "%s,%s: no lost mailboxes",
		      __FILE__, __func__);
		db_free_result();
		return DM_SUCCESS;
	}

	/* FIXME: this is actually properly designed... */
	for (i = 0; i < n; i++) {
		query_result = db_get_result(i, 0);
		if (query_result)
			mailbox_idnr = strtoull(query_result, NULL, 10);
		else
			continue;

		trace(TRACE_INFO, "%s,%s: found lost mailbox id [%llu]",
		      __FILE__, __func__, mailbox_idnr);
		if (!dm_list_nodeadd(lost_list, &mailbox_idnr, sizeof(u64_t))) {
			trace(TRACE_ERROR,
			      "%s,%s: could not add mailbox to list",
			      __FILE__, __func__);
			dm_list_free(&lost_list->start);
			db_free_result();
			return -2;
		}
	}
	db_free_result();
	return DM_SUCCESS;
}

int db_icheck_null_physmessages(struct dm_list *lost_list)
{
	u64_t physmessage_id;
	const char *result_string;
	unsigned i, n;

	dm_list_init(lost_list);

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT pm.id FROM %sphysmessage pm "
		 "LEFT JOIN %smessageblks mbk ON "
		 "pm.id = mbk.physmessage_id "
		 "WHERE mbk.physmessage_id is NULL",DBPFX,DBPFX);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not execute query",
		      __FILE__, __func__);
		return DM_EQUERY;
	}

	n = db_num_rows();
	if (n < 1) {
		trace(TRACE_DEBUG, "%s,%s: no null physmessages",
		      __FILE__, __func__);
		return DM_SUCCESS;
	}

	/* FIXME: this is actually properly designed... */
	for (i = 0; i < n; i++) {
		result_string = db_get_result(i, 0);
		if (result_string)
			physmessage_id = strtoull(result_string, NULL, 10);
		else
			continue;

		trace(TRACE_INFO,
		      "%s,%s: found empty physmessage_id [%llu]", __FILE__,
		      __func__, physmessage_id);
		if (!dm_list_nodeadd
		    (lost_list, &physmessage_id, sizeof(u64_t))) {
			trace(TRACE_ERROR,
			      "%s,%s: could not add physmessage "
			      "to list", __FILE__, __func__);
			dm_list_free(&lost_list->start);
			db_free_result();
			return -2;
		}
	}
	db_free_result();
	return DM_SUCCESS;
}

int db_icheck_null_messages(struct dm_list *lost_list)
{
	u64_t message_idnr;
	const char *query_result;
	int i, n;

	dm_list_init(lost_list);

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT msg.message_idnr FROM %smessages msg "
		 "LEFT JOIN %sphysmessage pm ON "
		 "msg.physmessage_id = pm.id " "WHERE pm.id is NULL",DBPFX,DBPFX);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not execute query",
		      __FILE__, __func__);
		return DM_EQUERY;
	}

	n = db_num_rows();
	if (n < 1) {
		trace(TRACE_DEBUG, "%s,%s: no null messages",
		      __FILE__, __func__);
		db_free_result();
		return DM_SUCCESS;
	}

	/* FIXME: this is actually properly designed... */
	for (i = 0; i < n; i++) {
		query_result = db_get_result(i, 0);
		if (query_result)
			message_idnr = strtoull(query_result, NULL, 10);
		else
			continue;

		trace(TRACE_INFO, "%s,%s: found empty message id [%llu]",
		      __FILE__, __func__, message_idnr);
		if (!dm_list_nodeadd(lost_list, &message_idnr, sizeof(u64_t))) {
			trace(TRACE_ERROR,
			      "%s,%s: could not add message to list",
			      __FILE__, __func__);
			dm_list_free(&lost_list->start);
			db_free_result();
			return -2;
		}
	}
	db_free_result();
	return DM_SUCCESS;
}

int db_set_isheader(GList *lost)
{
	GList *slices;
	if (! lost)
		return DM_SUCCESS;

	slices = g_list_slices(lost,100);
	slices = g_list_first(slices);
	while(slices) {
		snprintf(query, DEF_QUERYSIZE,
			"UPDATE %smessageblks"
			" SET is_header = '%u'"
			" WHERE messageblk_idnr IN (%s)",
			DBPFX, HEAD_BLOCK, (gchar *)slices->data);

		if (db_query(query) == -1) {
			trace(TRACE_ERROR, "%s,%s: could not access messageblks table",
			     __FILE__, __func__);
			return DM_EQUERY;
		}
		if (! g_list_next(slices))
			break;
		slices = g_list_next(slices);
	}
	g_list_free(slices);
	return DM_SUCCESS;
}

int db_icheck_isheader(GList  **lost)
{
	unsigned i;
	snprintf(query, DEF_QUERYSIZE,
			"SELECT MIN(messageblk_idnr),MAX(is_header) "
			"FROM %smessageblks "
			"GROUP BY physmessage_id HAVING MAX(is_header)=0",
			DBPFX);
	
	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not access messageblks table",
		     __FILE__, __func__);
		return DM_EQUERY;
	}

	for (i = 0; i < db_num_rows(); i++) 
		*(GList **)lost = g_list_append(*(GList **)lost, g_strdup(db_get_result(i, 0)));

	db_free_result();

	return DM_SUCCESS;
}


int db_set_headercache(GList *lost)
{
	u64_t pmsgid = -1;
	struct DbmailMessage *msg;
	if (! lost)
		return DM_SUCCESS;

	lost = g_list_first(lost);
	while (lost) {
		pmsgid = (u64_t)(GPOINTER_TO_UINT(lost->data));
	
		msg = dbmail_message_new();
		if (! msg)
			return DM_EQUERY;

		db_begin_transaction();
		if (! (msg = dbmail_message_retrieve(msg, pmsgid, DBMAIL_MESSAGE_FILTER_HEAD))) {
			trace(TRACE_WARNING,"%s,%s: error retrieving physmessage: [%llu]", 
					__FILE__, __func__,
					pmsgid);
			db_rollback_transaction();
			fprintf(stderr,"E");
		} else {
			if (dbmail_message_headers_cache(msg) != 1) {
				trace(TRACE_WARNING,"%s,%s: error caching headers for physmessage: [%llu]", 
					__FILE__, __func__,
					pmsgid);
				dbmail_message_free(msg);
				db_rollback_transaction();
				fprintf(stderr,"E");
			} else {
				db_commit_transaction();
				fprintf(stderr,".");
			}
			
		}
		dbmail_message_free(msg);
		if (! g_list_next(lost))
			break;
		lost = g_list_next(lost);
	}
	return DM_SUCCESS;
}

		
int db_icheck_headercache(GList **lost)
{
	unsigned i;
	snprintf(query, DEF_QUERYSIZE,
			"SELECT p.id FROM %sphysmessage p "
			"LEFT JOIN %sheadervalue h "
			"ON p.id = h.physmessage_id "
			"WHERE h.physmessage_id IS NULL",
			DBPFX, DBPFX);
	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: query failed",
				__FILE__, __func__);
		return DM_EQUERY;
	}
	
	for (i = 0; i < db_num_rows(); i++) 
		*(GList **)lost = g_list_append(*(GList **)lost, GUINT_TO_POINTER((unsigned)db_get_result_u64(i, 0)));

	db_free_result();

	return DM_SUCCESS;
}

int db_set_message_status(u64_t message_idnr, MessageStatus_t status)
{
	/** FIXME: We should check that, if a message is set from
	 * a status < MESSAGE_STATUS_DELETE 
	 * to >= MESSAGE_STATUS_DELETE, the curmail_size is also changed */
	snprintf(query, DEF_QUERYSIZE, "UPDATE %smessages SET status = %d "
		 "WHERE message_idnr = '%llu'",DBPFX, status, message_idnr);
	return db_query(query);
}

int db_delete_messageblk(u64_t messageblk_idnr)
{
	snprintf(query, DEF_QUERYSIZE,
		 "DELETE FROM %smessageblks "
		 "WHERE messageblk_idnr = '%llu'",DBPFX, messageblk_idnr);
	return db_query(query);
}

int db_delete_physmessage(u64_t physmessage_id)
{
	snprintf(query, DEF_QUERYSIZE,
		 "DELETE FROM %sphysmessage WHERE id = '%llu'",DBPFX,
		 physmessage_id);
	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not execute query",
		      __FILE__, __func__);
		return DM_EQUERY;
	}

	/* if foreign keys do their work (not with MySQL ISAM tables :( )
	   the next query would not be necessary */
	snprintf(query, DEF_QUERYSIZE,
		 "DELETE FROM %smessageblks WHERE physmessage_id = '%llu'",DBPFX,
		 physmessage_id);
	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not execute query. There "
		      "are now messageblocks in the database that have no "
		      "physmessage attached to them. run dbmail-util "
		      "to fix this.", __FILE__, __func__);

		return DM_EQUERY;
	}

	return DM_EGENERAL;
}

int db_delete_message(u64_t message_idnr)
{
	u64_t physmessage_id;

	if (db_get_physmessage_id(message_idnr, &physmessage_id) == DM_EQUERY) {
		trace(TRACE_ERROR, "%s,%s: error getting physmessage_id",
		      __FILE__, __func__);
		return DM_EQUERY;
	}

	/* now delete the message from the message table */
	snprintf(query, DEF_QUERYSIZE,
		 "DELETE FROM %smessages WHERE message_idnr = '%llu'",DBPFX,
		 message_idnr);
	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not execute query",
		      __FILE__, __func__);
		return DM_EQUERY;
	}

	/* find if there are other messages pointing to the same
	   physmessage entry */
	snprintf(query, DEF_QUERYSIZE,
		 "SELECT message_idnr FROM %smessages "
		 "WHERE physmessage_id = '%llu'",DBPFX, physmessage_id);
	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not execute query",
		      __FILE__, __func__);
		return DM_EQUERY;
	}
	if (db_num_rows() == 0) {
		/* there are no other messages with the same physmessage left.
		 *  the physmessage records and message blocks now need to
		 * be removed */
		db_free_result();
		if (db_delete_physmessage(physmessage_id) < 0) {
			trace(TRACE_ERROR,
			      "%s,%s: error deleting physmessage",
			      __FILE__, __func__);
			return DM_EQUERY;
		}
	} else
		db_free_result();
	return DM_EGENERAL;
}

int db_delete_mailbox(u64_t mailbox_idnr, int only_empty,
		      int update_curmail_size)
{
	unsigned i, n;
	u64_t *message_idnrs;
	u64_t user_idnr = 0;
	int result;
	u64_t mailbox_size = 0;

	/* get the user_idnr of the owner of the mailbox */
	result = db_get_mailbox_owner(mailbox_idnr, &user_idnr);
	if (result < 0) {
		trace(TRACE_ERROR,
		      "%s,%s: cannot find owner of mailbox for "
		      "mailbox [%llu]", __FILE__, __func__,
		      mailbox_idnr);
		return DM_EQUERY;
	}
	if (result == 0) {
		trace(TRACE_ERROR,
		      "%s,%s: unable to find owner of mailbox " "[%llu]",
		      __FILE__, __func__, mailbox_idnr);
		return DM_SUCCESS;
	}

	if (update_curmail_size) {
		if (db_get_mailbox_size(mailbox_idnr, 0, &mailbox_size) <
		    0) {
			trace(TRACE_ERROR,
			      "%s,%s: error getting mailbox size "
			      "for mailbox [%llu]", __FILE__, __func__,
			      mailbox_idnr);
			return DM_EQUERY;
		}
	}

	/* remove the mailbox */
	if (!only_empty) {
		/* delete mailbox */
		snprintf(query, DEF_QUERYSIZE,
			 "DELETE FROM %smailboxes WHERE mailbox_idnr = '%llu'",DBPFX,
			 mailbox_idnr);

		if (db_query(query) == -1) {
			trace(TRACE_ERROR,
			      "%s,%s: could not delete mailbox [%llu]",
			      __FILE__, __func__, mailbox_idnr);
			return DM_EQUERY;
		}
	}

	/* we want to delete all messages from the mailbox. So we
	 * need to find all messages in the box */
	snprintf(query, DEF_QUERYSIZE,
		 "SELECT message_idnr FROM %smessages "
		 "WHERE mailbox_idnr = '%llu'",DBPFX, mailbox_idnr);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: could not select message ID's for mailbox [%llu]",
		      __FILE__, __func__, mailbox_idnr);
		return DM_EQUERY;
	}

	n = db_num_rows();
	if (n == 0) {
		db_free_result();
		trace(TRACE_INFO, "%s,%s: mailbox is empty", __FILE__,
		      __func__);
	}

	if (!(message_idnrs = (u64_t *) dm_malloc(n * sizeof(u64_t)))) {
		trace(TRACE_ERROR, "%s,%s: error allocating memory",
		      __FILE__, __func__);
		return DM_EQUERY;
	}
	for (i = 0; i < n; i++)
		message_idnrs[i] = db_get_result_u64(i, 0);
	db_free_result();
	/* delete every message in the mailbox */
	for (i = 0; i < n; i++) {
		if (db_delete_message(message_idnrs[i]) == -1) {
			trace(TRACE_ERROR,
			      "%s,%s: error deleting message [%llu] "
			      "database might be inconsistent. "
			      "run dbmail-util",
			      __FILE__, __func__, message_idnrs[i]);
			dm_free(message_idnrs);
			return DM_EQUERY;
		}
	}
	dm_free(message_idnrs);

	/* calculate the new quotum */
	if (update_curmail_size) {
		if (db_subtract_quotum_used(user_idnr, mailbox_size) < 0) {
			trace(TRACE_ERROR,
			      "%s,%s: error decreasing curmail_size",
			      __FILE__, __func__);
			return DM_EQUERY;
		}
	}
	return DM_SUCCESS;
}

int db_send_message_lines(void *fstream, u64_t message_idnr,
			  long lines, int no_end_dot)
{
	u64_t physmessage_id = 0;
	char *buffer = NULL;
	int buffer_pos;
	const char *nextpos;
	const char *tmppos = NULL;
	int block_count;
	u64_t rowlength;
	int n;
	const char *query_result;

	trace(TRACE_DEBUG, "%s,%s: request for [%ld] lines",
	      __FILE__, __func__, lines);

	/* first find the physmessage_id */
	snprintf(query, DEF_QUERYSIZE,
		 "SELECT physmessage_id FROM %smessages "
		 "WHERE message_idnr = '%llu'",DBPFX, message_idnr);
	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: error executing query",
		      __FILE__, __func__);
		return DM_SUCCESS;
	}
	physmessage_id = db_get_result_u64(0, 0);
	db_free_result();

	buffer = dm_malloc(WRITE_BUFFER_SIZE * 2);
	if (buffer == NULL) {
		trace(TRACE_ERROR, "%s,%s: error allocating memory for buffer",
		      __FILE__, __func__);
		return DM_SUCCESS;
	}

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT messageblk FROM %smessageblks "
		 "WHERE physmessage_id='%llu' "
		 "ORDER BY messageblk_idnr ASC",DBPFX, physmessage_id);
	trace(TRACE_DEBUG, "%s,%s: executing query [%s]",
	      __FILE__, __func__, query);

	if (db_query(query) == -1) {
		dm_free(buffer);
		return DM_SUCCESS;
	}

	trace(TRACE_DEBUG,
	      "%s,%s: sending [%ld] lines from message [%llu]", __FILE__,
	      __func__, lines, message_idnr);

	block_count = 0;
	n = db_num_rows();
	/* loop over all rows in the result set, until the right amount of
	 * lines has been read 
	 */
	while ((block_count < n)
	       && ((lines > 0) || (lines == -2) || (block_count == 0))) {
		query_result = db_get_result(block_count, 0);
		nextpos = query_result;
		rowlength = (u64_t) db_get_length(block_count, 0);

		/* reset our buffer */
		memset(buffer, '\0', (WRITE_BUFFER_SIZE) * 2);
		buffer_pos = 0;

		while ((*nextpos != '\0') && (rowlength > 0)
		       && ((lines > 0) || (lines == -2)
			   || (block_count == 0))) {

			if (*nextpos == '\n') {
				/* first block is always the full header 
				   so this should not be counted when parsing
				   if lines == -2 none of the lines should be counted 
				   since the whole message is requested */
				if ((lines != -2) && (block_count != 0))
					lines--;

				if (tmppos != NULL) {
					if (*tmppos == '\r') {
						buffer[buffer_pos++] =
						    *nextpos;
					} else {
						buffer[buffer_pos++] =
						    '\r';
						buffer[buffer_pos++] =
						    *nextpos;
					}
				} else {
					buffer[buffer_pos++] = '\r';
					buffer[buffer_pos++] = *nextpos;
				}
			} else {
				if (*nextpos == '.') {
					if (tmppos != NULL) {
						if (*tmppos == '\n') {
							buffer
							    [buffer_pos++]
							    = '.';
							buffer
							    [buffer_pos++]
							    = *nextpos;
						} else {
							buffer
							    [buffer_pos++]
							    = *nextpos;
						}
					} else {
						buffer[buffer_pos++] =
						    *nextpos;
					}
				} else {
					buffer[buffer_pos++] = *nextpos;
				}
			}

			tmppos = nextpos;

			/* get the next character */
			nextpos++;
			rowlength--;

			if (rowlength % WRITE_BUFFER_SIZE == 0) {
				/* purge buffer at every WRITE_BUFFER_SIZE bytes  */
				if (fwrite(buffer, sizeof(char), 
					   strlen(buffer), (FILE *)fstream) !=
				    strlen(buffer)) {
					trace(TRACE_ERROR, "%s,%s: error writing to "
					      "fstream", __FILE__, __func__);
					db_free_result();
					dm_free(buffer);
					return DM_SUCCESS;
				}
				/*  cleanup the buffer  */
				memset(buffer, '\0',
				       (WRITE_BUFFER_SIZE * 2));
				buffer_pos = 0;
			}
		}
		/* next block in while loop */
		block_count++;
		trace(TRACE_DEBUG, "%s,%s: getting nextblock [%d]\n",
		      __FILE__, __func__, block_count);
		/* flush our buffer */
		if (fwrite(buffer, sizeof(char), strlen(buffer),
			   (FILE *) fstream) != strlen(buffer)) {
			trace(TRACE_ERROR, "%s,%s: error writing to file stream",
			      __FILE__, __func__);
			db_free_result();
			dm_free(buffer);
			return DM_SUCCESS;
		}
	}
	/* delimiter */
	if (no_end_dot == 0)
		fprintf((FILE *) fstream, "\r\n.\r\n");

	db_free_result();
	dm_free(buffer);
	return DM_EGENERAL;
}

int db_createsession(u64_t user_idnr, PopSession_t * session_ptr)
{
	struct message tmpmessage;
	int message_counter = 0;
	unsigned i;
	const char *query_result;
	u64_t inbox_mailbox_idnr;

	dm_list_init(&session_ptr->messagelst);

	if (db_findmailbox("INBOX", user_idnr, &inbox_mailbox_idnr) <= 0) {
		trace(TRACE_ERROR, "%s,%s: error finding mailbox_idnr of "
		      "INBOX for user [%llu], exiting..",
		      __FILE__, __func__, user_idnr);
		return DM_EQUERY;
	}
	/* query is < MESSAGE_STATUS_DELETE  because we don't want deleted 
	 * messages
	 */
	snprintf(query, DEF_QUERYSIZE,
		 "SELECT pm.messagesize, msg.message_idnr, msg.status, "
		 "msg.unique_id FROM %smessages msg, %sphysmessage pm "
		 "WHERE msg.mailbox_idnr = '%llu' "
		 "AND msg.status < '%d' "
		 "AND msg.physmessage_id = pm.id "
		 "order by status ASC",DBPFX,DBPFX,
		 inbox_mailbox_idnr, MESSAGE_STATUS_DELETE);

	if (db_query(query) == -1) {
		return DM_EQUERY;
	}

	session_ptr->totalmessages = 0;
	session_ptr->totalsize = 0;

	message_counter = db_num_rows();

	if (message_counter < 1) {
		/* there are no messages for this user */
		db_free_result();
		return DM_EGENERAL;
	}

	/* messagecounter is total message, +1 tot end at message 1 */
	message_counter++;

	/* filling the list */
	trace(TRACE_DEBUG, "%s,%s: adding items to list", __FILE__,
	      __func__);
	for (i = 0; i < db_num_rows(); i++) {
		/* message size */
		tmpmessage.msize = db_get_result_u64(i, 0);
		/* real message id */
		tmpmessage.realmessageid = db_get_result_u64(i, 1);
		/* message status */
		tmpmessage.messagestatus = db_get_result_u64(i, 2);
		/* virtual message status */
		tmpmessage.virtual_messagestatus =
		    tmpmessage.messagestatus;
		/* unique id */
		query_result = db_get_result(i, 3);
		if (query_result)
			strncpy(tmpmessage.uidl, query_result, UID_SIZE);

		session_ptr->totalmessages++;
		session_ptr->totalsize += tmpmessage.msize;
		/* descending to create inverted list */
		message_counter--;
		tmpmessage.messageid = (u64_t) message_counter;
		dm_list_nodeadd(&session_ptr->messagelst, &tmpmessage,
			     sizeof(tmpmessage));
	}

	trace(TRACE_DEBUG, "%s,%s: adding succesfull", __FILE__,
	      __func__);

	/* setting all virtual values */
	session_ptr->virtual_totalmessages = session_ptr->totalmessages;
	session_ptr->virtual_totalsize = session_ptr->totalsize;

	db_free_result();

	return DM_EGENERAL;
}

void db_session_cleanup(PopSession_t * session_ptr)
{
	/* cleanups a session 
	   removes a list and all references */
	session_ptr->totalsize = 0;
	session_ptr->virtual_totalsize = 0;
	session_ptr->totalmessages = 0;
	session_ptr->virtual_totalmessages = 0;
	dm_list_free(&(session_ptr->messagelst.start));
}

int db_update_pop(PopSession_t * session_ptr)
{
	struct element *tmpelement;
	u64_t user_idnr = 0;

	/* get first element in list */
	tmpelement = dm_list_getstart(&session_ptr->messagelst);

	while (tmpelement != NULL) {
		/* check if they need an update in the database */
		if (((struct message *) tmpelement->data)->
		    virtual_messagestatus !=
		    ((struct message *) tmpelement->data)->messagestatus) {
			/* use one message to get the user_idnr that goes with the
			   messages */
			if (user_idnr == 0)
				user_idnr =
				    db_get_useridnr(((struct message *)
						     tmpelement->data)->
						    realmessageid);

			/* yes they need an update, do the query */
			snprintf(query, DEF_QUERYSIZE,
				 "UPDATE %smessages set status='%d' WHERE "
				 "message_idnr='%llu' AND status < '%d'",DBPFX,
				 ((struct message *)
				  tmpelement->data)->virtual_messagestatus,
				 ((struct message *) tmpelement->data)->
				 realmessageid, MESSAGE_STATUS_DELETE);

			/* FIXME: a message could be deleted already if it has been accessed
			 * by another interface and be deleted by sysop
			 * we need a check if the query failes because it doesn't exists anymore
			 * now it will just bailout. 
			 * ADDITION (ilja 2004-04-21) I don't think this is needed here.
			 * The query does not fail when the message is already deleted, it
			 * just does not do anything! */
			if (db_query(query) == -1) {
				trace(TRACE_ERROR,
				      "%s,%s: could not execute query",
				      __FILE__, __func__);
				return DM_EQUERY;
			}
		}
		tmpelement = tmpelement->nextnode;
	}

	/* because the status of some messages might have changed (for instance
	 * to status >= MESSAGE_STATUS_DELETE, the quotum has to be 
	 * recalculated */
	if (user_idnr != 0) {
		if (db_calculate_quotum_used(user_idnr) == -1) {
			trace(TRACE_ERROR, "%s,%s: error calculating quotum used",
			      __FILE__, __func__);
			return DM_EQUERY;
		}
	}
	return DM_SUCCESS;
}

int db_set_deleted(u64_t * affected_rows)
{
	assert(affected_rows != NULL);
	*affected_rows = 0;

	snprintf(query, DEF_QUERYSIZE,
		 "UPDATE %smessages SET status = '%d' WHERE status = '%d'",DBPFX,
		 MESSAGE_STATUS_PURGE, MESSAGE_STATUS_DELETE);
	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: Could not execute query",
		      __FILE__, __func__);
		return DM_EQUERY;
	}
	*affected_rows = db_get_affected_rows();
	return DM_EGENERAL;
}

int db_deleted_purge(u64_t * affected_rows)
{
	unsigned i;
	u64_t *message_idnrs;

	assert(affected_rows != NULL);
	*affected_rows = 0;

	/* first we're deleting all the messageblks */
	snprintf(query, DEF_QUERYSIZE,
		 "SELECT message_idnr FROM %smessages WHERE status='%d'",DBPFX,
		 MESSAGE_STATUS_PURGE);
	trace(TRACE_DEBUG, "%s,%s: executing query [%s]",
	      __FILE__, __func__, query);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: Cound not fetch message ID numbers",
		      __FILE__, __func__);
		return DM_EQUERY;
	}

	*affected_rows = db_num_rows();
	if (*affected_rows == 0) {
		trace(TRACE_DEBUG, "%s,%s: no messages to purge",
		      __FILE__, __func__);
		db_free_result();
		return DM_SUCCESS;
	}
	if (!(message_idnrs =
	      (u64_t *) dm_malloc(*affected_rows * sizeof(u64_t)))) {
		trace(TRACE_ERROR, "%s,%s: error allocating memory",
		      __FILE__, __func__);
		return -2;
	}
	
	/* delete each message */
	for (i = 0; i < *affected_rows; i++)
		message_idnrs[i] = db_get_result_u64(i, 0);
	db_free_result();
	for (i = 0; i < *affected_rows; i++) {
		if (db_delete_message(message_idnrs[i]) == -1) {
			trace(TRACE_ERROR, "%s,%s: error deleting message",
			      __FILE__, __func__);
			dm_free(message_idnrs);
			return DM_EQUERY;
		}
	}
	dm_free(message_idnrs);
	return DM_EGENERAL;
}

int db_deleted_count(u64_t * affected_rows)
{
	assert(affected_rows != NULL);
	*affected_rows = 0;

	/* first we're deleting all the messageblks */
	snprintf(query, DEF_QUERYSIZE,
		 "SELECT message_idnr FROM %smessages WHERE status='%d'",
		 DBPFX, MESSAGE_STATUS_PURGE);
	trace(TRACE_DEBUG, "%s,%s: executing query [%s]",
	      __FILE__, __func__, query);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: Cound not fetch message ID numbers",
		      __FILE__, __func__);
		return DM_EQUERY;
	}

	*affected_rows = db_num_rows();

	db_free_result();
	return DM_EGENERAL;
}

int db_imap_append_msg(const char *msgdata, u64_t datalen,
		       u64_t mailbox_idnr, u64_t user_idnr,
		       timestring_t internal_date, u64_t * msg_idnr)
{
	u64_t message_idnr;
	u64_t messageblk_idnr;
	u64_t physmessage_id = 0;
	u64_t count;
	char unique_id[UID_SIZE];	/* unique id */

	switch (db_check_quotum_used(user_idnr, datalen)) {
	case -1:
		trace(TRACE_ERROR, "%s,%s: error checking quotum",
		      __FILE__, __func__);
		return DM_EQUERY;
	case 1:
		trace(TRACE_INFO, "%s,%s: user [%llu] would exceed quotum",
		      __FILE__, __func__, user_idnr);
		return 2;
	}

	if (strlen(internal_date) > 0) {
		if (db_insert_physmessage_with_internal_date(internal_date,
							     &physmessage_id)
		    < 0) {
			trace(TRACE_ERROR,
			      "%s,%s: could not create physmessage "
			      "with internal date [%s]", __FILE__,
			      __func__, internal_date);
			return DM_EQUERY;
		}
	} else {
		if (db_insert_physmessage(&physmessage_id) < 0) {
			trace(TRACE_ERROR,
			      "%s,%s: could not create physmessage",
			      __FILE__, __func__);
			return DM_EQUERY;
		}
	}

	/* create a msg 
	 * according to the rfc, the recent flag has to be set to '1'.
	 * this also means that the status will be set to '001'
	 */
	snprintf(query, DEF_QUERYSIZE,
		 "INSERT INTO %smessages "
		 "(mailbox_idnr, physmessage_id, unique_id, status,"
		 "recent_flag) VALUES ('%llu', '%llu', '', '%d', '1')",DBPFX,
		 mailbox_idnr, physmessage_id, MESSAGE_STATUS_SEEN);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not create message",
		      __FILE__, __func__);
		return DM_EQUERY;
	}

	/* fetch the id of the new message */
	message_idnr = db_insert_result("message_idnr");

	/* ok insert blocks */
	/* first the header: scan until double newline */
	for (count = 1; count < datalen; count++)
		if (msgdata[count - 1] == '\n' && msgdata[count] == '\n')
			break;

	if (count == datalen) {
		trace(TRACE_INFO,
		      "%s,%s: no double newline found [invalid msg]\n",
		      __FILE__, __func__);
		if (db_delete_message(message_idnr) == -1) {
			trace(TRACE_ERROR,
			      "%s,%s: could not delete invalid message"
			      "%llu. Database could be invalid now..",
			      __FILE__, __func__, message_idnr);
		}
		return DM_EGENERAL;
	}

	if (count == datalen - 1) {
		/* msg consists of a single header */
		trace(TRACE_INFO, "%s,%s: msg only contains a header",
		      __FILE__, __func__);

		if (db_insert_message_block_physmessage(msgdata, datalen,
							physmessage_id,
							&messageblk_idnr,1) == -1
		    || db_insert_message_block(" \n", 2, message_idnr,
					       &messageblk_idnr,1) == -1) {
			trace(TRACE_ERROR,
			      "%s,%s: could not insert msg block\n",
			      __FILE__, __func__);
			if (db_delete_message(message_idnr) == -1) {
				trace(TRACE_ERROR,
				      "%s,%s: could not delete invalid message"
				      "%llu. Database could be invalid now..",
				      __FILE__, __func__,
				      message_idnr);
			}
			return DM_EQUERY;
		}
	} else {
		/* 
		 * output header: 
		 * the first count bytes is the header
		 */
		count++;

		if (db_insert_message_block_physmessage(
			    msgdata, count, 
			    physmessage_id,
			    &messageblk_idnr,1) == -1) {
			trace(TRACE_ERROR,
			      "%s,%s: could not insert msg block\n",
			      __FILE__, __func__);
			if (db_delete_message(message_idnr) == -1) {
				trace(TRACE_ERROR,
				      "%s,%s: could not delete invalid message"
				      "%llu. Database could be  invalid now..",
				      __FILE__, __func__,
				      message_idnr);
			}
			return DM_EQUERY;
		}

		/* output message */
		while ((datalen - count) > READ_BLOCK_SIZE) {
			if (db_insert_message_block_physmessage(
				    &msgdata[count],
				    READ_BLOCK_SIZE,
				    physmessage_id,
				    &messageblk_idnr,0) == -1) {
				trace(TRACE_ERROR,
				      "%s,%s: could not insert msg block",
				      __FILE__, __func__);
				if (db_delete_message(message_idnr) == -1) {
					trace(TRACE_ERROR,
					      "%s,%s: could not delete invalid "
					      "message %llu. Database could be invalid now..",
					      __FILE__, __func__,
					      message_idnr);
				}
				return DM_EQUERY;
			}
			count += READ_BLOCK_SIZE;
		}


		if (db_insert_message_block_physmessage(
			    &msgdata[count],
			    datalen - count, physmessage_id,
			    &messageblk_idnr,0) == -1) {
			trace(TRACE_ERROR,
			      "%s,%s:  could not insert msg block\n",
			      __FILE__, __func__);
			if (db_delete_message(message_idnr) == -1) {
				trace(TRACE_ERROR,
				      "%s,%s: could not delete invalid "
				      "message %llu. Database could be invalid now..",
				      __FILE__, __func__,
				      message_idnr);
			}
			return DM_EQUERY;
		}

	}
	/* set message size */
	if (db_physmessage_set_sizes(physmessage_id, datalen, 0) < 0) {
		trace(TRACE_ERROR, "%s,%s: Error setting physmessages sizes",
		      __FILE__, __func__);
		return DM_EQUERY;
	}
	/* create a unique id */
	create_unique_id(unique_id, message_idnr);
	db_message_set_unique_id(message_idnr, unique_id);

	/* recalculate quotum used */
	db_add_quotum_used(user_idnr, datalen);

	*msg_idnr = message_idnr;
	return DM_SUCCESS;
}

int db_findmailbox(const char *fq_name, u64_t user_idnr,
		   u64_t * mailbox_idnr)
{
	const char *username = NULL;
	char *mailbox_name;
	char *name_str_copy;
	char *tempstr;
	size_t index;
	int result;
	u64_t owner_idnr = 0;

	assert(mailbox_idnr != NULL);
	*mailbox_idnr = 0;

	trace(TRACE_DEBUG, "%s,%s: looking for mailbox with FQN [%s].",
	      __FILE__, __func__, fq_name);

	name_str_copy = dm_strdup(fq_name);
	/* see if this is a #User mailbox */
	if ((strlen(NAMESPACE_USER) > 0) &&
	    (strstr(fq_name, NAMESPACE_USER) == fq_name)) {
		index = strcspn(name_str_copy, MAILBOX_SEPARATOR);
		tempstr = &name_str_copy[index + 1];
		index = strcspn(tempstr, MAILBOX_SEPARATOR);
		username = tempstr;
		tempstr[index] = '\0';
		mailbox_name = &tempstr[index + 1];
	} else {
		if ((strlen(NAMESPACE_PUBLIC) > 0) &&
		    (strstr(fq_name, NAMESPACE_PUBLIC) == fq_name)) {
			index = strcspn(name_str_copy, MAILBOX_SEPARATOR);
			mailbox_name = &name_str_copy[index + 1];
			username = PUBLIC_FOLDER_USER;
		} else {
			mailbox_name = name_str_copy;
			owner_idnr = user_idnr;
		}
	}
	if (username) {
		trace(TRACE_DEBUG, "%s,%s: finding user with name [%s].",
		      __FILE__, __func__, username);
		result = auth_user_exists(username, &owner_idnr);
		if (result < 0) {
			trace(TRACE_ERROR, "%s,%s: error checking id of "
			      "user.", __FILE__, __func__);
			return DM_EQUERY;
		}
		if (result == 0) {
			trace(TRACE_INFO, "%s,%s user [%s] not found.",
			      __FILE__, __func__, username);
			return DM_SUCCESS;
		}
	}
	result =
	    db_findmailbox_owner(mailbox_name, owner_idnr, mailbox_idnr);
	if (result < 0) {
		trace(TRACE_ERROR,
		      "%s,%s: error finding mailbox [%s] with "
		      "owner [%s, %llu]", __FILE__, __func__,
		      mailbox_name, username, owner_idnr);
		return DM_EQUERY;
	}
	dm_free(name_str_copy);
	return result;
}

int db_findmailbox_owner(const char *name, u64_t owner_idnr,
			 u64_t * mailbox_idnr)
{
	char *local_name;

	assert(mailbox_idnr != NULL);
	*mailbox_idnr = 0;

	local_name = dm_strdup(name);
	if (local_name == NULL) {
		trace(TRACE_ERROR, "%s,%s: error dm_strdup(name). Out of memory?",
		      __FILE__, __func__);
		return DM_EQUERY;
	}

	convert_inbox_to_uppercase(local_name);
	
	snprintf(query, DEF_QUERYSIZE,
		 "SELECT mailbox_idnr FROM %smailboxes "
		 "WHERE name='%s' AND owner_idnr='%llu'",DBPFX, local_name,
		 owner_idnr);
	dm_free(local_name);
	if (db_query(query) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: could not select mailbox '%s'\n", __FILE__,
		      __func__, name);
		db_free_result();
		return DM_EQUERY;
	}

	if (db_num_rows() < 1) {
		trace(TRACE_DEBUG,"%s,%s: no mailbox found", __FILE__, __func__);
		db_free_result();
		return DM_SUCCESS;
	} else {
		*mailbox_idnr = db_get_result_u64(0, 0);
		db_free_result();
	}

	if (*mailbox_idnr == 0)
		return DM_SUCCESS;
	return DM_EGENERAL;
}

int db_list_mailboxes_by_regex(u64_t user_idnr, int only_subscribed,
			       const char * pattern,
			       u64_t ** mailboxes,
			       unsigned int *nr_mailboxes)
{
	unsigned int i;
	u64_t *tmp_mailboxes;
	u64_t *all_mailboxes;
	char** all_mailbox_names;
	u64_t *all_mailbox_owners;
	unsigned n_rows;

	assert(mailboxes != NULL);
	assert(nr_mailboxes != NULL);

	*mailboxes = NULL;
	*nr_mailboxes = 0;
	
	trace(TRACE_DEBUG, "%s,%s: in func", __FILE__, __func__);
	if (only_subscribed)
		snprintf(query, DEF_QUERYSIZE,
			 "SELECT mbx.name, mbx.mailbox_idnr, mbx.owner_idnr "
			 "FROM %smailboxes mbx "
			 "LEFT JOIN %sacl acl "
			 "ON mbx.mailbox_idnr = acl.mailbox_id "
			 "LEFT JOIN %susers usr "
			 "ON acl.user_id = usr.user_idnr "
			 "LEFT JOIN %ssubscription sub "
			 "ON sub.mailbox_id = mbx.mailbox_idnr "
			 "WHERE "
			 "sub.user_id = '%llu' AND ("
			 "(mbx.owner_idnr = '%llu') OR "
			 "(acl.user_id = '%llu' AND "
			 "  acl.lookup_flag = '1') OR "
			 "(usr.userid = '%s' AND acl.lookup_flag = '1'))",
			 DBPFX, DBPFX, DBPFX, DBPFX,
			 user_idnr, user_idnr, user_idnr,
			 DBMAIL_ACL_ANYONE_USER);
	else
		snprintf(query, DEF_QUERYSIZE,
			 "SELECT mbx.name, mbx.mailbox_idnr, mbx.owner_idnr "
			 "FROM %smailboxes mbx "
			 "LEFT JOIN %sacl acl "
			 "ON mbx.mailbox_idnr = acl.mailbox_id "
			 "LEFT JOIN %susers usr "
			 "ON acl.user_id = usr.user_idnr "
			 "WHERE "
			 "(mbx.owner_idnr = '%llu') OR "
			 "(acl.user_id = '%llu' AND "
			 "  acl.lookup_flag = '1') OR "
			 "(usr.userid = '%s' AND acl.lookup_flag = '1')",
			 DBPFX, DBPFX, DBPFX,
			 user_idnr, user_idnr, DBMAIL_ACL_ANYONE_USER);
	
	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: error during mailbox query",
		      __FILE__, __func__);
		return (-1);
	}
	n_rows = db_num_rows();
	if (n_rows == 0) {
		/* none exist, none matched */
		db_free_result();
		return DM_SUCCESS;
	}
	all_mailboxes = (u64_t *) dm_malloc(n_rows * sizeof(u64_t));
	all_mailbox_names = (char **) dm_malloc(n_rows * sizeof(char*));
	all_mailbox_owners = (u64_t *) dm_malloc(n_rows * sizeof(u64_t));
	tmp_mailboxes = (u64_t *) dm_malloc(n_rows * sizeof(u64_t));
	if (!all_mailboxes || !all_mailbox_names || !all_mailbox_owners || 
	    !tmp_mailboxes) {
		trace(TRACE_ERROR, "%s,%s: not enough memory\n",
		      __FILE__, __func__);
		if (all_mailboxes)
			dm_free(all_mailboxes);
		if (all_mailbox_names)
			dm_free(all_mailbox_names);
		if (all_mailbox_owners)
			dm_free(all_mailbox_owners);
		if (tmp_mailboxes)
			dm_free(tmp_mailboxes);
		return (-2);
	}
	
	for (i = 0; i < n_rows; i++) {
		all_mailbox_names[i] = dm_strdup(db_get_result(i, 0));
		all_mailboxes[i] = db_get_result_u64(i, 1);
		all_mailbox_owners[i] = db_get_result_u64(i, 2);
	} 
	db_free_result();

	for (i = 0; i < n_rows; i++) {
		char *mailbox_name;
		u64_t mailbox_idnr = all_mailboxes[i];
		u64_t owner_idnr = all_mailbox_owners[i];
		char *simple_mailbox_name = all_mailbox_names[i];

		/* add possible namespace prefix to mailbox_name */
		mailbox_name = mailbox_add_namespace(simple_mailbox_name, owner_idnr, user_idnr);
		if (mailbox_name) {
			if (listex_match(pattern, mailbox_name, MAILBOX_SEPARATOR, 0)) {
				tmp_mailboxes[*nr_mailboxes] = mailbox_idnr;
				(*nr_mailboxes)++;
			}
		}
		g_free(mailbox_name);
	}
	dm_free(all_mailbox_names);
	dm_free(all_mailboxes);
	dm_free(all_mailbox_owners);

	if (*nr_mailboxes == 0) {
		/* none exist, none matched */
		dm_free(tmp_mailboxes);
		return DM_SUCCESS;
	}

	*mailboxes = tmp_mailboxes;

	return DM_EGENERAL;
}

int db_findmailbox_by_regex(u64_t owner_idnr, const char *pattern,
			    u64_t ** children, unsigned *nchildren,
			    int only_subscribed)
{
	*children = NULL;

	/* list normal mailboxes */
	if (db_list_mailboxes_by_regex(owner_idnr, only_subscribed, pattern, children, nchildren) < 0) {
		trace(TRACE_ERROR, "%s,%s: error listing mailboxes",
		      __FILE__, __func__);
		return DM_EQUERY;
	}

	if (*nchildren == 0) {
		trace(TRACE_INFO, "%s, %s: did not find any mailboxes that "
		      "match pattern. returning 0, nchildren = 0",
		      __FILE__, __func__);
		return DM_SUCCESS;
	}


	/* store matches */
	trace(TRACE_INFO, "%s,%s: found [%d] mailboxes", __FILE__,
	      __func__, *nchildren);
	return DM_SUCCESS;
}

int db_getmailbox(mailbox_t * mb)
{
	unsigned i;
	unsigned exists, seen, recent;
	
	/* free existing MSN list */
	if (mb->seq_list) {
		dm_free(mb->seq_list);
		mb->seq_list = NULL;
	}

	mb->flags = 0;
	mb->exists = 0;
	mb->unseen = 0;
	mb->recent = 0;
	mb->msguidnext = 0;

	/* select mailbox */
	snprintf(query, DEF_QUERYSIZE,
		 "SELECT permission,"
		 "seen_flag,"
		 "answered_flag,"
		 "deleted_flag,"
		 "flagged_flag,"
		 "recent_flag,"
		 "draft_flag "
		 "FROM %smailboxes WHERE mailbox_idnr = '%llu'",DBPFX, mb->uid);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not select mailbox\n",
		      __FILE__, __func__);
		return DM_EQUERY;
	}


	if (db_num_rows() == 0) {
		trace(TRACE_ERROR, "%s,%s: invalid mailbox id specified",
		      __FILE__, __func__);
		db_free_result();
		return DM_EQUERY;
	}

	mb->permission = db_get_result_int(0, 0);

	if (db_get_result(0, 1))
		mb->flags |= IMAPFLAG_SEEN;
	if (db_get_result(0, 2))
		mb->flags |= IMAPFLAG_ANSWERED;
	if (db_get_result(0, 3))
		mb->flags |= IMAPFLAG_DELETED;
	if (db_get_result(0, 4))
		mb->flags |= IMAPFLAG_FLAGGED;
	if (db_get_result(0, 5))
		mb->flags |= IMAPFLAG_RECENT;
	if (db_get_result(0, 6))
		mb->flags |= IMAPFLAG_DRAFT;

	db_free_result();

	/* count messages */
	snprintf(query, DEF_QUERYSIZE,
 			 "SELECT 'a',COUNT(*) FROM %smessages WHERE mailbox_idnr='%llu' "
 			 "AND (status='%d' OR status='%d') UNION "
 			 "SELECT 'b',COUNT(*) FROM %smessages WHERE mailbox_idnr='%llu' "
 			 "AND (status='%d' OR status='%d') AND seen_flag=1 UNION "
 			 "SELECT 'c',COUNT(*) FROM %smessages WHERE mailbox_idnr='%llu' "
 			 "AND (status='%d' OR status='%d') AND recent_flag=1", 
 			 DBPFX, mb->uid, MESSAGE_STATUS_NEW, MESSAGE_STATUS_SEEN,
 			 DBPFX, mb->uid, MESSAGE_STATUS_NEW, MESSAGE_STATUS_SEEN,
 			 DBPFX, mb->uid, MESSAGE_STATUS_NEW, MESSAGE_STATUS_SEEN);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: query error", __FILE__, __func__);
		return DM_EQUERY;
	}
	
 	exists = (unsigned)db_get_result_int(0,1);
 	seen   = (unsigned)db_get_result_int(1,1);
 	recent = (unsigned)db_get_result_int(2,1);
  
 	mb->exists = exists;
 	mb->unseen = exists - seen;
 	mb->recent = recent;
 
	db_free_result();
	
	if(mb->exists) {
		/* get  messages */
		snprintf(query, DEF_QUERYSIZE,
			 "SELECT message_idnr FROM %smessages WHERE mailbox_idnr = '%llu' "
			 "AND status < '%d' ORDER BY message_idnr ASC",
			 DBPFX, mb->uid, MESSAGE_STATUS_DELETE);
		
		if (db_query(query) == -1) {
			trace(TRACE_ERROR, "%s,%s: query error [%s]", __FILE__, __func__, query);
			return DM_EQUERY;
		}
		
		trace(TRACE_DEBUG,"%s,%s: exists [%d] num_rows [%d]",__FILE__, __func__, mb->exists, db_num_rows());
		if (! (mb->seq_list = (u64_t *) dm_malloc(sizeof(u64_t) * mb->exists))) {
			db_free_result();
			trace(TRACE_ERROR,"%s,%s: malloc error mb->seq_list [%d]",
					__FILE__, __func__,
					mb->exists);
			return DM_EQUERY;
		}
		
		for (i = 0; i < db_num_rows(); i++) 
			mb->seq_list[i] = db_get_result_u64(i, 0);

		db_free_result();
	}

	/* now determine the next message UID 
	 * NOTE expunged messages are selected as well in order to be 
	 * able to restore them 
	 */
	snprintf(query, DEF_QUERYSIZE, "SELECT message_idnr+1 FROM %smessages "
			"ORDER BY message_idnr DESC LIMIT 1",DBPFX);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: query error [%s]", __FILE__, __func__, query);
		if(mb->seq_list) {
			dm_free(mb->seq_list);
			mb->seq_list = NULL;
		}
		return DM_EQUERY;
	}
	mb->msguidnext = db_get_result_u64(0, 0);
	db_free_result();

	return DM_SUCCESS;
}

int db_createmailbox(const char *name, u64_t owner_idnr,
		     u64_t * mailbox_idnr)
{
	const char *simple_name;
	assert(mailbox_idnr != NULL);
	*mailbox_idnr = 0;

	/* remove namespace information from mailbox name */
	if (!(simple_name = mailbox_remove_namespace(name))) {
		trace(TRACE_ERROR,
		      "%s,%s: could not create simple mailbox name "
		      "from full name", __FILE__, __func__);
		return DM_EQUERY;
	}
	snprintf(query, DEF_QUERYSIZE,
		 "INSERT INTO %smailboxes (name, owner_idnr,"
		 "seen_flag, answered_flag, deleted_flag, flagged_flag, "
		 "recent_flag, draft_flag, permission)"
		 " VALUES ('%s', '%llu', 1, 1, 1, 1, 1, 1, 2)",DBPFX,
		 simple_name, owner_idnr);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not create mailbox",
		      __FILE__, __func__);
		return DM_EQUERY;
	}
	*mailbox_idnr = db_insert_result("mailbox_idnr");
	return DM_SUCCESS;
}


int db_find_create_mailbox(const char *name, u64_t owner_idnr,
			   u64_t * mailbox_idnr)
{
	u64_t mboxidnr;

	assert(mailbox_idnr != NULL);
	*mailbox_idnr = 0;

#ifdef AUTHLDAP
	if ((db_user_find_create(owner_idnr) < 0)) {
		trace(TRACE_ERROR, "%s,%s: unable to find or create sql shadow "
				"account for useridnr [%llu]", 
				__FILE__, __func__, owner_idnr);
		return DM_EQUERY;
	}
#endif

	/* Did we fail to find the mailbox? */
	if (db_findmailbox_owner(name, owner_idnr, &mboxidnr) != 1) {
		/* Did we fail to create the mailbox? */
		if (db_createmailbox(name, owner_idnr, &mboxidnr) != 0) {
			trace(TRACE_ERROR, "%s, %s: could not create mailbox [%s]",
					__FILE__, __func__, name);
			return DM_EQUERY;
		}
		trace(TRACE_DEBUG, "%s, %s: mailbox [%s] created on the fly", 
				__FILE__, __func__, name);
	}
	trace(TRACE_DEBUG, "%s, %s: mailbox [%s] found",
	      __FILE__, __func__, name);

	*mailbox_idnr = mboxidnr;
	return DM_SUCCESS;
}

int db_listmailboxchildren(u64_t mailbox_idnr, u64_t user_idnr,
			   u64_t ** children, int *nchildren,
			   const char *filter)
{
	int i;
	char *mailbox_name = NULL;
	const char *tmp;

	/* retrieve the name of this mailbox */
	snprintf(query, DEF_QUERYSIZE,
		 "SELECT name FROM %smailboxes WHERE "
		 "mailbox_idnr = '%llu' AND owner_idnr = '%llu'",DBPFX,
		 mailbox_idnr, user_idnr);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: could not retrieve mailbox name\n", __FILE__,
		      __func__);
		return DM_EQUERY;
	}

	if (db_num_rows() == 0) {
		trace(TRACE_WARNING,
		      "%s,%s: No mailbox found with mailbox_idnr "
		      "[%llu]", __FILE__, __func__, mailbox_idnr);
		db_free_result();
		*children = NULL;
		*nchildren = 0;
		return DM_SUCCESS;
	}

	if ((tmp = db_get_result(0, 0))) 
		mailbox_name = dm_strdup(tmp);

	db_free_result();
	if (mailbox_name) {
		snprintf(query, DEF_QUERYSIZE,
			 "SELECT mailbox_idnr FROM %smailboxes WHERE name LIKE '%s/%s'"
			 " AND owner_idnr = '%llu'",DBPFX,
			 mailbox_name, filter, user_idnr);
		dm_free(mailbox_name);
	}
	else
		snprintf(query, DEF_QUERYSIZE,
			 "SELECT mailbox_idnr FROM %smailboxes WHERE name LIKE '%s'"
			 " AND owner_idnr = '%llu'",DBPFX, filter, user_idnr);
	
	/* now find the children */
	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not retrieve mailbox id",
		      __FILE__, __func__);
		return DM_EQUERY;
	}

	if (db_num_rows() == 0) {
		/* empty set */
		*children = NULL;
		*nchildren = 0;
		db_free_result();
		return DM_SUCCESS;
	}

	*nchildren = db_num_rows();
	if (*nchildren == 0) {
		*children = NULL;
		db_free_result();
		return DM_SUCCESS;
	}

	*children = (u64_t *) dm_malloc(sizeof(u64_t) * (*nchildren));

	if (!(*children)) {
		/* out of mem */
		trace(TRACE_ERROR, "%s,%s: out of memory\n", __FILE__,
		      __FILE__);
		db_free_result();
		return DM_EQUERY;
	}

	for (i = 0; i < *nchildren; i++) {
		(*children)[i] = db_get_result_u64(i, 0);
	}

	db_free_result();
	return DM_SUCCESS;		/* success */
}

int db_isselectable(u64_t mailbox_idnr)
{
	const char *query_result;
	long not_selectable;

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT no_select FROM %smailboxes WHERE mailbox_idnr = '%llu'",DBPFX,
		 mailbox_idnr);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not retrieve select-flag",
		      __FILE__, __func__);
		return DM_EQUERY;
	}

	if (db_num_rows() == 0) {
		db_free_result();
		return DM_SUCCESS;
	}

	query_result = db_get_result(0, 0);
	if (!query_result) {
		trace(TRACE_ERROR, "%s,%s: query result is NULL, but there is a "
		      "result set", __FILE__, __func__);
		db_free_result();
		return DM_EQUERY;
	}

	not_selectable = strtol(query_result, NULL, 10);
	db_free_result();
	if (not_selectable == 0)
		return DM_EGENERAL;
	else
		return DM_SUCCESS;
}

int db_noinferiors(u64_t mailbox_idnr)
{
	const char *query_result;
	long no_inferiors;

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT no_inferiors FROM %smailboxes WHERE mailbox_idnr = '%llu'",DBPFX,
		 mailbox_idnr);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: could not retrieve noinferiors-flag",
		      __FILE__, __func__);
		return DM_EQUERY;
	}

	if (db_num_rows() == 0) {
		db_free_result();
		return DM_SUCCESS;
	}

	query_result = db_get_result(0, 0);
	if (!query_result) {
		trace(TRACE_ERROR, "%s,%s: query result is NULL, but there is a "
		      "result set", __FILE__, __func__);
		db_free_result();
		return DM_SUCCESS;
	}
	no_inferiors = strtol(query_result, NULL, 10);
	db_free_result();

	return no_inferiors;
}

int db_setselectable(u64_t mailbox_idnr, int select_value)
{
	snprintf(query, DEF_QUERYSIZE,
		 "UPDATE %smailboxes SET no_select = %d WHERE mailbox_idnr = '%llu'",DBPFX,
		 (!select_value), mailbox_idnr);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not set noselect-flag",
		      __FILE__, __func__);
		return DM_EQUERY;
	}
	return DM_SUCCESS;
}



int db_get_mailbox_size(u64_t mailbox_idnr, int only_deleted,
			u64_t * mailbox_size)
{
	assert(mailbox_size != NULL);

	*mailbox_size = 0;

	if (only_deleted)
		snprintf(query, DEF_QUERYSIZE,
			 "SELECT sum(pm.messagesize) FROM %smessages msg, "
			 "%sphysmessage pm "
			 "WHERE msg.physmessage_id = pm.id "
			 "AND msg.mailbox_idnr = '%llu' "
			 "AND msg.status < '%d' "
			 "AND msg.deleted_flag = '1'",DBPFX,DBPFX, mailbox_idnr,
			 MESSAGE_STATUS_DELETE);
	else
		snprintf(query, DEF_QUERYSIZE,
			 "SELECT sum(pm.messagesize) FROM %smessages msg, "
			 "%sphysmessage pm "
			 "WHERE msg.physmessage_id = pm.id "
			 "AND msg.mailbox_idnr = '%llu' "
			 "AND msg.status < '%d'",DBPFX,DBPFX, mailbox_idnr,
			 MESSAGE_STATUS_DELETE);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not calculate size of "
		      "mailbox [%llu]", __FILE__, __func__,
		      mailbox_idnr);
		return DM_EQUERY;
	}

	if (db_num_rows() > 0) {
		*mailbox_size = db_get_result_u64(0, 0);
	}

	db_free_result();
	return DM_SUCCESS;
}

int db_removemsg(u64_t user_idnr, u64_t mailbox_idnr)
{
	u64_t mailbox_size;

	if (db_get_mailbox_size(mailbox_idnr, 0, &mailbox_size) < 0) {
		trace(TRACE_ERROR,
		      "%s,%s: error getting size for mailbox [%llu]",
		      __FILE__, __func__, mailbox_idnr);
		return DM_EQUERY;
	}

	/* update messages belonging to this mailbox: mark as deleted (status 
	   MESSAGE_STATUS_PURGE) */
	snprintf(query, DEF_QUERYSIZE,
		 "UPDATE %smessages SET status='%d' WHERE mailbox_idnr = '%llu'",DBPFX,
		 MESSAGE_STATUS_PURGE, mailbox_idnr);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: could not update messages in mailbox",
		      __FILE__, __func__);
		return DM_EQUERY;
	}

	if (db_subtract_quotum_used(user_idnr, mailbox_size) < 0) {
		trace(TRACE_ERROR,
		      "%s,%s: error subtracting mailbox size from "
		      "used quotum for mailbox [%llu], user [%llu]. Database "
		      "might be inconsistent. Run dbmail-util",
		      __FILE__, __func__, mailbox_idnr, user_idnr);
		return DM_EQUERY;
	}
	return DM_SUCCESS;		/* success */
}

int db_movemsg(u64_t mailbox_to, u64_t mailbox_from)
{
	snprintf(query, DEF_QUERYSIZE,
		 "UPDATE %smessages SET mailbox_idnr='%llu' WHERE"
		 " mailbox_idnr = '%llu'",DBPFX, mailbox_to, mailbox_from);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: could not update messages in mailbox\n",
		      __FILE__, __func__);
		return DM_EQUERY;
	}
	return DM_SUCCESS;		/* success */
}

int db_get_message_size(u64_t message_idnr, u64_t * message_size)
{
	const char *result_string;

	assert(message_size != NULL);

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT pm.messagesize FROM %sphysmessage pm, %smessages msg "
		 "WHERE pm.id = msg.physmessage_id "
		 "AND message_idnr = '%llu'",DBPFX,DBPFX, message_idnr);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: could not fetch message size for message id "
		      "[%llu]", __FILE__, __func__, message_idnr);
		return DM_EQUERY;
	}

	if (db_num_rows() != 1) {
		trace(TRACE_ERROR,
		      "%s,%s: message [%llu] does not exist/has "
		      "multiple entries\n",
		      __FILE__, __func__, message_idnr);
		db_free_result();
		return DM_EQUERY;
	}

	result_string = db_get_result(0, 0);
	if (result_string)
		*message_size = strtoull(result_string, NULL, 10);
	else {
		trace(TRACE_ERROR,
		      "%s,%s: no result set after requesting msgsize "
		      "of msg [%llu]\n",
		      __FILE__, __func__, message_idnr);
		db_free_result();
		return DM_EQUERY;
	}
	db_free_result();
	return DM_EGENERAL;

}

int db_copymsg(u64_t msg_idnr, u64_t mailbox_to, u64_t user_idnr,
	       u64_t * newmsg_idnr)
{
	u64_t msgsize;
	char unique_id[UID_SIZE];

	/* Get the size of the message to be copied. */
	if (db_get_message_size(msg_idnr, &msgsize) == -1) {
		trace(TRACE_ERROR, "%s,%s: error getting message size for "
		      "message [%llu]", __FILE__, __func__, msg_idnr);
		return DM_EQUERY;
	}

	/* Check to see if the user has room for the message. */
	switch (db_check_quotum_used(user_idnr, msgsize)) {
	case -1:
		trace(TRACE_ERROR, "%s,%s: error checking quotum",
		      __FILE__, __func__);
		return DM_EQUERY;
	case 1:
		trace(TRACE_INFO, "%s,%s: user [%llu] would exceed quotum",
		      __FILE__, __func__, user_idnr);
		return -2;
	}

	create_unique_id(unique_id, msg_idnr);

	/* Copy the message table entry of the message. */
	snprintf(query, DEF_QUERYSIZE,
		 "INSERT INTO %smessages (mailbox_idnr,"
		 "physmessage_id, seen_flag, answered_flag, deleted_flag, "
		 "flagged_flag, recent_flag, draft_flag, unique_id, status) "
		 "SELECT '%llu', "
		 "physmessage_id, seen_flag, answered_flag, deleted_flag, "
		 "flagged_flag, recent_flag, draft_flag, '%s', status "
		 "FROM %smessages WHERE message_idnr = '%llu'",DBPFX,
		 mailbox_to, unique_id,DBPFX, msg_idnr);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: error copying message",
		      __FILE__, __func__);
		return DM_EQUERY;
	}

	/* get the id of the inserted record */
	*newmsg_idnr = db_insert_result("message_idnr");

	/* update quotum */
	if (db_add_quotum_used(user_idnr, msgsize) == -1) {
		trace(TRACE_ERROR, "%s,%s: error setting the new quotum "
		      "used value for user [%llu]",
		      __FILE__, __func__, user_idnr);
		return DM_EQUERY;
	}

	return DM_EGENERAL;
}

int db_getmailboxname(u64_t mailbox_idnr, u64_t user_idnr, char *name)
{
	char *tmp_name, *tmp_fq_name;
	const char *query_result;
	int result;
	size_t tmp_fq_name_len;
	u64_t owner_idnr;

	result = db_get_mailbox_owner(mailbox_idnr, &owner_idnr);
	if (result <= 0) {
		trace(TRACE_ERROR, "%s,%s: error checking ownership of "
		      "mailbox", __FILE__, __func__);
		return DM_EQUERY;
	}

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT name FROM %smailboxes WHERE mailbox_idnr = '%llu'",DBPFX,
		 mailbox_idnr);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not retrieve name",
		      __FILE__, __func__);
		return DM_EQUERY;
	}

	if (db_num_rows() < 1) {
		db_free_result();
		*name = '\0';
		return DM_SUCCESS;
	}

	query_result = db_get_result(0, 0);
	if (!query_result) {
		/* empty set, mailbox does not exist */
		db_free_result();
		*name = '\0';
		return DM_SUCCESS;
	}
	tmp_name = dm_strdup(query_result);
	
	db_free_result();
	tmp_fq_name = mailbox_add_namespace(tmp_name, owner_idnr, user_idnr);
	if (!tmp_fq_name) {
		trace(TRACE_ERROR, "%s,%s: error getting fully qualified "
		      "mailbox name", __FILE__, __func__);
		return DM_EQUERY;
	}
	tmp_fq_name_len = strlen(tmp_fq_name);
	if (tmp_fq_name_len >= IMAP_MAX_MAILBOX_NAMELEN)
		tmp_fq_name_len = IMAP_MAX_MAILBOX_NAMELEN - 1;
	strncpy(name, tmp_fq_name, tmp_fq_name_len);
	name[tmp_fq_name_len] = '\0';
	dm_free(tmp_name);
	g_free(tmp_fq_name);
	return DM_SUCCESS;
}

int db_setmailboxname(u64_t mailbox_idnr, const char *name)
{
	snprintf(query, DEF_QUERYSIZE,
		 "UPDATE %smailboxes SET name = '%s' "
		 "WHERE mailbox_idnr = '%llu'",DBPFX, name, mailbox_idnr);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not set name", __FILE__,
		      __func__);
		return DM_EQUERY;
	}

	return DM_SUCCESS;
}

int db_expunge(u64_t mailbox_idnr, u64_t user_idnr,
	       u64_t ** msg_idnrs, u64_t * nmsgs)
{
	u64_t i;
	u64_t mailbox_size;

	if (db_get_mailbox_size(mailbox_idnr, 1, &mailbox_size) < 0) {
		trace(TRACE_ERROR, "%s,%s: error getting mailbox size "
		      "for mailbox [%llu]",
		      __FILE__, __func__, mailbox_idnr);
		return DM_EQUERY;
	}

	if (nmsgs && msg_idnrs) {


		/* first select msg UIDs */
		snprintf(query, DEF_QUERYSIZE,
			 "SELECT message_idnr FROM %smessages WHERE "
			 "mailbox_idnr = '%llu' AND deleted_flag='1' "
			 "AND status < '%d' "
			 "ORDER BY message_idnr DESC",DBPFX, mailbox_idnr,
			 MESSAGE_STATUS_DELETE);

		if (db_query(query) == -1) {

			trace(TRACE_ERROR,
			      "%s,%s: could not select messages in mailbox",
			      __FILE__, __func__);
			return DM_EQUERY;
		}

		/* now alloc mem */
		*nmsgs = db_num_rows();
		*msg_idnrs = (u64_t *) dm_malloc(sizeof(u64_t) * (*nmsgs));
		if (!(*msg_idnrs)) {
			/* out of mem */
			*nmsgs = 0;
			db_free_result();
			return DM_EQUERY;
		}

		/* save ID's in array */
		for (i = 0; i < *nmsgs; i++) {
			(*msg_idnrs)[i] = db_get_result_u64(i, 0);
		}
		db_free_result();
	}

	/* update messages belonging to this mailbox: 
	 * mark as expunged (status MESSAGE_STATUS_DELETE) */
	snprintf(query, DEF_QUERYSIZE,
		 "UPDATE %smessages SET status='%d' "
		 "WHERE mailbox_idnr = '%llu' "
		 "AND deleted_flag='1' AND status < '%d'",DBPFX, 
		 MESSAGE_STATUS_DELETE, mailbox_idnr,
		 MESSAGE_STATUS_DELETE);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: could not update messages in mailbox",
		      __FILE__, __func__);
		if (msg_idnrs)
			dm_free(*msg_idnrs);

		if (nmsgs)
			*nmsgs = 0;

		return DM_EQUERY;
	}

	if (db_subtract_quotum_used(user_idnr, mailbox_size) < 0) {
		trace(TRACE_ERROR,
		      "%s,%s: error decreasing used quotum for "
		      "user [%llu]. Database might be inconsistent now",
		      __FILE__, __func__, user_idnr);
		return DM_EQUERY;
	}

	return DM_SUCCESS;		/* success */
}

u64_t db_first_unseen(u64_t mailbox_idnr)
{
	u64_t id;

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT MIN(message_idnr) FROM %smessages "
		 "WHERE mailbox_idnr = '%llu' "
		 "AND status < '%d' AND seen_flag = '0'",DBPFX,
		 mailbox_idnr, MESSAGE_STATUS_DELETE);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not select messages",
		      __FILE__, __func__);
		return (u64_t) (-1);
	}

	id = db_get_result_u64(0, 0);

	db_free_result();
	return id;
}

int db_subscribe(u64_t mailbox_idnr, u64_t user_idnr)
{
	snprintf(query, DEF_QUERYSIZE,
		 "SELECT * FROM %ssubscription "
		 "WHERE mailbox_id = '%llu' "
		 "AND user_id = '%llu'",DBPFX, mailbox_idnr, user_idnr);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not verify subscription",
		      __FILE__, __func__);
		return (-1);
	}

	if (db_num_rows() > 0) {
		trace(TRACE_DEBUG, "%s,%s: already subscribed to mailbox "
		      "[%llu]", __FILE__, __func__, mailbox_idnr);
		db_free_result();
		return DM_SUCCESS;
	}

	db_free_result();

	snprintf(query, DEF_QUERYSIZE,
		 "INSERT INTO %ssubscription (user_id, mailbox_id) "
		 "VALUES ('%llu', '%llu')",DBPFX, user_idnr, mailbox_idnr);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not insert subscription",
		      __FILE__, __func__);
		return DM_EQUERY;
	}

	return DM_SUCCESS;
}

int db_unsubscribe(u64_t mailbox_idnr, u64_t user_idnr)
{
	snprintf(query, DEF_QUERYSIZE,
		 "DELETE FROM %ssubscription "
		 "WHERE user_id = '%llu' AND mailbox_id = '%llu'",DBPFX,
		 user_idnr, mailbox_idnr);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not update mailbox",
		      __FILE__, __func__);
		return (-1);
	}
	return DM_SUCCESS;
}

int db_get_msgflag(const char *flag_name, u64_t msg_idnr,
		   u64_t mailbox_idnr)
{
	char the_flag_name[DEF_QUERYSIZE / 2];	/* should be sufficient ;) */
	int val;

	/* determine flag */
	if (strcasecmp(flag_name, "seen") == 0)
		snprintf(the_flag_name, DEF_QUERYSIZE / 2, "seen_flag");
	else if (strcasecmp(flag_name, "deleted") == 0)
		snprintf(the_flag_name, DEF_QUERYSIZE / 2, "deleted_flag");
	else if (strcasecmp(flag_name, "answered") == 0)
		snprintf(the_flag_name, DEF_QUERYSIZE / 2,
			 "answered_flag");
	else if (strcasecmp(flag_name, "flagged") == 0)
		snprintf(the_flag_name, DEF_QUERYSIZE / 2, "flagged_flag");
	else if (strcasecmp(flag_name, "recent") == 0)
		snprintf(the_flag_name, DEF_QUERYSIZE / 2, "recent_flag");
	else if (strcasecmp(flag_name, "draft") == 0)
		snprintf(the_flag_name, DEF_QUERYSIZE / 2, "draft_flag");
	else
		return DM_SUCCESS;	/* non-existent flag is not set */

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT %s FROM %smessages "
		 "WHERE message_idnr = '%llu' AND status < '%d' "
		 "AND mailbox_idnr = '%llu'",
		 the_flag_name, DBPFX, msg_idnr, 
		 MESSAGE_STATUS_DELETE, mailbox_idnr);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not select message",
		      __FILE__, __func__);
		return (-1);
	}

	val = db_get_result_int(0, 0);

	db_free_result();
	return val;
}

int db_get_msgflag_all(u64_t msg_idnr, u64_t mailbox_idnr, int *flags)
{
	int i;

	memset(flags, 0, sizeof(int) * IMAP_NFLAGS);

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT seen_flag, answered_flag, deleted_flag, "
		 "flagged_flag, draft_flag, recent_flag FROM %smessages "
		 "WHERE message_idnr = '%llu' AND status < '%d' "
		 "AND mailbox_idnr = '%llu'",DBPFX, msg_idnr, MESSAGE_STATUS_DELETE,
		 mailbox_idnr);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not select message",
		      __FILE__, __func__);
		return (-1);
	}

	if (db_num_rows() > 0) {
		for (i = 0; i < IMAP_NFLAGS; i++) {
			flags[i] = db_get_result_bool(0, i);
		}
	}
	db_free_result();
	return DM_SUCCESS;
}

int db_set_msgflag(u64_t msg_idnr, u64_t mailbox_idnr,
		   int *flags, int action_type)
{
	/* we're lazy.. just call db_set_msgflag_range with range
	 * msg_idnr to msg_idnr! */

	if (db_set_msgflag_range(msg_idnr, msg_idnr, mailbox_idnr,
				 flags, action_type) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not set message flags",
		      __FILE__, __func__);
		return DM_EQUERY;
	}

	return DM_SUCCESS;

}

	
int db_set_msgflag_range(u64_t msg_idnr_low, u64_t msg_idnr_high,
			 u64_t mailbox_idnr, int *flags, int action_type)
{
	size_t i;
	size_t placed = 0;
	size_t left;

	snprintf(query, DEF_QUERYSIZE, "UPDATE %smessages SET ",DBPFX);

	for (i = 0; i < IMAP_NFLAGS; i++) {
		left = DEF_QUERYSIZE - strlen(query);
		switch (action_type) {
		case IMAPFA_ADD:
			if (flags[i] > 0) {
				strncat(query, db_flag_desc[i], left);
				left = DEF_QUERYSIZE - strlen(query);
				strncat(query, "=1,", left);
				placed = 1;
			}
			break;
		case IMAPFA_REMOVE:
			if (flags[i] > 0) {
				strncat(query, db_flag_desc[i], left);
				left = DEF_QUERYSIZE - strlen(query);
				strncat(query, "=0,", left);
				placed = 1;
			}
			break;

		case IMAPFA_REPLACE:
			strncat(query, db_flag_desc[i], left);
			left = DEF_QUERYSIZE - strlen(query);
			if (flags[i] == 0)
				strncat(query, "=0,", left);
			else
				strncat(query, "=1,", left);
			placed = 1;
			break;
		}
		db_free_result();
	}

	if (!placed)
		return DM_SUCCESS;	/* nothing to update */

	/* last character in string is comma, replace it --> strlen()-1 */
	left = DEF_QUERYSIZE - strlen(query);
	snprintf(&query[strlen(query) - 1], left,
		 " WHERE message_idnr BETWEEN '%llu' AND '%llu' AND "
		 "status < '%d' AND mailbox_idnr = '%llu'",
		 msg_idnr_low, msg_idnr_high, MESSAGE_STATUS_DELETE, 
		 mailbox_idnr);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not set flags",
		      __FILE__, __func__);
		return (-1);
	}

	return DM_SUCCESS;
}

int db_set_msgflag_recent(u64_t msg_idnr, u64_t mailbox_idnr)
{
	return db_set_msgflag_recent_range(msg_idnr, msg_idnr, mailbox_idnr);
}

int db_set_msgflag_recent_range(u64_t msg_idnr_lo, u64_t msg_idnr_hi, u64_t mailbox_idnr)
{
	GString *query = g_string_new("");
	g_string_printf(query, "UPDATE %smessages SET recent_flag=0 WHERE "
			" WHERE message_idnr BETWEEN '%llu' AND '%llu' AND "
			"status < '%d' AND mailbox_idnr = '%llu'",
			DBPFX, msg_idnr_lo, msg_idnr_hi, MESSAGE_STATUS_DELETE, mailbox_idnr);
	if (db_query(query->str) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not update recent_flag",__FILE__,__func__);
		g_string_free(query,1);
		return DM_EQUERY;
	}
	g_string_free(query,1);
	return DM_SUCCESS;
}

int db_get_msgdate(u64_t mailbox_idnr, u64_t msg_idnr, char *date)
{
	const char *query_result;
	char *to_char_str;

	to_char_str = date2char_str("pm.internal_date");
	snprintf(query, DEF_QUERYSIZE,
		 "SELECT %s FROM %sphysmessage pm, %smessages msg "
		 "WHERE msg.mailbox_idnr = '%llu' "
		 "AND msg.message_idnr = '%llu' "
		 "AND pm.id = msg.physmessage_id",
		 to_char_str, DBPFX, DBPFX,
		 mailbox_idnr, msg_idnr);
	dm_free(to_char_str);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not get message",
		      __FILE__, __func__);
		return (-1);
	}

	if ((db_num_rows() > 0) && (query_result = db_get_result(0, 0))) {
		strncpy(date, query_result, IMAP_INTERNALDATE_LEN);
		date[IMAP_INTERNALDATE_LEN - 1] = '\0';
	} else {
		/* no date ? let's say 1 jan 1970 */
		strncpy(date, "1970-01-01 00:00:01",
			IMAP_INTERNALDATE_LEN);
		date[IMAP_INTERNALDATE_LEN - 1] = '\0';
	}

	db_free_result();
	return DM_SUCCESS;
}

int db_set_rfcsize(u64_t rfcsize, u64_t msg_idnr, u64_t mailbox_idnr)
{
	u64_t physmessage_id = 0;

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT physmessage_id FROM %smessages "
		 "WHERE message_idnr = '%llu' "
		 "AND mailbox_idnr = '%llu'",DBPFX, msg_idnr, mailbox_idnr);
	if (db_query(query) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: could not get physmessage_id for "
		      "message [%llu]", __FILE__, __func__, msg_idnr);
		return DM_EQUERY;
	}

	if (db_num_rows() == 0) {
		trace(TRACE_DEBUG, "%s,%s: no such message [%llu]",
		      __FILE__, __func__, msg_idnr);
		db_free_result();
		return DM_SUCCESS;
	}

	physmessage_id = db_get_result_u64(0, 0);
	db_free_result();

	snprintf(query, DEF_QUERYSIZE,
		 "UPDATE %sphysmessage SET rfcsize = '%llu' "
		 "WHERE id = '%llu'",DBPFX, rfcsize, physmessage_id);
	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not update  "
		      "message [%llu]", __FILE__, __func__, msg_idnr);
		return DM_EQUERY;
	}

	return DM_SUCCESS;
}

int db_get_rfcsize(u64_t msg_idnr, u64_t mailbox_idnr, u64_t * rfc_size)
{
	assert(rfc_size != NULL);
	*rfc_size = 0;

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT pm.rfcsize FROM %sphysmessage pm, %smessages msg "
		 "WHERE pm.id = msg.physmessage_id "
		 "AND msg.message_idnr = '%llu' "
		 "AND msg.status< '%d' "
		 "AND msg.mailbox_idnr = '%llu'",DBPFX,DBPFX, msg_idnr, MESSAGE_STATUS_DELETE,
		 mailbox_idnr);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: could not fetch RFC size from table",
		      __FILE__, __func__);
		return DM_EQUERY;
	}

	if (db_num_rows() < 1) {
		trace(TRACE_ERROR, "%s,%s: message not found",
		      __FILE__, __func__);
		db_free_result();
		return DM_EQUERY;
	}

	*rfc_size = db_get_result_u64(0, 0);

	db_free_result();
	return DM_EGENERAL;
}

int db_get_main_header(u64_t msg_idnr, struct dm_list *hdrlist)
{
	const char *query_result;
	int result;

	if (!hdrlist)
		return DM_SUCCESS;

	if (hdrlist->start)
		dm_list_free(&hdrlist->start);

	dm_list_init(hdrlist);

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT messageblk "
		 "FROM %smessageblks blk, %smessages msg "
		 "WHERE blk.physmessage_id = msg.physmessage_id "
		 "AND msg.message_idnr = '%llu' "
		 "ORDER BY blk.messageblk_idnr ASC",DBPFX,DBPFX, msg_idnr);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not get message header",
		      __FILE__, __func__);
		return DM_EQUERY;
	}

	if (db_num_rows() > 0) {
		query_result = db_get_result(0, 0);
		if (!query_result) {
			trace(TRACE_ERROR,
			      "%s,%s: no header for message found",
			      __FILE__, __func__);
			db_free_result();
			return DM_EQUERY;
		}
	} else {
		trace(TRACE_ERROR,
		      "%s,%s: no message blocks found for message",
		      __FILE__, __func__);
		db_free_result();
		return DM_EQUERY;
	}

	result = mime_fetch_headers(query_result, hdrlist);

	db_free_result();

	if (result == -1) {
		/* parse error */
		trace(TRACE_ERROR,
		      "%s,%s: error parsing header of message [%llu]",
		      __FILE__, __func__, msg_idnr);
		if (hdrlist->start) {
			dm_list_free(&hdrlist->start);
			dm_list_init(hdrlist);
		}
		return -3;
	}

	if (result == -2) {
		/* out of memory */
		trace(TRACE_ERROR, "%s,%s: out of memory", __FILE__,
		      __func__);
		if (hdrlist->start) {
			dm_list_free(&hdrlist->start);
			dm_list_init(hdrlist);
		}
		return -2;
	}

	/* success ! */
	return DM_SUCCESS;
}

int db_mailbox_msg_match(u64_t mailbox_idnr, u64_t msg_idnr)
{
	int val;

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT message_idnr FROM %smessages "
		 "WHERE message_idnr = '%llu' "
		 "AND mailbox_idnr = '%llu' "
		 "AND status< '%d'",DBPFX, msg_idnr,
		 mailbox_idnr, MESSAGE_STATUS_DELETE);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not get message",
		      __FILE__, __func__);
		return (-1);
	}

	val = db_num_rows();
	db_free_result();
	return val;
}

int db_acl_has_right(u64_t userid, u64_t mboxid, const char *right_flag)
{
	int result;
	int owner_result;

	trace(TRACE_DEBUG, "%s,%s: checking ACL for user [%llu] on "
	      "mailbox [%llu]", __FILE__, __func__, userid, mboxid);
	owner_result = db_user_is_mailbox_owner(userid, mboxid);

	if (owner_result < 0) {
		trace(TRACE_ERROR,
		      "%s,%s: error checking mailbox ownership.", __FILE__,
		      __func__);
		return DM_EQUERY;
	}

	if (owner_result == 1)
		return DM_EGENERAL;

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT * FROM %sacl "
		 "WHERE user_id = '%llu' "
		 "AND mailbox_id = '%llu' "
		 "AND %s = '1'",DBPFX, userid, mboxid, right_flag);

	if (db_query(query) < 0) {
		trace(TRACE_ERROR, "%s,%s: error finding acl_right",
		      __FILE__, __func__);
		return DM_EQUERY;
	}

	if (db_num_rows() == 0)
		result = 0;
	else
		result = 1;

	db_free_result();
	return result;
}

static int db_acl_has_acl(u64_t userid, u64_t mboxid)
{
	int result;

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT user_id, mailbox_id FROM %sacl "
		 "WHERE user_id = '%llu' AND mailbox_id = '%llu'",DBPFX,
		 userid, mboxid);

	if (db_query(query) < 0) {
		trace(TRACE_ERROR, "%s,%s: Error finding ACL entry",
		      __FILE__, __func__);
		return DM_EQUERY;
	}

	if (db_num_rows() == 0)
		result = 0;
	else
		result = 1;

	db_free_result();
	return result;
}

static int db_acl_create_acl(u64_t userid, u64_t mboxid)
{
	snprintf(query, DEF_QUERYSIZE,
		 "INSERT INTO %sacl (user_id, mailbox_id) "
		 "VALUES ('%llu', '%llu')",DBPFX, userid, mboxid);

	if (db_query(query) < 0) {
		trace(TRACE_ERROR,
		      "%s,%s: Error creating ACL entry for user "
		      "[%llu], mailbox [%llu].", __FILE__, __func__,
		      userid, mboxid);
		return DM_EQUERY;
	}

	return DM_EGENERAL;
}

int db_acl_set_right(u64_t userid, u64_t mboxid, const char *right_flag,
		     int set)
{
	int owner_result;
	int result;

	assert(set == 0 || set == 1);

	trace(TRACE_DEBUG, "%s, %s: Setting ACL for user [%llu], mailbox "
	      "[%llu].", __FILE__, __func__, userid, mboxid);

	owner_result = db_user_is_mailbox_owner(userid, mboxid);
	if (owner_result < 0) {
		trace(TRACE_ERROR, "%s,%s: error checking ownership of "
		      "mailbox.", __FILE__, __func__);
		return DM_EQUERY;
	}
	if (owner_result == 1)
		return DM_SUCCESS;

	// if necessary, create ACL for user, mailbox
	result = db_acl_has_acl(userid, mboxid);
	if (result == -1) {
		trace(TRACE_ERROR, "%s,%s: Error finding acl for user "
		      "[%llu], mailbox [%llu]",
		      __FILE__, __func__, userid, mboxid);
		return DM_EQUERY;
	}

	if (result == 0) {
		if (db_acl_create_acl(userid, mboxid) == -1) {
			trace(TRACE_ERROR, "%s,%s: Error creating ACL for "
			      "user [%llu], mailbox [%llu]",
			      __FILE__, __func__, userid, mboxid);
			return DM_EQUERY;
		}
	}

	snprintf(query, DEF_QUERYSIZE,
		 "UPDATE %sacl SET %s = '%i' "
		 "WHERE user_id = '%llu' AND mailbox_id = '%llu'",DBPFX,
		 right_flag, set, userid, mboxid);

	if (db_query(query) < 0) {
		trace(TRACE_ERROR, "%s,%s: Error updating ACL for user "
		      "[%llu], mailbox [%llu].", __FILE__, __func__,
		      userid, mboxid);
		return DM_EQUERY;
	}
	trace(TRACE_DEBUG, "%s,%s: Updated ACL for user [%llu], "
	      "mailbox [%llu].", __FILE__, __func__, userid, mboxid);
	return DM_EGENERAL;
}

int db_acl_delete_acl(u64_t userid, u64_t mboxid)
{
	trace(TRACE_DEBUG, "%s,%s: deleting ACL for user [%llu], "
	      "mailbox [%llu].", __FILE__, __func__, userid, mboxid);

	snprintf(query, DEF_QUERYSIZE,
		 "DELETE FROM %sacl "
		 "WHERE user_id = '%llu' AND mailbox_id = '%llu'",DBPFX,
		 userid, mboxid);

	if (db_query(query) < 0) {
		trace(TRACE_ERROR, "%s,%s: error deleting ACL",
		      __FILE__, __func__);
		return DM_EQUERY;
	}

	return DM_EGENERAL;
}

int db_acl_get_identifier(u64_t mboxid, struct dm_list *identifier_list)
{
	unsigned i, n;
	const char *result_string;

	assert(identifier_list != NULL);

	dm_list_init(identifier_list);

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT %susers.userid FROM %susers, %sacl "
		 "WHERE %sacl.mailbox_id = '%llu' "
		 "AND %susers.user_idnr = %sacl.user_id",DBPFX,DBPFX,DBPFX,
		DBPFX,mboxid,DBPFX,DBPFX);

	if (db_query(query) < 0) {
		trace(TRACE_ERROR, "%s,%s: error getting acl identifiers "
		      "for mailbox [%llu].", __FILE__, __func__,
		      mboxid);
		return DM_EQUERY;
	}

	n = db_num_rows();
	for (i = 0; i < n; i++) {
		result_string = db_get_result(i, 0);
		trace(TRACE_DEBUG, "%s,%s: adding %s to identifier list",
		      __FILE__, __func__, result_string);
		if (!result_string || !dm_list_nodeadd(identifier_list,
						    result_string,
						    strlen(result_string) +
						    1)) {
			db_free_result();
			return -2;
		}
	}
	db_free_result();
	return DM_EGENERAL;
}

int db_get_mailbox_owner(u64_t mboxid, u64_t * owner_id)
{
	assert(owner_id != NULL);

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT owner_idnr FROM %smailboxes "
		 "WHERE mailbox_idnr = '%llu'", DBPFX, mboxid);

	if (db_query(query) < 0) {
		trace(TRACE_ERROR, "%s,%s: error finding owner of mailbox "
		      "[%llu]", __FILE__, __func__, mboxid);
		return DM_EQUERY;
	}

	*owner_id = db_get_result_u64(0, 0);
	db_free_result();
	if (*owner_id == 0)
		return DM_SUCCESS;
	else
		return DM_EGENERAL;
}

int db_user_is_mailbox_owner(u64_t userid, u64_t mboxid)
{
	int result;

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT mailbox_idnr FROM %smailboxes "
		 "WHERE mailbox_idnr = '%llu' "
		 "AND owner_idnr = '%llu'", DBPFX, mboxid, userid);

	if (db_query(query) < 0) {
		trace(TRACE_ERROR,
		      "%s,%s: error checking if user [%llu] is "
		      "owner of mailbox [%llu]", __FILE__, __func__,
		      userid, mboxid);
		return DM_EQUERY;
	}

	if (db_num_rows() == 0)
		result = 0;
	else
		result = 1;

	db_free_result();
	return result;
}

/* db_get_result_* Utility Function Annex.
 * These are variants of the db_get_result function that
 * appears in the low level database driver. Each of these
 * encapsulates some value checking and type conversion. */

int db_get_result_int(unsigned row, unsigned field)
{
	const char *tmp;
	tmp = db_get_result(row, field);
	return (tmp ? atoi(tmp) : 0);
}

int db_get_result_bool(unsigned row, unsigned field)
{
	const char *tmp;
	tmp = db_get_result(row, field);
	return (tmp ? (atoi(tmp) ? 1 : 0) : 0);
}

u64_t db_get_result_u64(unsigned row, unsigned field)
{
	const char *tmp;
	tmp = db_get_result(row, field);
	return (tmp ? strtoull(tmp, NULL, 10) : 0);
}

char *date2char_str(const char *column)
{
	unsigned len;
	char *s;
	len = strlen(TO_CHAR) + MAX_COLUMN_LEN;

	s = (char *) dm_malloc(len);
	if (!s)
		return NULL;

	snprintf(s, len, TO_CHAR, column);

	return s;
}

char *char2date_str(const char *date)
{
	unsigned len;
	char *s;

	len = strlen(TO_CHAR) + MAX_DATE_LEN;

	s = (char *) dm_malloc(len);
	if (!s)
		return NULL;

	snprintf(s, len, TO_DATE, date);

	return s;
}

int user_idnr_is_delivery_user_idnr(u64_t user_idnr)
{
	static int delivery_user_idnr_looked_up = 0;
	static u64_t delivery_user_idnr;

	if (delivery_user_idnr_looked_up == 0) {
		trace(TRACE_DEBUG, "%s.%s: looking up user_idnr for %s",
		      __FILE__, __func__, DBMAIL_DELIVERY_USERNAME);
		if (auth_user_exists(DBMAIL_DELIVERY_USERNAME,
				     &delivery_user_idnr) < 0) {
			trace(TRACE_ERROR, "%s,%s: error looking up "
			      "user_idnr for DBMAIL_DELIVERY_USERNAME",
			      __FILE__, __func__);
			return DM_EQUERY;
		}
		delivery_user_idnr_looked_up = 1;
	} else 
		trace(TRACE_DEBUG, "%s.%s: no need to look up user_idnr "
		      "for %s",
		      __FILE__, __func__, DBMAIL_DELIVERY_USERNAME);
	
	if (delivery_user_idnr == user_idnr)
		return DM_EGENERAL;
	else
		return DM_SUCCESS;
}

void convert_inbox_to_uppercase(char *name)
{
	const char *inbox = "INBOX";
	
	if (strncasecmp(name, "INBOX", strlen("INBOX")) != 0)
		return;
	
	if (strlen(name) == strlen("INBOX") ||
	    strncasecmp(name, "INBOX/", strlen("INBOX/")) == 0)
		memcpy((void *) name, (void *) inbox, strlen(inbox));
	return;
}

int db_getmailbox_list_result(u64_t mailbox_idnr, u64_t user_idnr, mailbox_t * mb)
{
	/* query mailbox for LIST results */
	const char *query_result;
	char *mbxname;
	GString *fqname;
	int i=0;

	snprintf(query, DEF_QUERYSIZE,
		 "SELECT owner_idnr, name, no_select, no_inferiors "
		 "FROM %smailboxes WHERE mailbox_idnr = '%llu'",
		 DBPFX, mailbox_idnr);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: db error", __FILE__, __func__);
		return DM_EQUERY;
	}

	if (db_num_rows() == 0) {
		db_free_result();
		return DM_SUCCESS;
	}
	/* owner_idnr */
	query_result=db_get_result(0,i++);
	mb->owner_idnr=strtol(query_result,NULL,10);
	
	/* name */
	query_result=db_get_result(0,i++);
	mbxname = mailbox_add_namespace(query_result, mb->owner_idnr, user_idnr);
	fqname = g_string_new(mbxname);
	fqname = g_string_truncate(fqname,IMAP_MAX_MAILBOX_NAMELEN);
	mb->name = fqname->str;
	g_string_free(fqname,FALSE);
	g_free(mbxname);

	/* no_select */
	query_result=db_get_result(0,i++);
	mb->no_select=strtol(query_result,NULL,1);

	/* no_inferior */
	query_result=db_get_result(0,i++);
	mb->no_inferiors=strtol(query_result,NULL,1);

	db_free_result();
	return DM_SUCCESS;
}

int db_usermap_resolve(clientinfo_t *ci, const char *username, char *real_username)
{
	struct sockaddr saddr;
	sa_family_t sa_family;
	char clientsock[DM_SOCKADDR_LEN];
	char * escaped_username;
	const char *userid = NULL, *sockok = NULL, *sockno = NULL, *login = NULL;
	unsigned row, bestrow = 0;
	int result;
	int score, bestscore = -1;
	
	trace (TRACE_DEBUG,"%s,%s: checking userid '%s' in usermap", 
			__FILE__, __func__, username);
	
	if (ci==NULL) {
		strncpy(clientsock,"",1);
	} else {
		/* get the socket the client is connecting on */
		sa_family = dm_get_client_sockaddr(ci, &saddr);
		if (sa_family == AF_INET) {
			snprintf(clientsock, DM_SOCKADDR_LEN, "inet:%s:%d", 
					inet_ntoa(((struct sockaddr_in *)(&saddr))->sin_addr),
					ntohs(((struct sockaddr_in *)(&saddr))->sin_port));
			trace(TRACE_DEBUG,"%s,%s: client on inet socket [%s]", __FILE__, __func__, clientsock);
		}	
		if (sa_family == AF_UNIX) {
			snprintf(clientsock, DM_SOCKADDR_LEN, "unix:%s",
					((struct sockaddr_un *)(&saddr))->sun_path);
			trace(TRACE_DEBUG,"%s,%s: client on unix socket [%s]", __FILE__, __func__, clientsock);
		}		
	}

	escaped_username = dm_stresc(username);
	
	/* user_idnr not found, so try to get it from the usermap */
	snprintf(query, DEF_QUERYSIZE, "SELECT login, sock_allow, sock_deny, userid FROM %susermap "
			"WHERE login in ('%s','ANY') "
			"ORDER BY sock_allow, sock_deny", 
			DBPFX, escaped_username);

	dm_free(escaped_username);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not select usermap",
				__FILE__, __func__);
		return DM_EQUERY;
	}

	if (db_num_rows() == 0) {
		/* user does not exist */
		trace (TRACE_DEBUG,"%s,%s: login '%s' not found in usermap", 
				__FILE__, __func__, username);
		db_free_result();
		return DM_SUCCESS;
	}

	/* find the best match on the usermap table */
	for (row=0; row < db_num_rows(); row++) {
		login = db_get_result(row, 0);
		sockok = db_get_result(row, 1);
		sockno = db_get_result(row, 2);
		userid = db_get_result(row, 3);
		result = dm_sock_compare(clientsock, sockok, sockno);
		/* any match on sockno will be fatal */
		if (result) {
			db_free_result();
			return result;
		}
		score = dm_sock_score(clientsock, sockok);
		if (score > bestscore) {
			bestrow = row;
			bestscore = score;
		}
	}
	if (bestscore < 0)
		return DM_EGENERAL;
	
	/* use the best matching sockok */
	login = db_get_result(bestrow, 0);
	userid = db_get_result(bestrow, 3);

	if ((strncmp(login,"ANY",3)==0)) {
		if (dm_valid_format(userid)==0)
			snprintf(real_username,DM_USERNAME_LEN,userid,username);
		else
			return DM_EQUERY;
	} else {
		strncpy(real_username, userid, DM_USERNAME_LEN);
	}
	
	trace (TRACE_DEBUG,"%s,%s: '%s' maps to '%s'", __FILE__, __func__, username, real_username);
	db_free_result();

	return DM_SUCCESS;

}
int db_user_exists(const char *username, u64_t * user_idnr) 
{
	const char *query_result;
	char *escaped_username;

	assert(user_idnr != NULL);
	*user_idnr = 0;
	if (!username) {
		trace(TRACE_ERROR, "%s,%s: got NULL as username",
		      __FILE__, __func__);
		return 0;
		
	}
	
	if (! (escaped_username = dm_stresc(username)))
		return DM_EQUERY;
	
	snprintf(query, DEF_QUERYSIZE,
		 "SELECT user_idnr FROM %susers WHERE userid='%s'",DBPFX,
		 escaped_username);
	
	dm_free(escaped_username);
	
	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not select user information",
		      __FILE__, __func__);
		return DM_EQUERY;
	}

	if (db_num_rows() > 0) {
		query_result = db_get_result(0, 0);
		*user_idnr = (query_result) ? strtoull(query_result, 0, 10) : 0;
		db_free_result();
		return 1;
	}
		
	return 0;
	
}

int db_user_create_shadow(const char *username, u64_t * user_idnr)
{
	return db_user_create(username, "UNUSED", "UNUSED", 0xffff, 0xffff, user_idnr);
}

int db_user_create(const char *username, const char *password, const char *enctype,
		 u64_t clientid, u64_t maxmail, u64_t * user_idnr) 
{
	char escapedpass[DEF_QUERYSIZE];
	char *escaped_username;

	assert(user_idnr != NULL);
//	*user_idnr = 0;

#ifdef _DBAUTH_STRICT_USER_CHECK
	if (!(escaped_username = (char *) dm_malloc(strlen(username) * 2 + 1))) {
		trace(TRACE_ERROR, "%s,%s: out of memory allocating "
			"escaped username", __FILE__, __func__);
		return DM_EQUERY;
	}

	db_escape_string(escaped_username, username, strlen(username));
	/* first check to see if this user already exists */
	snprintf(query, DEF_QUERYSIZE,
		 "SELECT * FROM %susers WHERE userid = '%s'",DBPFX, escaped_username);
	dm_free(escaped_username);

	if (db_query(query) == -1) {
		/* query failed */
		trace(TRACE_ERROR, "%s,%s: query failed",
		      __FILE__, __func__);
		return DM_EQUERY;
	}

	if (db_num_rows() > 0) {
		/* this username already exists */
		trace(TRACE_ERROR, "%s,%s: user already exists",
		      __FILE__, __func__);
		db_free_result();
		return DM_EQUERY;
	}

	db_free_result();
#endif

	if (strlen(password) >= DEF_QUERYSIZE) {
		trace(TRACE_ERROR, "%s,%s: password length is insane",
		      __FILE__, __func__);
		return DM_EQUERY;
	}

	db_escape_string(escapedpass, password, strlen(password));

	if (!(escaped_username = (char *) dm_malloc(strlen(username) * 2 + 1))) {
		trace(TRACE_ERROR, "%s,%s: out of memory allocating "
			"escaped username", __FILE__, __func__);
		return DM_EQUERY;
	}

	db_escape_string(escaped_username, username, strlen(username));

	if (*user_idnr==0) {
		snprintf(query, DEF_QUERYSIZE, "INSERT INTO %susers "
			"(userid,passwd,client_idnr,maxmail_size,"
			"encryption_type, last_login) VALUES "
			"('%s','%s',%llu,'%llu','%s', %s)",
			DBPFX, escaped_username, escapedpass, clientid, 
			maxmail, enctype ? enctype : "", SQL_CURRENT_TIMESTAMP);
	} else {
		snprintf(query, DEF_QUERYSIZE, "INSERT INTO %susers "
			"(userid,user_idnr,passwd,client_idnr,maxmail_size,"
			"encryption_type, last_login) VALUES "
			"('%s',%llu,'%s',%llu,'%llu','%s', %s)",
			DBPFX,escaped_username,*user_idnr,escapedpass,clientid, 
			maxmail, enctype ? enctype : "", SQL_CURRENT_TIMESTAMP);
	}
	dm_free(escaped_username);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: query for adding user failed",
		      __FILE__, __func__);
		return DM_EQUERY;
	}
	
	if (*user_idnr == 0)
		*user_idnr = db_insert_result("user_idnr");

	return DM_EGENERAL;
}

int db_user_delete(const char * username)
{
	char *escaped_username;

	if (!(escaped_username = (char *) dm_malloc(strlen(username) * 2 + 1))) {
		trace(TRACE_ERROR, "%s,%s: out of memory allocating "
			"escaped username", __FILE__, __func__);
		return DM_EQUERY;
	}

	db_escape_string(escaped_username, username, strlen(username));
	snprintf(query, DEF_QUERYSIZE, "DELETE FROM %susers WHERE userid = '%s'",
		 DBPFX, escaped_username);
	dm_free(escaped_username);

	if (db_query(query) == -1) {
		/* query failed */
		trace(TRACE_ERROR, "%s,%s: query for removing user failed",
		      __FILE__, __func__);
		return DM_EQUERY;
	}

	return DM_SUCCESS;
}

int db_user_rename(u64_t user_idnr, const char *new_name) 
{
	char *escaped_new_name;

	if (!(escaped_new_name = (char *) dm_malloc(strlen(new_name) * 2 + 1))) {
		trace(TRACE_ERROR, "%s,%s: out of memory allocating escaped new_name", 
				__FILE__, __func__);
		return DM_EQUERY;
	}

	db_escape_string(escaped_new_name, new_name, strlen(new_name));
	snprintf(query, DEF_QUERYSIZE, "UPDATE %susers SET userid = '%s' WHERE user_idnr='%llu'",
		 DBPFX, escaped_new_name, user_idnr);
	dm_free(escaped_new_name);

	if (db_query(query) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: could not change name for user [%llu]",
		      __FILE__, __func__, user_idnr);
		return DM_EQUERY;
	}
	return DM_SUCCESS;
}

int db_user_find_create(u64_t user_idnr)
{
	char *username;
	u64_t idnr;
	int result;

	assert(user_idnr > 0);
	
	trace(TRACE_DEBUG,"%s,%s: user_idnr [%llu]", 
			__FILE__, __func__, user_idnr);

	if ((result = user_idnr_is_delivery_user_idnr(user_idnr)))
		return result;
	
	if (! (username = auth_get_userid(user_idnr))) 
		return DM_EQUERY;
	
	trace(TRACE_DEBUG,"%s,%s: found username for user_idnr [%llu -> %s",
			__FILE__, __func__,
			user_idnr, username);
	
	if ((db_user_exists(username, &idnr) < 0))
		return DM_EQUERY;

	if ((idnr > 0) && (idnr != user_idnr)) {
		trace(TRACE_ERROR, "%s,%s: user_idnr for sql shadow account "
				"differs from user_idnr [%llu != %llu]",
				__FILE__, __func__,
				idnr, user_idnr);
		return DM_EQUERY;
	}
	
	if (idnr == user_idnr) {
		trace(TRACE_DEBUG, "%s,%s: shadow entry exists and valid",
				__FILE__, __func__);
		return DM_EGENERAL;
	}

	return db_user_create_shadow(username, &user_idnr);
}

int db_replycache_register(const char *to, const char *from)
{
	snprintf(query, DEF_QUERYSIZE,
			"SELECT lastseen FROM %sreplycache "
			"WHERE to_addr = '%s' "
			"AND from_addr = '%s'",
			DBPFX, to, from);
	if (db_query(query)== -1) {
		trace(TRACE_ERROR, "%s,%s: query failed",
				__FILE__, __func__);
		return DM_EQUERY;
	}
	
	if (db_num_rows() > 0) {
		snprintf(query, DEF_QUERYSIZE,
			 "UPDATE %sreplycache SET lastseen = %s "
			 "WHERE to_addr = '%s' AND from_addr = '%s'", DBPFX, SQL_CURRENT_TIMESTAMP,
			 to, from);
	} else {
		snprintf(query, DEF_QUERYSIZE,
			 "INSERT INTO %sreplycache (to_addr, from_addr, lastseen) "
			 "VALUES ('%s','%s', %s)", DBPFX, to, from, SQL_CURRENT_TIMESTAMP);
	}
	
	db_free_result();
	
	if (db_query(query)== -1) {
		trace(TRACE_ERROR, "%s,%s: query failed",
				__FILE__, __func__);
		return DM_EQUERY;
	}

	return DM_SUCCESS;

}

#define REPLYCACHE_TIMEOUT 3600*24*7
int db_replycache_validate(const char *to, const char *from)
{
	GString *tmp = g_string_new("");
	g_string_printf(tmp, SQL_REPLYCACHE_EXPIRE, REPLYCACHE_TIMEOUT);
	snprintf(query, DEF_QUERYSIZE,
			"SELECT lastseen FROM %sreplycache "
			"WHERE to_addr = '%s' AND from_addr = '%s' "
			"AND lastseen > (%s)", DBPFX, to, from, tmp->str);
	if (db_query(query) == -1) {
		trace(TRACE_ERROR, "%s,%s: query failed",
				__FILE__, __func__);
		return DM_EQUERY;
	}
	if (db_num_rows() > 0) {
		db_free_result();
		return DM_EGENERAL;
	}
	
	db_free_result();
	
	return DM_SUCCESS;			
}

