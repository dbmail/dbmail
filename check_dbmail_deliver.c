/*
 *  Copyright (C) 2004  Paul Stevens <paul@nfg.nl>
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation; either
 *   version 2 of the License, or (at your option) any later
 *   version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *  $Id$ 
 *
 *
 *  
 *
 *   Basic unit-test framework for dbmail (www.dbmail.org)
 *
 *   See http://check.sf.net for details and docs.
 *
 *
 *   Run 'make check' to see some action.
 *
 */ 

#include <stdlib.h>
#include <check.h>
#include <stdio.h>
#include <string.h>
#include <check.h>

#include "dbmail.h"
#include "debug.h"
#include "db.h"
#include "auth.h"

#include "check_dbmail.h"

char *configFile = DEFAULT_CONFIG_FILE;
extern db_param_t _db_params;


/* we need this one because we can't directly link imapd.o */
int imap_before_smtp = 0;

static u64_t get_first_user_idnr(void);

u64_t get_first_user_idnr(void)
{
	u64_t user_idnr;
	GList *users = auth_get_known_users();
	users = g_list_first(users);
	auth_user_exists((char *)users->data,&user_idnr);
	return user_idnr;
}



/*
 *
 * the test fixtures
 *
 */

	
void setup(void)
{
	configure_debug(4,0,1);
	config_read(configFile);
	GetDBParams(&_db_params);
	db_connect();
	auth_connect();
}

void teardown(void)
{
	db_disconnect();
}

/**
 * \brief connect to the authentication database. In case of an SQL connection,
 * no now connection is made (the already present database connection is
 * used). 
 * \return
 * 		- -1 on failure
 * 		-  0 on success
 */
//int auth_connect(void);

START_TEST(test_auth_connect)
{
	fail_unless(auth_connect()==0,"auth_connect failed");
}
END_TEST

/**
 * \brief disconnect from the authentication database. In case of an SQL
 * authentication connection, the connection is not released, because the
 * main dbmail database connection is used.
 * \return
 *    - -1 on failure
 *    -  0 on success
 */
//int auth_disconnect(void);
START_TEST(test_auth_disconnect)
{
	fail_unless(auth_disconnect()==0,"auth_disconnect failed");
}
END_TEST

/**
 * \brief check if a user exists
 * \param username 
 * \param user_idnr will hold user_idnr after call. May not be NULL on call
 * \return 
 *    - -1 on database error
 *    -  0 if user not found
 *    -  1 otherwise
 */
//int auth_user_exists(const char *username, /*@out@*/ u64_t * user_idnr);
START_TEST(test_auth_user_exists)
{
	u64_t uid;
	int result;
	result = auth_user_exists(DBMAIL_DELIVERY_USERNAME,&uid);
	fail_unless(result==1,"auth_user_exists error return value");
	fail_unless(uid,"auth_user_exists found no internal user");
}
END_TEST

/**
 * \brief get a list of all known users
 * \return
 *    - list off all usernames on success
 *    - NULL on error
 * \attention caller should free list
 */
//GList * auth_get_known_users(void);
START_TEST(test_auth_get_known_users)
{
	GList *users = auth_get_known_users();
	fail_unless(users != NULL,"Unable to get known users");
	fail_unless(g_list_length(users) >= 1, "Usercount too low");
}
END_TEST


/**
 * \brief get client_id for a user
 * \param user_idnr
 * \param client_idnr will hold client_idnr after return. Must hold a valid
 * pointer on call
 * \return 
 *   - -1 on error
 *   -  1 on success
 */
//int auth_getclientid(u64_t user_idnr, u64_t * client_idnr);

START_TEST(test_auth_getclientid)
{
	int result;
	u64_t client_idnr;
	u64_t user_idnr = get_first_user_idnr();
	result = auth_getclientid(user_idnr, &client_idnr);
	fail_unless(result==1,"auth_getclientid failed");
	
}
END_TEST

/**
 * \brief get the maximum mail size for a user
 * \param user_idnr
 * \param maxmail_size will hold value of maxmail_size after return. Must
 * hold a valid pointer on call.
 * \return
 *     - -1 if error
 *     -  0 if no maxmail_size found (which effectively is the same as a 
 *        maxmail_size of 0.
 *     -  1 otherwise
 */
//int auth_getmaxmailsize(u64_t user_idnr, u64_t * maxmail_size);
START_TEST(test_auth_getmaxmailsize)
{
	int result;
	u64_t maxmail_size;
	u64_t user_idnr = get_first_user_idnr();
	result = auth_getmaxmailsize(user_idnr, &maxmail_size);
	fail_unless(result>=0,"auth_getmaxmailsize failed");
}
END_TEST


/**
 * \brief returns a string describing the encryption used for the 
 * passwd storage for this user.
 * The string is valid until the next function call; in absence of any 
 * encryption the string will be empty (not null).
 * If the specified user does not exist an empty string will be returned.
 * \param user_idnr
 * \return
 *    - NULL if error
 */
//char *auth_getencryption(u64_t user_idnr);
START_TEST(test_auth_getencryption)
{
	char * result = NULL;
	u64_t user_idnr = get_first_user_idnr();
	result = auth_getencryption(user_idnr);
	fail_unless(result!=NULL,"auth_getencryption failed");
}
END_TEST


/**
 * \brief find all deliver_to addresses for a username (?, code is not exactly
 * clear to me at the moment, IB 21-08-03)
 * \param username 
 * \param userids list of user ids (empty on call)
 * \param checks nr of checks. Used internally in recursive calls. It \b should
 * be set to -1 when called!
 * \return number of deliver_to addresses found
 */
//int auth_check_user(const char *username, struct list *userids,
//		    int checks);

/**
 * \brief as auth_check_user() but adds the numeric ID of the user found to
 * userids or the forward to the fwds list
 * \param username
 * \param userids list of user id's (empty on call)
 * \param fwds list of forwards (emoty on call)
 * \param checks used internally, \b should be -1 on call
 * \return number of deliver_to addresses found
 */
//int auth_check_user_ext(const char *username, struct list *userids,
//			struct list *fwds, int checks);
/**
 * \brief add a new user to the database (whichever type of database is 
 * implemented)
 * \param username name of new user
 * \param password his/her password
 * \param enctype encryption type of password
 * \param clientid client the user belongs with
 * \param maxmail maximum size of mailbox in bytes
 * \param user_idnr will hold the user_idnr of the user after return. Must hold
 * a valid pointer on call.
 * \return 
 *     - -1 on error
 *     -  1 on success
 */
//int auth_adduser(const char *username, const char *password, const char *enctype,
//		 u64_t clientid, u64_t maxmail, u64_t * user_idnr);

START_TEST(test_auth_adduser)
{
	int result;
	u64_t user_idnr;
	result = auth_adduser("sometestfoouser","sometestfoopass", "md5", 101, 1024000, &user_idnr);
	fail_unless(result==1,"auth_adduser failed");
	fail_unless(user_idnr > 0, "auth_adduser returned invalid user_idnr");
}
END_TEST

/**
 * \brief delete user from the database. Does not delete the user's email!
 * \param username name of user to be deleted
 * \return 
 *     - -1 on failure
 *     -  0 on success
 */
//int auth_delete_user(const char *username);
START_TEST(test_auth_delete_user)
{
	int result;
	result = auth_delete_user("sometestfoouser");
	fail_unless(result==0,"auth_delete_user failed");
}
END_TEST

/**
 * \brief change the username of a user.
 * \param user_idnr idnr identifying the user
 * \param new_name new name of user
 * \return
 *      - -1 on failure
 *      -  0 on success
 */
//int auth_change_username(u64_t user_idnr, const char *new_name);
/**
 * \brief change a users password
 * \param user_idnr
 * \param new_pass new password (encrypted)
 * \param enctype encryption type of password
 * \return
 *    - -1 on failure
 *    -  0 on success
 */
//int auth_change_password(u64_t user_idnr,
//			 const char *new_pass, const char *enctype);
/**
 * \brief change a users client id
 * \param user_idnr
 * \param new_cid new client id
 * \return
 *    - -1 on failure
 *    -  0 on success
 */
//int auth_change_clientid(u64_t user_idnr, u64_t new_cid);
/**
 * \brief change a user's mailbox size (maxmailsize)
 * \param user_idnr
 * \param new_size new size of mailbox
 * \return
 *    - -1 on failure
 *    -  0 on success
 */
//int auth_change_mailboxsize(u64_t user_idnr, u64_t new_size);
/**
 * \brief try to validate a user (used for login to server). 
 * \param username 
 * \param password
 * \param user_idnr will hold the user_idnr after return. Must be a pointer
 * to a valid u64_t variable on call.
 * \return
 *     - -1 on error
 *     -  0 if not validated
 *     -  1 if OK
 */
//int auth_validate(char *username, char *password, u64_t * user_idnr);

/** 
 * \brief try tp validate a user using md5 hash
 * \param username
 * \param md5_apop_he md5 string
 * \param apop_stamp timestamp
 * \return 
 *      - -1 on error
 *      -  0 if not validated
 *      -  user_idrn if OK
 */
//u64_t auth_md5_validate(char *username, unsigned char *md5_apop_he,
//			char *apop_stamp);

/**
 * \brief get username for a user_idnr
 * \param user_idnr
 * \return
 *    - NULL on error
 *    - username otherwise
 * \attention caller should free username string
 */
//char *auth_get_userid(u64_t user_idnr);

/**
 * \brief get user ids belonging to a client id
 * \param client_id 
 * \param user_ids
 * \param num_users
 * \return 
 *      - -2 on memory error
 *      - -1 on database error
 *      -  1 on success
 */
//int auth_get_users_from_clientid(u64_t client_id, 
//			       /*@out@*/ u64_t ** user_ids,
//			       /*@out@*/ unsigned *num_users);
/**
 * \brief get deliver_to from alias. Gets a list of deliver_to
 * addresses
 * \param alias the alias
 * \return 
 *         - NULL on failure
 *         - "" if no such alias found
 *         - deliver_to address otherwise
 * \attention caller needs to free the return value
 */
//char *auth_get_deliver_from_alias(const char *alias);
/**
 * \brief get a list of aliases associated with a user's user_idnr
 * \param user_idnr idnr of user
 * \param aliases list of aliases
 * \return
 * 		- -2 on memory failure
 * 		- -1 on database failure
 * 		- 0 on success
 * \attention aliases list needs to be empty. Method calls list_init()
 *            which sets list->start to NULL.
 */
//int auth_get_user_aliases(u64_t user_idnr, struct list *aliases);
/**
 * \brief add an alias for a user
 * \param user_idnr user's id
 * \param alias new alias
 * \param clientid client id
 * \return 
 *        - -1 on failure
 *        -  0 on success
 *        -  1 if alias already exists for given user
 */
//int auth_addalias(u64_t user_idnr, const char *alias, u64_t clientid);
/**
 * \brief add an alias to deliver to an extern address
 * \param alias the alias
 * \param deliver_to extern address to deliver to
 * \param clientid client idnr
 * \return 
 *        - -1 on failure
 *        - 0 on success
 *        - 1 if deliver_to already exists for given alias
 */
//int auth_addalias_ext(const char *alias, const char *deliver_to,
//		    u64_t clientid);
/**
 * \brief remove alias for user
 * \param user_idnr user id
 * \param alias the alias
 * \return
 *         - -1 on failure
 *         - 0 on success
 */
//int auth_removealias(u64_t user_idnr, const char *alias);
/**
 * \brief remove external delivery address for an alias
 * \param alias the alias
 * \param deliver_to the deliver to address the alias is
 *        pointing to now
 * \return
 *        - -1 on failure
 *        - 0 on success
 */
//int auth_removealias_ext(const char *alias, const char *deliver_to);



Suite *dbmail_deliver_suite(void)
{
	Suite *s = suite_create("Dbmail Delivery");
	TCase *tc_auth = tcase_create("Auth");
	
	suite_add_tcase(s, tc_auth);
	
	tcase_add_checked_fixture(tc_auth, setup, teardown);
	tcase_add_test(tc_auth, test_auth_connect);
	tcase_add_test(tc_auth, test_auth_disconnect);
	tcase_add_test(tc_auth, test_auth_user_exists);
	tcase_add_test(tc_auth, test_auth_get_known_users);
	tcase_add_test(tc_auth, test_auth_getclientid);
	tcase_add_test(tc_auth, test_auth_getmaxmailsize);
	tcase_add_test(tc_auth, test_auth_getencryption);
	/*
	tcase_add_test(tc_auth, test_auth_check_user);
	tcase_add_test(tc_auth, test_auth_check_user_ext);
	*/
	tcase_add_test(tc_auth, test_auth_adduser);
	tcase_add_test(tc_auth, test_auth_delete_user);
	/*
	tcase_add_test(tc_auth, test_auth_change_username);
	tcase_add_test(tc_auth, test_auth_change_password);
	tcase_add_test(tc_auth, test_auth_change_clientid);
	tcase_add_test(tc_auth, test_auth_change_mailboxsize);
	tcase_add_test(tc_auth, test_auth_validate);
	tcase_add_test(tc_auth, test_auth_md5_validate);
	tcase_add_test(tc_auth, test_get_userid);
	tcase_add_test(tc_auth, test_get_users_from_clientid);
	tcase_add_test(tc_auth, test_get_deliver_from_alias);
	tcase_add_test(tc_auth, test_get_user_aliases);
	tcase_add_test(tc_auth, test_auth_addalias);
	tcase_add_test(tc_auth, test_auth_addalias_ext);
	tcase_add_test(tc_auth, test_auth_removealias);
	tcase_add_test(tc_auth, test_auth_removealias_ext);
	*/
	
	return s;
}

int main(void)
{
	int nf;
	Suite *s = dbmail_deliver_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	suite_free(s);
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
	

