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
 * \file authsql.c
 * \brief implements SQL authentication. Prototypes of these functions
 * can be found in auth.h . 
 * \author IC&S (http://www.ic-s.nl)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "auth.h"
#include "db.h"
#include "list.h"
#include "debug.h"
#include "dbmd5.h"
#include "dbmail.h"
#include "misc.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifdef HAVE_CRYPT_H
#include <crypt.h>
#endif

/**
 * used for query strings
 */
#define AUTH_QUERY_SIZE 1024
static char __auth_query_data[AUTH_QUERY_SIZE];
/* string to be returned by auth_getencryption() */
#define _DESCSTRLEN 50
static char __auth_encryption_desc_string[_DESCSTRLEN];


/**
 * \brief perform a authentication query
 * \param thequery the query
 * \return
 *     - -1 on error
 *     -  0 otherwise
 */
static int __auth_query(const char *thequery);

static u64_t __auth_insert_result(const char *sequence_identifier);

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

int auth_user_exists(const char *username, u64_t * user_idnr)
{
	const char *query_result;

	assert(user_idnr != NULL);
	*user_idnr = 0;
	if (!username) {
		trace(TRACE_ERROR, "%s,%s: got NULL as username",
		      __FILE__, __FUNCTION__);
		return 0;
	}

	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "SELECT user_idnr FROM users WHERE userid='%s'",
		 username);

	if (__auth_query(__auth_query_data) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not execute query",
		      __FILE__, __FUNCTION__);
		return -1;
	}

	if (db_num_rows() == 0) {
		db_free_result();
		return 0;
	}

	query_result = db_get_result(0, 0);
	*user_idnr = (query_result) ? strtoull(query_result, 0, 10) : 0;
	db_free_result();
	return 1;
}

int auth_get_known_users(struct list *users)
{
	u64_t i;
	const char *query_result;

	if (!users) {
		trace(TRACE_ERROR, "%s,%s: got a NULL pointer as argument",
		      __FILE__, __FUNCTION__);
		return -2;
	}

	list_init(users);

	/* do a inverted (DESC) query because adding the names to the
	 * final list inverts again */
	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "SELECT userid FROM users ORDER BY userid DESC");

	if (__auth_query(__auth_query_data) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not retrieve user list",
		      __FILE__, __FUNCTION__);
		return -1;
	}

	if (db_num_rows() > 0) {
		for (i = 0; i < (unsigned) db_num_rows(); i++) {
			query_result = db_get_result(i, 0);
			if (!list_nodeadd
			    (users, query_result,
			     strlen(query_result) + 1)) {
				list_freelist(&users->start);
				return -2;
			}
		}
	}
	db_free_result();
	return 0;
}

int auth_getclientid(u64_t user_idnr, u64_t * client_idnr)
{
	const char *query_result;

	assert(client_idnr != NULL);
	*client_idnr = 0;

	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "SELECT client_idnr FROM users WHERE user_idnr = '%llu'",
		 user_idnr);

	if (__auth_query(__auth_query_data) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: could not retrieve client id for user [%llu]\n",
		      __FILE__, __FUNCTION__, user_idnr);
		return -1;
	}

	if (db_num_rows() == 0) {
		db_free_result();
		return 1;
	}

	query_result = db_get_result(0, 0);
	*client_idnr = (query_result) ? strtoull(query_result, 0, 10) : 0;

	db_free_result();
	return 1;
}

int auth_getmaxmailsize(u64_t user_idnr, u64_t * maxmail_size)
{
	const char *query_result;

	assert(maxmail_size != NULL);
	*maxmail_size = 0;

	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "SELECT maxmail_size FROM users WHERE user_idnr = '%llu'",
		 user_idnr);

	if (__auth_query(__auth_query_data) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: could not retrieve client id for user [%llu]",
		      __FILE__, __FUNCTION__, user_idnr);
		return -1;
	}

	if (db_num_rows() == 0) {
		db_free_result();
		return 0;
	}

	query_result = db_get_result(0, 0);
	if (query_result)
		*maxmail_size = strtoull(query_result, NULL, 10);
	else
		return -1;

	db_free_result();
	return 1;
}

char *auth_getencryption(u64_t user_idnr)
{
	const char *query_result;
	__auth_encryption_desc_string[0] = '\0';

	if (user_idnr == 0) {
		/* assume another function returned an error code (-1) 
		 * or this user does not exist (0)
		 */
		trace(TRACE_ERROR, "%s,%s: got (%lld) as userid",
		      __FILE__, __FUNCTION__, user_idnr);
		return __auth_encryption_desc_string;	/* return empty */
	}

	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "SELECT encryption_type FROM users WHERE user_idnr = '%llu'",
		 user_idnr);

	if (__auth_query(__auth_query_data) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: could not retrieve encryption type for user [%llu]",
		      __FILE__, __FUNCTION__, user_idnr);
		return __auth_encryption_desc_string;	/* return empty */
	}

	if (db_num_rows() == 0) {
		db_free_result();
		return __auth_encryption_desc_string;	/* return empty */
	}

	query_result = db_get_result(0, 0);
	strncpy(__auth_encryption_desc_string, query_result, _DESCSTRLEN);

	db_free_result();
	return __auth_encryption_desc_string;
}

int auth_check_user(const char *username, struct list *userids, int checks)
{
	int occurences = 0;
	int r;
	void *saveres;
	u64_t counter;
	unsigned num_rows;
	const char *query_result;

	trace(TRACE_DEBUG, "%s,%s: checking user [%s] in alias table",
	      __FILE__, __FUNCTION__, username);

	saveres = db_get_result_set();
	db_set_result_set(NULL);

	if (checks > MAX_CHECKS_DEPTH) {
		trace(TRACE_ERROR,
		      "%s,%s: maximum checking depth reached, "
		      "there probably is a loop in your alias table",
		      __FILE__, __FUNCTION__);
		return -1;
	}

	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "SELECT deliver_to FROM aliases WHERE "
		 "lower(alias) = lower('%s')", username);
	trace(TRACE_DEBUG, "%s,%s: checks [%d]", __FILE__, __FUNCTION__,
	      checks);

	if (__auth_query(__auth_query_data) == -1) {
		/* copy the old result set */
		db_set_result_set(saveres);
		return 0;
	}
	num_rows = db_num_rows();
	if (num_rows < 1) {
		if (checks > 0) {
			/* found the last one, this is the deliver to
			 * but checks needs to be bigger then 0 because
			 * else it could be the first query failure */
			list_nodeadd(userids, username,
				     strlen(username) + 1);
			trace(TRACE_DEBUG,
			      "%s,%s: adding [%s] to deliver_to address",
			      __FILE__, __FUNCTION__, username);
			db_free_result();
			db_set_result_set(saveres);
			return 1;
		} else {
			trace(TRACE_DEBUG,
			      "%s,%s: user %s not in aliases table",
			      __FILE__, __FUNCTION__, username);
			db_free_result();
			db_set_result_set(saveres);
			return 0;
		}
	}

	trace(TRACE_DEBUG, "%s,%s: into checking loop", __FILE__,
	      __FUNCTION__);

	if (num_rows > 0) {
		for (counter = 0; counter < num_rows; counter++) {
			/* do a recursive search for deliver_to */
			query_result = db_get_result(counter, 0);
			trace(TRACE_DEBUG, "%s,%s: checking user %s to %s",
			      __FILE__, __FUNCTION__, username,
			      query_result);

			r = auth_check_user(query_result, userids,
					    (checks < 0) ? 1 : checks + 1);
			if (r < 0) {
				/* loop detected */
				db_free_result();
				db_set_result_set(saveres);

				if (checks > 0)
					return -1;	/* still in recursive call */

				if (userids->start) {
					list_freelist(&userids->start);
					userids->total_nodes = 0;
				}

				return 0;	/* report to calling routine: no results */
			}

			occurences += r;
		}
	}

	db_free_result();
	db_set_result_set(saveres);
	return occurences;
}

int auth_check_user_ext(const char *username, struct list *userids,
			struct list *fwds, int checks)
{
	int occurences = 0;
	void *saveres;
	u64_t counter;
	const char *query_result;
	char *endptr;
	u64_t id;
	unsigned num_rows;

	saveres = db_get_result_set();
	db_set_result_set(NULL);

	trace(TRACE_DEBUG, "%s,%s: checking user [%s] in alias table",
	      __FILE__, __FUNCTION__, username);

	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "SELECT deliver_to FROM aliases "
		 "WHERE lower(alias) = lower('%s')", username);
	trace(TRACE_DEBUG, "%s,%s: checks [%d]", __FILE__, __FUNCTION__,
	      checks);

	if (__auth_query(__auth_query_data) == -1) {
		db_set_result_set(saveres);
		return 0;
	}

	num_rows = db_num_rows();
	if (num_rows == 0) {
		if (checks > 0) {
			/* found the last one, this is the deliver to
			 * but checks needs to be bigger then 0 because
			 * else it could be the first query failure */
			id = strtoull(username, &endptr, 10);

			if (*endptr == 0)
				list_nodeadd(userids, &id, sizeof(id));
			/* numeric deliver-to --> this is a userid */
			else
				list_nodeadd(fwds, username,
					     strlen(username) + 1);

			trace(TRACE_DEBUG,
			      "%s,%s: adding [%s] to deliver_to address",
			      __FILE__, __FUNCTION__, username);
			db_free_result();
			db_set_result_set(saveres);
			return 1;
		} else {
			trace(TRACE_DEBUG,
			      "%s,%s: user %s not in aliases table",
			      __FILE__, __FUNCTION__, username);
			db_free_result();
			db_set_result_set(saveres);
			return 0;
		}
	}

	trace(TRACE_DEBUG, "%s,%s: into checking loop", __FILE__,
	      __FUNCTION__);

	if (num_rows > 0) {
		for (counter = 0; counter < num_rows; counter++) {
			/* do a recursive search for deliver_to */
			query_result = db_get_result(counter, 0);
			trace(TRACE_DEBUG, "%s,%s: checking user %s to %s",
			      __FILE__, __FUNCTION__, username,
			      query_result);
			occurences +=
			    auth_check_user_ext(query_result, userids,
						fwds, 1);
		}
	}
	db_free_result();
	db_set_result_set(saveres);
	return occurences;
}

int __auth_query(const char *thequery)
{
	/* start using authentication result */
	if (db_query(thequery) < 0) {
		trace(TRACE_ERROR, "%s,%s: error executing query",
		      __FILE__, __FUNCTION__);
		return -1;
	}
	return 0;
}

int auth_adduser(char *username, char *password, char *enctype,
		 char *clientid, char *maxmail, u64_t * user_idnr)
{
	char *tst;
	u64_t size;
	char escapedpass[AUTH_QUERY_SIZE];

	assert(user_idnr != NULL);
	*user_idnr = 0;

#ifdef _DBAUTH_STRICT_USER_CHECK
	/* first check to see if this user already exists */
	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "SELECT * FROM users WHERE userid = '%s'", username);

	if (__auth_query(__auth_query_data) == -1) {
		/* query failed */
		trace(TRACE_ERROR, "%s,%s: query failed",
		      __FILE__, __FUNCTION__);
		return -1;
	}

	if (db_num_rows() > 0) {
		/* this username already exists */
		trace(TRACE_ERROR, "%s,%s: user already exists",
		      __FILE__, __FUNCTION__);
		db_free_result();
		return -1;
	}

	db_free_result();
#endif

	size = strtoull(maxmail, &tst, 10);
	if (tst) {
		if (tst[0] == 'M' || tst[0] == 'm')
			size *= 1000000;

		if (tst[0] == 'K' || tst[0] == 'k')
			size *= 1000;
	}

	if (strlen(password) >= AUTH_QUERY_SIZE) {
		trace(TRACE_ERROR, "%s,%s: password length is insane",
		      __FILE__, __FUNCTION__);
		return -1;
	}

	db_escape_string(escapedpass, password, strlen(password));

	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "INSERT INTO users "
		 "(userid,passwd,client_idnr,maxmail_size,encryption_type) VALUES "
		 "('%s','%s',%s,'%llu','%s')",
		 username, escapedpass, clientid, size,
		 enctype ? enctype : "");

	if (__auth_query(__auth_query_data) == -1) {
		/* query failed */
		trace(TRACE_ERROR, "%s,%s: query for adding user failed",
		      __FILE__, __FUNCTION__);
		return -1;
	}

	*user_idnr = __auth_insert_result("user_idnr");

	/* creating query for adding mailbox */
	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "INSERT INTO mailboxes (owner_idnr, name) "
		 "VALUES ('%llu','INBOX')", *user_idnr);

	trace(TRACE_DEBUG, "%s,%s: executing query for mailbox",
	      __FILE__, __FUNCTION__);

	if (__auth_query(__auth_query_data) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: query failed for adding mailbox", __FILE__,
		      __FUNCTION__);
		return -1;
	}

	return 1;
}

int auth_delete_user(const char *username)
{
	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "DELETE FROM users WHERE userid = '%s'", username);

	if (__auth_query(__auth_query_data) == -1) {
		/* query failed */
		trace(TRACE_ERROR, "%s,%s: query for removing user failed",
		      __FILE__, __FUNCTION__);
		return -1;
	}

	return 0;
}

int auth_change_username(u64_t user_idnr, const char *new_name)
{
	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "UPDATE users SET userid = '%s' WHERE user_idnr='%llu'",
		 new_name, user_idnr);

	if (__auth_query(__auth_query_data) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: could not change name for user [%llu]",
		      __FILE__, __FUNCTION__, user_idnr);
		return -1;
	}

	return 0;
}

int auth_change_password(u64_t user_idnr, const char *new_pass,
			 const char *enctype)
{
	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "UPDATE users SET passwd = '%s', encryption_type = '%s' "
		 " WHERE user_idnr='%llu'",
		 new_pass, enctype ? enctype : "", user_idnr);

	if (__auth_query(__auth_query_data) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: could not change passwd for user [%llu]",
		      __FILE__, __FUNCTION__, user_idnr);
		return -1;
	}

	return 0;
}

int auth_change_clientid(u64_t user_idnr, u64_t new_cid)
{
	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "UPDATE users SET client_idnr = '%llu' "
		 "WHERE user_idnr='%llu'", new_cid, user_idnr);

	if (__auth_query(__auth_query_data) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: could not change client id for user [%llu]",
		      __FILE__, __FUNCTION__, user_idnr);
		return -1;
	}

	return 0;
}

int auth_change_mailboxsize(u64_t user_idnr, u64_t new_size)
{
	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "UPDATE users SET maxmail_size = '%llu' "
		 "WHERE user_idnr = '%llu'", new_size, user_idnr);

	if (__auth_query(__auth_query_data) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: could not change maxmailsize for user [%llu]",
		      __FILE__, __FUNCTION__, user_idnr);
		return -1;
	}

	return 0;
}

int auth_validate(char *username, char *password, u64_t * user_idnr)
{
	const char *query_result;
	int is_validated = 0;
	timestring_t timestring;
	char salt[13];
	char cryptres[35];
	char *escuser;

	assert(user_idnr != NULL);
	*user_idnr = 0;

	if (username == NULL || password == NULL) {
		trace(TRACE_DEBUG, "%s,%s: username or password is NULL",
		      __FILE__, __FUNCTION__);
		return 0;
	}

	create_current_timestring(&timestring);

	/* the shared mailbox user should not log in! */
	if (strcmp(username, SHARED_MAILBOX_USERNAME) == 0)
		return 0;

	if (!(escuser = (char *) malloc(strlen(username) * 2 + 1))) {
		trace(TRACE_ERROR,
		      "%s,%s: out of memory allocating for escaped userid",
		      __FILE__, __FUNCTION__);
		return -1;
	}

	db_escape_string(escuser, username, strlen(username));

	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "SELECT user_idnr, passwd, encryption_type FROM users "
		 "WHERE userid = '%s'", escuser);

	if (__auth_query(__auth_query_data) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: could not select user information", __FILE__,
		      __FUNCTION__);
		my_free(escuser);
		return -1;
	}

	my_free(escuser);

	if (db_num_rows() == 0) {
		db_free_result();
		return 0;
	}

	/* get encryption type */
	query_result = db_get_result(0, 2);

	if (!query_result || strcasecmp(query_result, "") == 0) {
		trace(TRACE_DEBUG,
		      "%s,%s: validating using cleartext passwords",
		      __FILE__, __FUNCTION__);
		/* get password from database */
		query_result = db_get_result(0, 1);
		is_validated =
		    (strcmp(query_result, password) == 0) ? 1 : 0;
	} else if (strcasecmp(query_result, "crypt") == 0) {
		trace(TRACE_DEBUG,
		      "%s,%s: validating using crypt() encryption",
		      __FILE__, __FUNCTION__);
		query_result = db_get_result(0, 1);
		is_validated = (strcmp((const char *) crypt(password, query_result),	/* Flawfinder: ignore */
				       query_result) == 0) ? 1 : 0;
	} else if (strcasecmp(query_result, "md5") == 0) {
		/* get password */
		query_result = db_get_result(0, 1);
		if (strncmp(query_result, "$1$", 3)) {
			trace(TRACE_DEBUG,
			      "%s,%s: validating using MD5 digest comparison",
			      __FILE__, __FUNCTION__);
			/* redundant statement: query_result = db_get_result(0, 1); */
			is_validated =
			    (strncmp(makemd5(password), query_result, 32)
			     == 0) ? 1 : 0;
		} else {
			trace(TRACE_DEBUG,
			      "%s, %s: validating using MD5 hash comparison",
			      __FILE__, __FUNCTION__);
			strncpy(salt, query_result, 12);
			strncpy(cryptres, (char *) crypt(password, query_result), 34);	/* Flawfinder: ignore */

			trace(TRACE_DEBUG, "%s,%s: salt   : %s",
			      __FILE__, __FUNCTION__, salt);
			trace(TRACE_DEBUG, "%s,%s: hash   : %s",
			      __FILE__, __FUNCTION__, query_result);
			trace(TRACE_DEBUG, "%s,%s: crypt(): %s",
			      __FILE__, __FUNCTION__, cryptres);

			is_validated =
			    (strncmp(query_result, cryptres, 34) ==
			     0) ? 1 : 0;
		}
	} else if (strcasecmp(query_result, "md5sum") == 0) {
		trace(TRACE_DEBUG,
		      "%s,%s: validating using MD5 digest comparison",
		      __FILE__, __FUNCTION__);
		query_result = db_get_result(0, 1);
		is_validated =
		    (strncmp(makemd5(password), query_result, 32) ==
		     0) ? 1 : 0;
	}

	if (is_validated) {
		query_result = db_get_result(0, 0);
		*user_idnr =
		    (query_result) ? strtoull(query_result, NULL, 10) : 0;

		/* MEM LEAK: old result should be free-ed here! We can also
		 * free it in the query code... maybe a good idea? */

		/* log login in the dbase */
		snprintf(__auth_query_data, AUTH_QUERY_SIZE,
			 "UPDATE users SET last_login = '%s' "
			 "WHERE user_idnr = '%llu'", timestring,
			 *user_idnr);

		if (__auth_query(__auth_query_data) == -1)
			trace(TRACE_ERROR,
			      "%s,%s: could not update user login time",
			      __FILE__, __FUNCTION__);
	}

	db_free_result();
	return (is_validated ? 1 : 0);
}

u64_t auth_md5_validate(char *username, unsigned char *md5_apop_he,
			char *apop_stamp)
{
	/* returns useridnr on OK, 0 on validation failed, -1 on error */
	char *checkstring;
	unsigned char *md5_apop_we;
	u64_t user_idnr;
	const char *query_result;
	timestring_t timestring;

	create_current_timestring(&timestring);
	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "SELECT passwd,user_idnr FROM users WHERE "
		 "userid='%s'", username);

	if (__auth_query(__auth_query_data) == -1) {
		return -1;
	}

	if (db_num_rows() < 1) {
		/* no such user found */
		db_free_result();
		return 0;
	}

	/* now authenticate using MD5 hash comparisation  */
	query_result = db_get_result(0, 0);

	/* value holds the password */

	trace(TRACE_DEBUG, "%s,%s: apop_stamp=[%s], userpw=[%s]",
	      __FILE__, __FUNCTION__, apop_stamp, query_result);


	memtst((checkstring =
		(char *) my_malloc(strlen(apop_stamp) +
				   strlen(query_result) + 2)) == NULL);
	snprintf(checkstring,
		 strlen(apop_stamp) + strlen(query_result) + 2, "%s%s",
		 apop_stamp, query_result);

	md5_apop_we = makemd5(checkstring);

	trace(TRACE_DEBUG,
	      "%s,%s: checkstring for md5 [%s] -> result [%s]", __FILE__,
	      __FUNCTION__, checkstring, md5_apop_we);
	trace(TRACE_DEBUG,
	      "%s,%s: validating md5_apop_we=[%s] md5_apop_he=[%s]",
	      __FILE__, __FUNCTION__, md5_apop_we, md5_apop_he);

	if (strcmp(md5_apop_he, makemd5(checkstring)) == 0) {
		trace(TRACE_MESSAGE,
		      "%s,%s: user [%s] is validated using APOP", __FILE__,
		      __FUNCTION__, username);
		/* get user idnr */
		query_result = db_get_result(0, 1);
		user_idnr =
		    (query_result) ? strtoull(query_result, NULL, 10) : 0;
		db_free_result();
		my_free(checkstring);

		/* log login in the dbase */
		snprintf(__auth_query_data, AUTH_QUERY_SIZE,
			 "UPDATE users SET last_login = '%s' "
			 "WHERE user_idnr = '%llu'", timestring,
			 user_idnr);

		if (__auth_query(__auth_query_data) == -1)
			trace(TRACE_ERROR,
			      "%s,%s: could not update user login time",
			      __FILE__, __FUNCTION__);

		return user_idnr;
	}

	trace(TRACE_MESSAGE, "%s,%s: user [%s] could not be validated",
	      __FILE__, __FUNCTION__, username);

	db_free_result();
	my_free(checkstring);
	return 0;
}

char *auth_get_userid(u64_t user_idnr)
{
	const char *query_result;
	char *returnid = NULL;

	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "SELECT userid FROM users WHERE user_idnr = '%llu'",
		 user_idnr);

	if (__auth_query(__auth_query_data) == -1) {
		trace(TRACE_ERROR, "%s,%s: query failed",
		      __FILE__, __FUNCTION__);
		return 0;
	}

	if (db_num_rows() < 1) {
		trace(TRACE_DEBUG, "%s,%s: user has no username?",
		      __FILE__, __FUNCTION__);
		db_free_result();
		return 0;
	}

	query_result = db_get_result(0, 0);
	if (query_result) {
		trace(TRACE_DEBUG, "%s,%s: query_result = %s", __FILE__,
		      __FUNCTION__, query_result);
		if (!
		    (returnid =
		     (char *) my_malloc(strlen(query_result) + 1))) {
			trace(TRACE_ERROR, "%s,%s: out of memory",
			      __FILE__, __FUNCTION__);
			db_free_result();
			return NULL;
		}
		strncpy(returnid, query_result, strlen(query_result) + 1);
	}

	db_free_result();
	trace(TRACE_DEBUG, "%s,%s: returning %s as returnid", __FILE__,
	      __FUNCTION__, returnid);
	return returnid;
}

u64_t __auth_insert_result(const char *sequence_identifier)
{
	u64_t insert_result;
	insert_result = db_insert_result(sequence_identifier);
	return insert_result;
}
