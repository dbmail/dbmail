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

extern db_param_t _db_params;
#define DBPFX _db_params.pfx

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
	char *escaped_username;

	assert(user_idnr != NULL);
	*user_idnr = 0;
	if (!username) {
		trace(TRACE_ERROR, "%s,%s: got NULL as username",
		      __FILE__, __func__);
		return 0;
	}

	if (!(escaped_username = (char *) malloc(strlen(username) * 2 + 1))) {
		trace(TRACE_ERROR, "%s,%s: out of memory allocating "
		      "escaped username", __FILE__, __func__);
		return -1;
	}

	db_escape_string(escaped_username, username, strlen(username));

	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "SELECT user_idnr FROM %susers WHERE userid='%s'",DBPFX,
		 escaped_username);
	free(escaped_username);

	if (__auth_query(__auth_query_data) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not execute query",
		      __FILE__, __func__);
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
		      __FILE__, __func__);
		return -2;
	}

	list_init(users);

	/* do a inverted (DESC) query because adding the names to the
	 * final list inverts again */
	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "SELECT userid FROM %susers ORDER BY userid DESC",DBPFX);

	if (__auth_query(__auth_query_data) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not retrieve user list",
		      __FILE__, __func__);
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
		 "SELECT client_idnr FROM %susers WHERE user_idnr = '%llu'",DBPFX,
		 user_idnr);

	if (__auth_query(__auth_query_data) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: could not retrieve client id for user [%llu]\n",
		      __FILE__, __func__, user_idnr);
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
		 "SELECT maxmail_size FROM %susers WHERE user_idnr = '%llu'",DBPFX,
		 user_idnr);

	if (__auth_query(__auth_query_data) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: could not retrieve client id for user [%llu]",
		      __FILE__, __func__, user_idnr);
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
		      __FILE__, __func__, user_idnr);
		return __auth_encryption_desc_string;	/* return empty */
	}

	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "SELECT encryption_type FROM %susers WHERE user_idnr = '%llu'",DBPFX,
		 user_idnr);

	if (__auth_query(__auth_query_data) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: could not retrieve encryption type for user [%llu]",
		      __FILE__, __func__, user_idnr);
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
	char *escaped_username;

	trace(TRACE_DEBUG, "%s,%s: checking user [%s] in alias table",
	      __FILE__, __func__, username);

	saveres = db_get_result_set();
	db_set_result_set(NULL);

	if (checks > MAX_CHECKS_DEPTH) {
		trace(TRACE_ERROR,
		      "%s,%s: maximum checking depth reached, "
		      "there probably is a loop in your alias table",
		      __FILE__, __func__);
		return -1;
	}

	if (!(escaped_username = (char *) malloc(strlen(username) * 2 + 1))) {
		trace(TRACE_ERROR, "%s,%s: out of memory allocating "
			"escaped username", __FILE__, __func__);
		return -1;
	}

	db_escape_string(escaped_username, username, strlen(username));

	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "SELECT deliver_to FROM %saliases WHERE "
		 "lower(alias) = lower('%s')",DBPFX, escaped_username);
	free(escaped_username);

	trace(TRACE_DEBUG, "%s,%s: checks [%d]", __FILE__, __func__,
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
			      __FILE__, __func__, username);
			db_free_result();
			db_set_result_set(saveres);
			return 1;
		} else {
			trace(TRACE_DEBUG,
			      "%s,%s: user %s not in aliases table",
			      __FILE__, __func__, username);
			db_free_result();
			db_set_result_set(saveres);
			return 0;
		}
	}

	trace(TRACE_DEBUG, "%s,%s: into checking loop", __FILE__,
	      __func__);

	if (num_rows > 0) {
		for (counter = 0; counter < num_rows; counter++) {
			/* do a recursive search for deliver_to */
			query_result = db_get_result(counter, 0);
			trace(TRACE_DEBUG, "%s,%s: checking user %s to %s",
			      __FILE__, __func__, username,
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
	char *escaped_username;

	saveres = db_get_result_set();
	db_set_result_set(NULL);

	trace(TRACE_DEBUG, "%s,%s: checking user [%s] in alias table",
	      __FILE__, __func__, username);

	if (!(escaped_username = (char *) malloc(strlen(username) * 2 + 1))) {
		trace(TRACE_ERROR, "%s,%s: out of memory allocating "
			"escaped username", __FILE__, __func__);
		return -1;
	}

	db_escape_string(escaped_username, username, strlen(username));

	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "SELECT deliver_to FROM %saliases "
		 "WHERE lower(alias) = lower('%s')",DBPFX, escaped_username);
	free(escaped_username);

	trace(TRACE_DEBUG, "%s,%s: checks [%d]", __FILE__, __func__,
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
			      __FILE__, __func__, username);
			db_free_result();
			db_set_result_set(saveres);
			return 1;
		} else {
			trace(TRACE_DEBUG,
			      "%s,%s: user %s not in aliases table",
			      __FILE__, __func__, username);
			db_free_result();
			db_set_result_set(saveres);
			return 0;
		}
	}

	trace(TRACE_DEBUG, "%s,%s: into checking loop", __FILE__,
	      __func__);

	if (num_rows > 0) {
		for (counter = 0; counter < num_rows; counter++) {
			/* do a recursive search for deliver_to */
			query_result = db_get_result(counter, 0);
			trace(TRACE_DEBUG, "%s,%s: checking user %s to %s",
			      __FILE__, __func__, username,
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
		      __FILE__, __func__);
		return -1;
	}
	return 0;
}

int auth_adduser(const char *username, const char *password, const char *enctype,
		 u64_t clientid, u64_t maxmail, u64_t * user_idnr)
{
	char escapedpass[AUTH_QUERY_SIZE];
	char *escaped_username;

	assert(user_idnr != NULL);
	*user_idnr = 0;

#ifdef _DBAUTH_STRICT_USER_CHECK
	if (!(escaped_username = (char *) malloc(strlen(username) * 2 + 1))) {
		trace(TRACE_ERROR, "%s,%s: out of memory allocating "
			"escaped username", __FILE__, __func__);
		return -1;
	}

	db_escape_string(escaped_username, username, strlen(username));
	/* first check to see if this user already exists */
	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "SELECT * FROM %susers WHERE userid = '%s'",DBPFX, escaped_username);
	free(escaped_username);

	if (__auth_query(__auth_query_data) == -1) {
		/* query failed */
		trace(TRACE_ERROR, "%s,%s: query failed",
		      __FILE__, __func__);
		return -1;
	}

	if (db_num_rows() > 0) {
		/* this username already exists */
		trace(TRACE_ERROR, "%s,%s: user already exists",
		      __FILE__, __func__);
		db_free_result();
		return -1;
	}

	db_free_result();
#endif

	if (strlen(password) >= AUTH_QUERY_SIZE) {
		trace(TRACE_ERROR, "%s,%s: password length is insane",
		      __FILE__, __func__);
		return -1;
	}

	db_escape_string(escapedpass, password, strlen(password));

	if (!(escaped_username = (char *) malloc(strlen(username) * 2 + 1))) {
		trace(TRACE_ERROR, "%s,%s: out of memory allocating "
			"escaped username", __FILE__, __func__);
		return -1;
	}

	db_escape_string(escaped_username, username, strlen(username));

	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "INSERT INTO %susers "
		 "(userid,passwd,client_idnr,maxmail_size,"
		 "encryption_type, last_login) VALUES "
		 "('%s','%s',%llu,'%llu','%s', CURRENT_TIMESTAMP)",DBPFX,
		 escaped_username, escapedpass, clientid, maxmail,
		 enctype ? enctype : "");
	free(escaped_username);

	if (__auth_query(__auth_query_data) == -1) {
		/* query failed */
		trace(TRACE_ERROR, "%s,%s: query for adding user failed",
		      __FILE__, __func__);
		return -1;
	}
	*user_idnr = __auth_insert_result("user_idnr");

	return 1;
}

int auth_delete_user(const char *username)
{
	char *escaped_username;

	if (!(escaped_username = (char *) malloc(strlen(username) * 2 + 1))) {
		trace(TRACE_ERROR, "%s,%s: out of memory allocating "
			"escaped username", __FILE__, __func__);
		return -1;
	}

	db_escape_string(escaped_username, username, strlen(username));

	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "DELETE FROM %susers WHERE userid = '%s'",
		 DBPFX, escaped_username);
	free(escaped_username);

	if (__auth_query(__auth_query_data) == -1) {
		/* query failed */
		trace(TRACE_ERROR, "%s,%s: query for removing user failed",
		      __FILE__, __func__);
		return -1;
	}

	return 0;
}

int auth_change_username(u64_t user_idnr, const char *new_name)
{
	char *escaped_new_name;

	if (!(escaped_new_name = (char *) malloc(strlen(new_name) * 2 + 1))) {
		trace(TRACE_ERROR, "%s,%s: out of memory allocating "
			"escaped new_name", __FILE__, __func__);
		return -1;
	}

	db_escape_string(escaped_new_name, new_name, strlen(new_name));

	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "UPDATE %susers SET userid = '%s' WHERE user_idnr='%llu'",
		 DBPFX, escaped_new_name, user_idnr);
	free(escaped_new_name);

	if (__auth_query(__auth_query_data) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: could not change name for user [%llu]",
		      __FILE__, __func__, user_idnr);
		return -1;
	}

	return 0;
}

int auth_change_password(u64_t user_idnr, const char *new_pass,
			 const char *enctype)
{
	char escapedpass[AUTH_QUERY_SIZE];

	if (strlen(new_pass) >= AUTH_QUERY_SIZE) {
		trace(TRACE_ERROR, "%s,%s: new password length is insane",
		      __FILE__, __func__);
		return -1;
	}

	db_escape_string(escapedpass, new_pass, strlen(new_pass));


	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "UPDATE %susers SET passwd = '%s', encryption_type = '%s' "
		 " WHERE user_idnr='%llu'", DBPFX,
		 escapedpass, enctype ? enctype : "", user_idnr);

	if (__auth_query(__auth_query_data) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: could not change passwd for user [%llu]",
		      __FILE__, __func__, user_idnr);
		return -1;
	}

	return 0;
}

int auth_change_clientid(u64_t user_idnr, u64_t new_cid)
{
	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "UPDATE %susers SET client_idnr = '%llu' "
		 "WHERE user_idnr='%llu'",
		 DBPFX, new_cid, user_idnr);

	if (__auth_query(__auth_query_data) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: could not change client id for user [%llu]",
		      __FILE__, __func__, user_idnr);
		return -1;
	}

	return 0;
}

int auth_change_mailboxsize(u64_t user_idnr, u64_t new_size)
{
	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "UPDATE %susers SET maxmail_size = '%llu' "
		 "WHERE user_idnr = '%llu'",
		 DBPFX, new_size, user_idnr);

	if (__auth_query(__auth_query_data) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: could not change maxmailsize for user [%llu]",
		      __FILE__, __func__, user_idnr);
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
		      __FILE__, __func__);
		return 0;
	}

	create_current_timestring(&timestring);

	/* the shared mailbox user should not log in! */
	if (strcmp(username, SHARED_MAILBOX_USERNAME) == 0)
		return 0;

	if (!(escuser = (char *) malloc(strlen(username) * 2 + 1))) {
		trace(TRACE_ERROR,
		      "%s,%s: out of memory allocating for escaped userid",
		      __FILE__, __func__);
		return -1;
	}

	db_escape_string(escuser, username, strlen(username));

	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "SELECT user_idnr, passwd, encryption_type FROM %susers "
		 "WHERE userid = '%s'", DBPFX, escuser);

	if (__auth_query(__auth_query_data) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: could not select user information", __FILE__,
		      __func__);
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
		      "%s,%s: validating using plaintext passwords",
		      __FILE__, __func__);
		/* get password from database */
		query_result = db_get_result(0, 1);
		is_validated =
		    (strcmp(query_result, password) == 0) ? 1 : 0;
	} else if (strcasecmp(query_result, "crypt") == 0) {
		trace(TRACE_DEBUG,
		      "%s,%s: validating using crypt() encryption",
		      __FILE__, __func__);
		query_result = db_get_result(0, 1);
		is_validated = (strcmp((const char *) crypt(password, query_result),	/* Flawfinder: ignore */
				       query_result) == 0) ? 1 : 0;
	} else if (strcasecmp(query_result, "md5") == 0) {
		/* get password */
		query_result = db_get_result(0, 1);
		if (strncmp(query_result, "$1$", 3)) {
			trace(TRACE_DEBUG,
			      "%s,%s: validating using MD5 digest comparison",
			      __FILE__, __func__);
			/* redundant statement: query_result = db_get_result(0, 1); */
			is_validated =
			    (strncmp(makemd5(password), query_result, 32)
			     == 0) ? 1 : 0;
		} else {
			trace(TRACE_DEBUG,
			      "%s, %s: validating using MD5 hash comparison",
			      __FILE__, __func__);
			strncpy(salt, query_result, 12);
			strncpy(cryptres, (char *) crypt(password, query_result), 34);	/* Flawfinder: ignore */

			trace(TRACE_DEBUG, "%s,%s: salt   : %s",
			      __FILE__, __func__, salt);
			trace(TRACE_DEBUG, "%s,%s: hash   : %s",
			      __FILE__, __func__, query_result);
			trace(TRACE_DEBUG, "%s,%s: crypt(): %s",
			      __FILE__, __func__, cryptres);

			is_validated =
			    (strncmp(query_result, cryptres, 34) ==
			     0) ? 1 : 0;
		}
	} else if (strcasecmp(query_result, "md5sum") == 0) {
		trace(TRACE_DEBUG,
		      "%s,%s: validating using MD5 digest comparison",
		      __FILE__, __func__);
		query_result = db_get_result(0, 1);
		is_validated =
		    (strncmp(makemd5(password), query_result, 32) ==
		     0) ? 1 : 0;
	}

	if (is_validated) {
		query_result = db_get_result(0, 0);
		*user_idnr =
		    (query_result) ? strtoull(query_result, NULL, 10) : 0;

		db_free_result();

		/* log login in the dbase */
		snprintf(__auth_query_data, AUTH_QUERY_SIZE,
			 "UPDATE %susers SET last_login = '%s' "
			 "WHERE user_idnr = '%llu'",DBPFX, timestring,
			 *user_idnr);

		if (__auth_query(__auth_query_data) == -1)
			trace(TRACE_ERROR,
			      "%s,%s: could not update user login time",
			      __FILE__, __func__);
	} else {
		db_free_result();
	}
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
	char *escaped_username;

	create_current_timestring(&timestring);

	if (!(escaped_username = (char *) malloc (strlen(username) * 2 + 1))) {
		trace(TRACE_ERROR, "%s,%s: error allocating escaped_username",
		      __FILE__, __func__);
		return -1;
	}

	db_escape_string(escaped_username, username, strlen(username));

	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "SELECT passwd,user_idnr FROM %susers WHERE "
		 "userid = '%s'", DBPFX, escaped_username);
	free(escaped_username);

	if (__auth_query(__auth_query_data) == -1) {
		trace(TRACE_ERROR, "%s,%s: error calling __auth_query()",
		      __FILE__, __func__);
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
	      __FILE__, __func__, apop_stamp, query_result);


	memtst((checkstring =
		(char *) my_malloc(strlen(apop_stamp) +
				   strlen(query_result) + 2)) == NULL);
	snprintf(checkstring,
		 strlen(apop_stamp) + strlen(query_result) + 2, "%s%s",
		 apop_stamp, query_result);

	md5_apop_we = makemd5(checkstring);

	trace(TRACE_DEBUG,
	      "%s,%s: checkstring for md5 [%s] -> result [%s]", __FILE__,
	      __func__, checkstring, md5_apop_we);
	trace(TRACE_DEBUG,
	      "%s,%s: validating md5_apop_we=[%s] md5_apop_he=[%s]",
	      __FILE__, __func__, md5_apop_we, md5_apop_he);

	if (strcmp(md5_apop_he, makemd5(checkstring)) == 0) {
		trace(TRACE_MESSAGE,
		      "%s,%s: user [%s] is validated using APOP", __FILE__,
		      __func__, username);
		/* get user idnr */
		query_result = db_get_result(0, 1);
		user_idnr =
		    (query_result) ? strtoull(query_result, NULL, 10) : 0;
		db_free_result();
		my_free(checkstring);

		/* log login in the dbase */
		snprintf(__auth_query_data, AUTH_QUERY_SIZE,
			 "UPDATE %susers SET last_login = '%s' "
			 "WHERE user_idnr = '%llu'",DBPFX, timestring,
			 user_idnr);

		if (__auth_query(__auth_query_data) == -1)
			trace(TRACE_ERROR,
			      "%s,%s: could not update user login time",
			      __FILE__, __func__);

		return user_idnr;
	}

	trace(TRACE_MESSAGE, "%s,%s: user [%s] could not be validated",
	      __FILE__, __func__, username);

	db_free_result();
	my_free(checkstring);
	return 0;
}

char *auth_get_userid(u64_t user_idnr)
{
	const char *query_result;
	char *returnid = NULL;

	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "SELECT userid FROM %susers WHERE user_idnr = '%llu'",
		 DBPFX, user_idnr);

	if (__auth_query(__auth_query_data) == -1) {
		trace(TRACE_ERROR, "%s,%s: query failed",
		      __FILE__, __func__);
		return 0;
	}

	if (db_num_rows() < 1) {
		trace(TRACE_DEBUG, "%s,%s: user has no username?",
		      __FILE__, __func__);
		db_free_result();
		return 0;
	}

	query_result = db_get_result(0, 0);
	if (query_result) {
		trace(TRACE_DEBUG, "%s,%s: query_result = %s",
			__FILE__, __func__, query_result);
		if (!(returnid =
		     (char *) my_malloc(strlen(query_result) + 1))) {
			trace(TRACE_ERROR, "%s,%s: out of memory",
			      __FILE__, __func__);
			db_free_result();
			return NULL;
		}
		strncpy(returnid, query_result, strlen(query_result) + 1);
	}

	db_free_result();
	trace(TRACE_DEBUG, "%s,%s: returning %s as returnid", __FILE__,
	      __func__, returnid);
	return returnid;
}

u64_t __auth_insert_result(const char *sequence_identifier)
{
	u64_t insert_result;
	insert_result = db_insert_result(sequence_identifier);
	return insert_result;
}

int auth_get_users_from_clientid(u64_t client_id, u64_t ** user_ids,
			       unsigned *num_users)
{
	unsigned i;

	assert(user_ids != NULL);
	assert(num_users != NULL);
	
	*user_ids = NULL;
	*num_users = 0;

	snprintf(__auth_query_data, DEF_QUERYSIZE,
		 "SELECT user_idnr FROM %susers WHERE client_idnr = '%llu'",
		 DBPFX, client_id);
	if (__auth_query(__auth_query_data) == -1) {
		trace(TRACE_ERROR, "%s,%s: error gettings users for "
		      "client_id [%llu]", __FILE__, __func__, client_id);
		return -1;
	}
	*num_users = db_num_rows();
	*user_ids = (u64_t *) my_malloc(*num_users * sizeof(u64_t));
	if (*user_ids == NULL) {
		trace(TRACE_ERROR,
		      "%s,%s: error allocating memory, probably "
		      "out of memory", __FILE__, __func__);
		db_free_result();
		return -2;
	}
	memset(*user_ids, 0, *num_users * sizeof(u64_t));
	for (i = 0; i < *num_users; i++) {
		(*user_ids)[i] = db_get_result_u64(i, 0);
	}
	db_free_result();
	return 1;
}

char *auth_get_deliver_from_alias(const char *alias)
{
	char *deliver = NULL;
	const char *query_result = NULL;
	char *escaped_alias;

	if (!(escaped_alias = (char *) malloc(strlen(alias) * 2 + 1))) {
		trace(TRACE_ERROR, "%s,%s: out of memory allocating "
		      "escaped alias", __FILE__, __func__);
		return NULL;
	}

	db_escape_string(escaped_alias, alias, strlen(alias));

	snprintf(__auth_query_data, DEF_QUERYSIZE,
		 "SELECT deliver_to FROM %saliases WHERE alias = '%s'",
		 DBPFX, escaped_alias);
	free(escaped_alias);

	if (__auth_query(__auth_query_data) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not execute query",
		      __FILE__, __func__);
		return NULL;
	}

	if (db_num_rows() == 0) {
		/* no such user */
		db_free_result();
		return strdup("");
	}

	query_result = db_get_result(0, 0);
	if (!query_result) {
		db_free_result();
		return NULL;
	}

	deliver = strdup(query_result);
	db_free_result();
	return deliver;
}

int auth_addalias(u64_t user_idnr, const char *alias, u64_t clientid)
{
	char *escaped_alias;

	if (!(escaped_alias = (char *) malloc(strlen(alias) * 2 + 1))) {
		trace(TRACE_ERROR, "%s,%s: out of memory allocating "
		      "escaped alias", __FILE__, __func__);
		return -1;
	}

	db_escape_string(escaped_alias, alias, strlen(alias));

	/* check if this alias already exists */
	snprintf(__auth_query_data, DEF_QUERYSIZE,
		 "SELECT alias_idnr FROM %saliases "
		 "WHERE lower(alias) = lower('%s') AND deliver_to = '%llu' "
		 "AND client_idnr = '%llu'",DBPFX, escaped_alias, user_idnr, clientid);

	if (__auth_query(__auth_query_data) == -1) {
		/* query failed */
		trace(TRACE_ERROR,
		      "%s,%s: query for searching alias failed", __FILE__,
		      __func__);
		free(escaped_alias);
		return -1;
	}

	if (db_num_rows() > 0) {
		trace(TRACE_INFO,
		      "%s,%s: alias [%s] for user [%llu] already exists",
		      __FILE__, __func__, escaped_alias, user_idnr);

		free(escaped_alias);
		db_free_result();
		return 1;
	}

	db_free_result();
	snprintf(__auth_query_data, DEF_QUERYSIZE,
		 "INSERT INTO %saliases (alias,deliver_to,client_idnr) "
		 "VALUES ('%s','%llu','%llu')",DBPFX, escaped_alias, user_idnr,
		 clientid);
	free(escaped_alias);

	if (db_query(__auth_query_data) == -1) {
		/* query failed */
		trace(TRACE_ERROR, "%s,%s: query for adding alias failed",
		      __FILE__, __func__);
		return -1;
	}
	return 0;
}

int auth_addalias_ext(const char *alias,
		    const char *deliver_to, u64_t clientid)
{
	char *escaped_alias;
	char *escaped_deliver_to;

	if (!(escaped_alias = (char *) malloc(strlen(alias) * 2 + 1))) {
		trace(TRACE_ERROR, "%s,%s: out of memory allocating "
		      "escaped alias", __FILE__, __func__);
		return -1;
	}

	if (!(escaped_deliver_to = (char *) malloc(strlen(deliver_to) * 2 + 1))) {
		trace(TRACE_ERROR, "%s,%s: out of memory allocating "
		      "escaped deliver_to", __FILE__, __func__);
		return -1;
	}


	db_escape_string(escaped_alias, alias, strlen(alias));
	db_escape_string(escaped_deliver_to, deliver_to, strlen(deliver_to));

	/* check if this alias already exists */
	if (clientid != 0) {
		snprintf(__auth_query_data, DEF_QUERYSIZE,
			 "SELECT alias_idnr FROM %saliases "
			 "WHERE lower(alias) = lower('%s') AND "
			 "lower(deliver_to) = lower('%s') "
			 "AND client_idnr = '%llu'",DBPFX, escaped_alias, escaped_deliver_to,
			 clientid);
	} else {
		snprintf(__auth_query_data, DEF_QUERYSIZE,
			 "SELECT alias_idnr FROM %saliases "
			 "WHERE lower(alias) = lower('%s') "
			 "AND lower(deliver_to) = lower('%s') ",DBPFX,
			 escaped_alias, escaped_deliver_to);
	}

	if (__auth_query(__auth_query_data) == -1) {
		/* query failed */
		trace(TRACE_ERROR,
		      "%s,%s: query for searching alias failed", __FILE__,
		      __func__);
		free(escaped_alias);
		free(escaped_deliver_to);
		return -1;
	}

	if (db_num_rows() > 0) {
		trace(TRACE_INFO,
		      "%s,%s: alias [%s] --> [%s] already exists",
		      __FILE__, __func__, alias, deliver_to);

		free(escaped_alias);
		free(escaped_deliver_to);
		db_free_result();
		return 1;
	}
	db_free_result();

	snprintf(__auth_query_data, DEF_QUERYSIZE,
		 "INSERT INTO %saliases (alias,deliver_to,client_idnr) "
		 "VALUES ('%s','%s','%llu')",DBPFX, escaped_alias, escaped_deliver_to, clientid);
	free(escaped_alias);
	free(escaped_deliver_to);

	if (__auth_query(__auth_query_data) == -1) {
		/* query failed */
		trace(TRACE_ERROR, "%s,%s: query for adding alias failed",
		      __FILE__, __func__);
		return -1;
	}
	return 0;
}

int auth_removealias(u64_t user_idnr, const char *alias)
{
	char *escaped_alias;

	if (!(escaped_alias = (char *) malloc(strlen(alias) * 2 + 1))) {
		trace(TRACE_ERROR, "%s,%s: out of memory allocating "
		      "escaped alias", __FILE__, __func__);
		return -1;
	}

	db_escape_string(escaped_alias, alias, strlen(alias));

	snprintf(__auth_query_data, DEF_QUERYSIZE,
		 "DELETE FROM %saliases WHERE deliver_to='%llu' "
		 "AND lower(alias) = lower('%s')",DBPFX, user_idnr, escaped_alias);
	free(escaped_alias);

	if (__auth_query(__auth_query_data) == -1) {
		/* query failed */
		trace(TRACE_ERROR, "%s,%s: query failed", __FILE__,
		      __func__);
		return -1;
	}
	return 0;
}

int auth_removealias_ext(const char *alias, const char *deliver_to)
{
	char *escaped_alias;
	char *escaped_deliver_to;

	if (!(escaped_alias = (char *) malloc(strlen(alias) * 2 + 1))) {
		trace(TRACE_ERROR, "%s,%s: out of memory allocating "
		      "escaped alias", __FILE__, __func__);
		return -1;
	}

	if (!(escaped_deliver_to = (char *) malloc(strlen(deliver_to) * 2 + 1))) {
		trace(TRACE_ERROR, "%s,%s: out of memory allocating "
		      "escaped deliver_to", __FILE__, __func__);
		return -1;
	}

	db_escape_string(escaped_alias, alias, strlen(alias));
	db_escape_string(escaped_deliver_to, deliver_to, strlen(deliver_to));

	snprintf(__auth_query_data, DEF_QUERYSIZE,
		 "DELETE FROM %saliases WHERE lower(deliver_to) = lower('%s') "
		 "AND lower(alias) = lower('%s')",DBPFX, deliver_to, alias);
	free(escaped_alias);
	free(escaped_deliver_to);

	if (db_query(__auth_query_data) == -1) {
		/* query failed */
		trace(TRACE_ERROR, "%s,%s: query failed", __FILE__,
		      __func__);
		return -1;
	}
	return 0;
}


int auth_get_user_aliases(u64_t user_idnr, struct list *aliases)
{
	int i, n;
	const char *query_result;

	if (!aliases) {
		trace(TRACE_ERROR, "%s,%s: got a NULL pointer as argument",
		      __FILE__, __func__);
		return -2;
	}

	list_init(aliases);

	/* do a inverted (DESC) query because adding the names to the 
	 * final list inverts again */
	snprintf(__auth_query_data, DEF_QUERYSIZE,
		 "SELECT alias FROM %saliases WHERE deliver_to = '%llu' "
		 "ORDER BY alias DESC",DBPFX, user_idnr);

	if (__auth_query(__auth_query_data) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not retrieve  list",
		      __FILE__, __func__);
		return -1;
	}

	n = db_num_rows();
	for (i = 0; i < n; i++) {
		query_result = db_get_result(i, 0);
		if (!query_result || !list_nodeadd(aliases,
						   query_result,
						   strlen(query_result) +
						   1)) {
			list_freelist(&aliases->start);
			db_free_result();
			return -2;
		}
	}

	db_free_result();
	return 0;
}

