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

#include "dbmail.h"

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
	return db_user_exists(username, user_idnr);
}

GList * auth_get_known_users(void)
{
	u64_t i;
	GList * users = NULL;

	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "SELECT userid FROM %susers ORDER BY userid",DBPFX);

	if (__auth_query(__auth_query_data) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not retrieve user list",
		      __FILE__, __func__);
		return NULL;
	}

	for (i = 0; i < (unsigned) db_num_rows(); i++) 
		users = g_list_append(users, g_strdup(db_get_result(i, 0)));
	
	db_free_result();
	return users;
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

int auth_check_user_ext(const char *username, struct dm_list *userids, struct dm_list *fwds, int checks)
{
	int occurences = 0;
	void *saveres;
	u64_t counter;
	const char *query_result;
	char *endptr;
	u64_t id;
	unsigned num_rows;
	char *escaped_username;

	if (checks > 20) {
		trace(TRACE_ERROR,"%s,%s: too many checks. Possible loop detected.",
				__FILE__, __func__);
		return 0;
	}

	saveres = db_get_result_set();
	db_set_result_set(NULL);

	trace(TRACE_DEBUG, "%s,%s: checking user [%s] in alias table",
	      __FILE__, __func__, username);

	if (!(escaped_username = (char *) dm_malloc(strlen(username) * 2 + 1))) {
		trace(TRACE_ERROR, "%s,%s: out of memory allocating "
			"escaped username", __FILE__, __func__);
		return -1;
	}

	db_escape_string(escaped_username, username, strlen(username));

	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "SELECT deliver_to FROM %saliases "
		 "WHERE lower(alias) = lower('%s') "
		 "AND lower(alias) <> lower(deliver_to)",
		 DBPFX, escaped_username);
	
	dm_free(escaped_username);

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
				dm_list_nodeadd(userids, &id, sizeof(id));
			/* numeric deliver-to --> this is a userid */
			else
				dm_list_nodeadd(fwds, username,
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
			      __FILE__, __func__, username, query_result);
			occurences += auth_check_user_ext(query_result, userids, fwds, checks+1 );
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
	*user_idnr=0; 
	return db_user_create(username, password, enctype, clientid, maxmail, user_idnr);
}


int auth_delete_user(const char *username)
{
	return db_user_delete(username);
}


int auth_change_username(u64_t user_idnr, const char *new_name)
{
	return db_user_rename(user_idnr, new_name);
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
	return db_change_mailboxsize(user_idnr, new_size);
}

int auth_validate(clientinfo_t *ci, char *username, char *password, u64_t * user_idnr)
{
	const char *query_result;
	int is_validated = 0;
	char salt[13];
	char cryptres[35];
	char real_username[DM_USERNAME_LEN];
	char *md5str;
	int result;

	assert(user_idnr != NULL);
	*user_idnr = 0;

	if (username == NULL || password == NULL) {
		trace(TRACE_DEBUG, "%s,%s: username or password is NULL",
		      __FILE__, __func__);
		return 0;
	}


	/* the shared mailbox user should not log in! */
	if (strcmp(username, SHARED_MAILBOX_USERNAME) == 0)
		return 0;

	strncpy(real_username, username, DM_USERNAME_LEN);
	if (db_use_usermap()) {  /* use usermap */
		result = db_usermap_resolve(ci, username, real_username);
		if (result == DM_EGENERAL)
			return 0;
		if (result == DM_EQUERY)
			return DM_EQUERY;
	}
	
	/* lookup the user_idnr */
	if (auth_user_exists(real_username, user_idnr) == DM_EQUERY)
		return DM_EQUERY;

	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "SELECT user_idnr, passwd, encryption_type FROM %susers "
		 "WHERE user_idnr = '%llu'", DBPFX, *user_idnr);

	if (__auth_query(__auth_query_data) == -1) {
		trace(TRACE_ERROR,
		      "%s,%s: could not select user information", __FILE__,
		      __func__);
		return -1;
	}

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
		is_validated = (strcmp(query_result, password) == 0) ? 1 : 0;
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
			md5str = dm_md5((unsigned char *)password);
			is_validated = (strncmp(md5str, query_result, 32) == 0) ? 1 : 0;
			dm_free(md5str);
		} else {
			trace(TRACE_DEBUG,
			      "%s, %s: validating using MD5 hash comparison",
			      __FILE__, __func__);
			strncpy(salt, query_result, 12);
			strncpy(cryptres, (char *) crypt(password, query_result), 34);	/* Flawfinder: ignore */
			trace(TRACE_DEBUG, "%s,%s: salt   : %s", __FILE__, __func__, salt);
			trace(TRACE_DEBUG, "%s,%s: hash   : %s", __FILE__, __func__, query_result);
			trace(TRACE_DEBUG, "%s,%s: crypt(): %s", __FILE__, __func__, cryptres);
			is_validated = (strncmp(query_result, cryptres, 34) == 0) ? 1 : 0;
		}
	} else if (strcasecmp(query_result, "md5sum") == 0) {
		trace(TRACE_DEBUG,
		      "%s,%s: validating using MD5 digest comparison",
		      __FILE__, __func__);
		query_result = db_get_result(0, 1);
		md5str = dm_md5((unsigned char *)password);
		is_validated = (strncmp(md5str, query_result, 32) == 0) ? 1 : 0;
		dm_free(md5str);
	} else if (strcasecmp(query_result, "md5base64") == 0) {
		trace(TRACE_DEBUG,
		      "%s,%s: validating using MD5 digest base64 comparison",
		      __FILE__, __func__);
		query_result = db_get_result(0, 1);
		md5str = dm_md5_base64((unsigned char *)password);
		is_validated = (strncmp(md5str, query_result, 32) == 0) ? 1 : 0;
		dm_free(md5str);
	}

	if (is_validated) {
		db_user_log_login(*user_idnr);
	} else {
		*user_idnr = 0;
	}
	db_free_result();
	return (is_validated ? 1 : 0);
}

u64_t auth_md5_validate(clientinfo_t *ci UNUSED, char *username,
		unsigned char *md5_apop_he, char *apop_stamp)
{
	/* returns useridnr on OK, 0 on validation failed, -1 on error */
	char *checkstring;
	char *md5_apop_we;
	u64_t user_idnr;
	const char *query_result;

	/* lookup the user_idnr */
	if (auth_user_exists(username, &user_idnr) == DM_EQUERY)
		return DM_EQUERY;
	
	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "SELECT passwd,user_idnr FROM %susers WHERE "
		 "user_idnr = '%llu'", DBPFX, user_idnr);

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
	query_result = db_get_result(0, 0); /* value holds the password */

	trace(TRACE_DEBUG, "%s,%s: apop_stamp=[%s], userpw=[%s]",
	      __FILE__, __func__, apop_stamp, query_result);


	memtst((checkstring = (char *) dm_malloc(strlen(apop_stamp) +
				   strlen(query_result) + 2)) == NULL);
	snprintf(checkstring,
		 strlen(apop_stamp) + strlen(query_result) + 2, "%s%s",
		 apop_stamp, query_result);

	md5_apop_we = dm_md5((unsigned char *)checkstring);

	trace(TRACE_DEBUG,
	      "%s,%s: checkstring for md5 [%s] -> result [%s]", __FILE__,
	      __func__, checkstring, md5_apop_we);
	trace(TRACE_DEBUG,
	      "%s,%s: validating md5_apop_we=[%s] md5_apop_he=[%s]",
	      __FILE__, __func__, md5_apop_we, md5_apop_he);

	if (strcmp(md5_apop_he, md5_apop_we) == 0) {
		trace(TRACE_MESSAGE,
		      "%s,%s: user [%s] is validated using APOP", __FILE__,
		      __func__, username);
		/* get user idnr */
		query_result = db_get_result(0, 1);
		user_idnr =
		    (query_result) ? strtoull(query_result, NULL, 10) : 0;
		db_free_result();
		dm_free(md5_apop_we);
		dm_free(checkstring);

		db_user_log_login(user_idnr);
		return user_idnr;
	}

	trace(TRACE_MESSAGE, "%s,%s: user [%s] could not be validated",
	      __FILE__, __func__, username);

	db_free_result();
	dm_free(md5_apop_we);
	dm_free(checkstring);
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
		     (char *) dm_malloc(strlen(query_result) + 1))) {
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

int auth_check_userid(u64_t user_idnr)
{
	snprintf(__auth_query_data, AUTH_QUERY_SIZE,
		 "SELECT userid FROM %susers WHERE user_idnr = '%llu'",
		 DBPFX, user_idnr);

	if (__auth_query(__auth_query_data) == -1) {
		trace(TRACE_ERROR, "%s,%s: query failed",
		      __FILE__, __func__);
		return -1;
	}

	if (db_num_rows() < 1) {
		trace(TRACE_DEBUG, "%s,%s: didn't find user_idnr [%llu]",
		      __FILE__, __func__, user_idnr);
		db_free_result();
		return 1;
	}

	trace(TRACE_DEBUG, "%s,%s: found user_idnr [%llu]",
	      __FILE__, __func__, user_idnr);
	db_free_result();
	return 0;
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
	*user_ids = (u64_t *) dm_malloc(*num_users * sizeof(u64_t));
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

int auth_addalias(u64_t user_idnr, const char *alias, u64_t clientid)
{
	char *escaped_alias;

	if (!(escaped_alias = (char *) dm_malloc(strlen(alias) * 2 + 1))) {
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
		dm_free(escaped_alias);
		return -1;
	}

	if (db_num_rows() > 0) {
		trace(TRACE_INFO,
		      "%s,%s: alias [%s] for user [%llu] already exists",
		      __FILE__, __func__, escaped_alias, user_idnr);

		dm_free(escaped_alias);
		db_free_result();
		return 1;
	}

	db_free_result();
	snprintf(__auth_query_data, DEF_QUERYSIZE,
		 "INSERT INTO %saliases (alias,deliver_to,client_idnr) "
		 "VALUES ('%s','%llu','%llu')",DBPFX, escaped_alias, user_idnr,
		 clientid);
	dm_free(escaped_alias);

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

	if (!(escaped_alias = (char *) dm_malloc(strlen(alias) * 2 + 1))) {
		trace(TRACE_ERROR, "%s,%s: out of memory allocating "
		      "escaped alias", __FILE__, __func__);
		return -1;
	}

	if (!(escaped_deliver_to = (char *) dm_malloc(strlen(deliver_to) * 2 + 1))) {
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
		dm_free(escaped_alias);
		dm_free(escaped_deliver_to);
		return -1;
	}

	if (db_num_rows() > 0) {
		trace(TRACE_INFO,
		      "%s,%s: alias [%s] --> [%s] already exists",
		      __FILE__, __func__, alias, deliver_to);

		dm_free(escaped_alias);
		dm_free(escaped_deliver_to);
		db_free_result();
		return 1;
	}
	db_free_result();

	snprintf(__auth_query_data, DEF_QUERYSIZE,
		 "INSERT INTO %saliases (alias,deliver_to,client_idnr) "
		 "VALUES ('%s','%s','%llu')",DBPFX, escaped_alias, escaped_deliver_to, clientid);
	dm_free(escaped_alias);
	dm_free(escaped_deliver_to);

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

	if (!(escaped_alias = (char *) dm_malloc(strlen(alias) * 2 + 1))) {
		trace(TRACE_ERROR, "%s,%s: out of memory allocating "
		      "escaped alias", __FILE__, __func__);
		return -1;
	}

	db_escape_string(escaped_alias, alias, strlen(alias));

	snprintf(__auth_query_data, DEF_QUERYSIZE,
		 "DELETE FROM %saliases WHERE deliver_to='%llu' "
		 "AND lower(alias) = lower('%s')",DBPFX, user_idnr, escaped_alias);
	dm_free(escaped_alias);

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

	if (!(escaped_alias = (char *) dm_malloc(strlen(alias) * 2 + 1))) {
		trace(TRACE_ERROR, "%s,%s: out of memory allocating "
		      "escaped alias", __FILE__, __func__);
		return -1;
	}

	if (!(escaped_deliver_to = (char *) dm_malloc(strlen(deliver_to) * 2 + 1))) {
		trace(TRACE_ERROR, "%s,%s: out of memory allocating "
		      "escaped deliver_to", __FILE__, __func__);
		return -1;
	}

	db_escape_string(escaped_alias, alias, strlen(alias));
	db_escape_string(escaped_deliver_to, deliver_to, strlen(deliver_to));

	snprintf(__auth_query_data, DEF_QUERYSIZE,
		 "DELETE FROM %saliases WHERE lower(deliver_to) = lower('%s') "
		 "AND lower(alias) = lower('%s')",DBPFX, deliver_to, alias);
	dm_free(escaped_alias);
	dm_free(escaped_deliver_to);

	if (db_query(__auth_query_data) == -1) {
		/* query failed */
		trace(TRACE_ERROR, "%s,%s: query failed", __FILE__,
		      __func__);
		return -1;
	}
	return 0;
}

GList * auth_get_user_aliases(u64_t user_idnr)
{
	int i, n;
	const char *query_result;
	GList *aliases = NULL;

	/* do a inverted (DESC) query because adding the names to the 
	 * final list inverts again */
	snprintf(__auth_query_data, DEF_QUERYSIZE,
		 "SELECT alias FROM %saliases WHERE deliver_to = '%llu' "
		 "ORDER BY alias DESC",DBPFX, user_idnr);

	if (__auth_query(__auth_query_data) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not retrieve  list",
		      __FILE__, __func__);
		return NULL;
	}

	n = db_num_rows();
	for (i = 0; i < n; i++) {
		query_result = db_get_result(i, 0);
		if (!query_result || ! (aliases = g_list_append(aliases,g_strdup(query_result)))) {
			g_list_foreach(aliases, (GFunc)g_free, NULL);
			g_list_free(aliases);
			db_free_result();
			return NULL;
		}
	}

	db_free_result();
	return aliases;
}

GList * auth_get_aliases_ext(const char *alias)
{
	int i, n;
	const char *query_result;
	GList *aliases = NULL;

	/* do a inverted (DESC) query because adding the names to the 
	 * final list inverts again */
	snprintf(__auth_query_data, DEF_QUERYSIZE,
		 "SELECT deliver_to FROM %saliases WHERE alias = '%s' "
		 "ORDER BY alias DESC",DBPFX, alias);

	if (__auth_query(__auth_query_data) == -1) {
		trace(TRACE_ERROR, "%s,%s: could not retrieve  list",
		      __FILE__, __func__);
		return NULL;
	}

	n = db_num_rows();
	for (i = 0; i < n; i++) {
		query_result = db_get_result(i, 0);
		if (!query_result || ! (aliases = g_list_append(aliases,g_strdup(query_result)))) {
			g_list_foreach(aliases, (GFunc)g_free, NULL);
			g_list_free(aliases);
			db_free_result();
			return NULL;
		}
	}

	db_free_result();
	return aliases;
}

gboolean auth_requires_shadow_user(void)
{
	return FALSE;
}

