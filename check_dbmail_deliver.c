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
#include "misc.h"
#include "dsn.h"
#include "dbmail-message.h"
#include "mime.h"
#include "pipe.h"

#include "check_dbmail.h"

extern char * raw_message;
extern char * configFile;
extern db_param_t _db_params;


/* we need this one because we can't directly link imapd.o */
int imap_before_smtp = 0;
	
void init_testuser1(void) 
{
        u64_t user_idnr;
	if (! (auth_user_exists("testuser1",&user_idnr)))
		auth_adduser("testuser1","test", "md5", 101, 1024000, &user_idnr);
}
	
void setup(void)
{
	configure_debug(5,1,1);
	config_read(configFile);
	GetDBParams(&_db_params);
	db_connect();
	auth_connect();
	init_testuser1();
}

void teardown(void)
{
	db_disconnect();
}


/****************************************************************************************
 *
 *
 * TestCases for pipe.h
 *
 *
 ***************************************************************************************/


/**
 * \brief inserts a message in the database. The header of the message is 
 * supposed to be given. The rest of the message will be read from instream
 * \return 0
 */
//int insert_messages(struct DbmailMessage *message,
//		struct list *headerfields, 
//		struct list *dsnusers,
//		struct list *returnpath);

START_TEST(test_insert_messages)
{
	int result;
	struct DbmailMessage *message;
	struct list dsnusers, headerfields, returnpath;
	GString *tmp;
	deliver_to_user_t dsnuser;
	
	message = dbmail_message_new();
	tmp = g_string_new(raw_message);
	message = dbmail_message_init_with_string(message,tmp);

	list_init(&dsnusers);
	list_init(&headerfields);
	list_init(&returnpath);
	
	dsnuser_init(&dsnuser);
	dsnuser.address = "testuser1";
	list_nodeadd(&dsnusers, &dsnuser, sizeof(deliver_to_user_t));
	
	mime_fetch_headers(dbmail_message_hdrs_to_string(message), &headerfields);

	result = insert_messages(message, &headerfields, &dsnusers, &returnpath);

	fail_unless(result==0,"insert_messages failed");

	g_string_free(tmp,TRUE);
	dbmail_message_free(message);
}
END_TEST
/**
 * \brief discards all input coming from instream
 * \param instream FILE stream holding input from a client
 * \return 
 *      - -1 on error
 *      -  0 on success
 */
//int discard_client_input(FILE * instream);

/**
 * store a messagebody (without headers in one or more blocks in the database
 * \param message the message
 * \param message_size size of message
 * \param msgidnr idnr of message
 * \return 
 *     - -1 on error
 *     -  1 on success
 */
//int store_message_in_blocks(const char* message,
//				   u64_t message_size,
//				   u64_t msgidnr);



/****************************************************************************************
 *
 *
 * TestCases for auth.h
 *
 *
 ***************************************************************************************/

/*
 *
 * some utilities
 *
 */

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
	//fail_unless(maxmail_size>=0,"auth_getmaxmailsize return illegal maxmail_size");
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
START_TEST(test_auth_check_user_ext)
{
	struct list uids;
	struct list fwds;
	int checks = -1;
	int result;
	list_init(&uids);
	list_init(&fwds);
	result = auth_check_user_ext("foobar@foobar.org",&uids,&fwds,checks);
}
END_TEST
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
START_TEST(test_auth_change_username)
{
	u64_t user_idnr, new_idnr;
	char *old="beforerename";
	char *new="afterrename";
	int result;
	auth_adduser(old,"sometestfoopass", "md5", 101, 1024000, &user_idnr);
	auth_user_exists(old,&user_idnr);
	result = auth_change_username(user_idnr, new);
	auth_user_exists(new,&new_idnr);
	auth_delete_user(new);
	fail_unless(result==0,"auth_change_username failed");
	fail_unless(user_idnr==new_idnr,"auth_change_username: user_idnr mismatch");
}
END_TEST

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
START_TEST(test_auth_change_password)
{
	u64_t user_idnr;
	int result;
	char *userid = "testchangepass";
	auth_adduser(userid,"sometestpass","md5", 101, 1002400, &user_idnr);
	auth_user_exists(userid, &user_idnr);
	result = auth_change_password(user_idnr, "newtestpass", "md5");
	fail_unless(result==0,"auth_change_password failed");
	auth_delete_user(userid);
}
END_TEST
/**
 * \brief change a users client id
 * \param user_idnr
 * \param new_cid new client id
 * \return
 *    - -1 on failure
 *    -  0 on success
 */
//int auth_change_clientid(u64_t user_idnr, u64_t new_cid);
START_TEST(test_auth_change_clientid)
{
	u64_t user_idnr;
	int result;
	char *userid = "testchangeclientid";
	auth_adduser(userid, "testpass", "md5", 101, 1000, &user_idnr);
	auth_user_exists(userid, &user_idnr);
	result = auth_change_clientid(user_idnr, 102);
	fail_unless(result==0, "auth_change_clientid failed");
	auth_delete_user(userid);
}
END_TEST
/**
 * \brief change a user's mailbox size (maxmailsize)
 * \param user_idnr
 * \param new_size new size of mailbox
 * \return
 *    - -1 on failure
 *    -  0 on success
 */
//int auth_change_mailboxsize(u64_t user_idnr, u64_t new_size);
START_TEST(test_auth_change_mailboxsize)
{
	u64_t user_idnr;
	int result;
	char *userid = "testchangemaxm";
	auth_adduser(userid, "testpass", "md5", 101, 1000, &user_idnr);
	auth_user_exists(userid, &user_idnr);
	result = auth_change_mailboxsize(user_idnr, 2000);
	fail_unless(result==0, "auth_change_mailboxsize failed");
	auth_delete_user(userid);
}
END_TEST


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

START_TEST(test_auth_validate) 
{
	int result;
	u64_t user_idnr = 0;
	result = auth_validate("testuser1","test",&user_idnr);
	fail_unless(result==1,"auth_validate positive failure");
	fail_unless(user_idnr > 0,"auth_validate couldn't find user_idnr");
	
	user_idnr = 0;
	result = auth_validate("testuser1","wqer",&user_idnr);
	fail_unless(result==0,"auth_validate negative failure");
	fail_unless(user_idnr == 0,"auth_validate shouldn't find user_idnr");
}
END_TEST
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
START_TEST(test_auth_get_userid)
{
	u64_t testidnr;
	u64_t user_idnr = get_first_user_idnr();
	char *username = auth_get_userid(user_idnr);
	fail_unless(strlen(username)>3,"auth_get_userid failed");
	auth_user_exists(username, &testidnr);
	fail_unless(testidnr==user_idnr,"auth_get_userid: auth_user_exists returned wrong idnr");
}
END_TEST

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
 * \return aliases list of aliases
 */
//GList * auth_get_user_aliases(u64_t user_idnr);
START_TEST(test_auth_get_user_aliases)
{
	u64_t user_idnr;
	char *username="testuser1";
	GList *aliases;
	int result;
	result = auth_user_exists(username, &user_idnr);
	aliases = auth_get_user_aliases(user_idnr);
//	fail_unless(g_list_length(aliases)>1,"auth_get_user_aliases failed");
}
END_TEST
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

START_TEST(test_auth_addalias)
{
	int result;
	u64_t user_idnr;
	char *username="testuser1";
	result = auth_user_exists(username,&user_idnr);
	result = auth_addalias(user_idnr,"addalias@foobar.org",0);
	fail_unless(result==0,"auth_addalias failed");
}
END_TEST
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
START_TEST(test_auth_addalias_ext)
{
	int result;
	result = auth_addalias_ext("foobar@foo.org","foobar@bar.org",0);
	fail_unless(result==0,"auth_addalias_ext failed");
}
END_TEST
/**
 * \brief remove alias for user
 * \param user_idnr user id
 * \param alias the alias
 * \return
 *         - -1 on failure
 *         - 0 on success
 */
//int auth_removealias(u64_t user_idnr, const char *alias);
START_TEST(test_auth_removealias)
{
	int result;
	u64_t user_idnr;
	char *username="testuser1";
	result = auth_user_exists(username,&user_idnr);
	result = auth_removealias(user_idnr,"addalias@foobar.org");
	fail_unless(result==0,"auth_removealias failed");
}
END_TEST

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
START_TEST(test_auth_removealias_ext)
{
	int result;
	result = auth_removealias_ext("foobar@foo.org","foobar@bar.org");
	fail_unless(result==0,"auth_removealias_ext failed");
}
END_TEST


#ifdef AUTHLDAP
//char *dm_ldap_get_filter(const gchar boolean, const gchar *attribute, GList *values) 

START_TEST(test_dm_ldap_get_filter)
{	
	char *result;
	char *expect = "(&(objectClasses=top)(objectClasses=account)(objectClasses=dbmailUser))";
	GString *objclasses = g_string_new("top,account,dbmailUser");
	GList *l = g_string_split(objclasses,",");
	result = dm_ldap_get_filter('&',"objectClasses",l);
	fail_unless(strcmp(result,expect)==0,"dm_ldap_get_filter failed");	
}
END_TEST

START_TEST(test_dm_ldap_get_freeid)
{
	u64_t id;

	id = dm_ldap_get_freeid("uidNumber");
	fail_unless(id != 0,"dm_ldap_get_freeid failed");
	
	return;
}
END_TEST

#endif

START_TEST(test_dm_stresc)
{
	char *to;
	to = dm_stresc("test");
	fail_unless(strcmp(to,"test")==0,"dm_stresc failed 1");
	to = dm_stresc("test's");
	fail_unless(strcmp(to,"test\\'s")==0,"dm_stresc failed 2");
	g_free(to);
}
END_TEST

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
	tcase_add_test(tc_auth, test_auth_check_user_ext);
	tcase_add_test(tc_auth, test_auth_adduser);
	tcase_add_test(tc_auth, test_auth_delete_user);
	tcase_add_test(tc_auth, test_auth_change_username);
	tcase_add_test(tc_auth, test_auth_change_password);
	tcase_add_test(tc_auth, test_auth_change_clientid);
	tcase_add_test(tc_auth, test_auth_change_mailboxsize);
	tcase_add_test(tc_auth, test_auth_validate);
//	tcase_add_test(tc_auth, test_auth_md5_validate);
	tcase_add_test(tc_auth, test_auth_get_userid);
//	tcase_add_test(tc_auth, test_auth_get_users_from_clientid);
	tcase_add_test(tc_auth, test_auth_get_user_aliases);
	tcase_add_test(tc_auth, test_auth_addalias);
	tcase_add_test(tc_auth, test_auth_addalias_ext);
	tcase_add_test(tc_auth, test_auth_removealias);
	tcase_add_test(tc_auth, test_auth_removealias_ext);
#ifdef AUTHLDAP
	tcase_add_test(tc_auth, test_dm_ldap_get_filter);
	tcase_add_test(tc_auth, test_dm_ldap_get_freeid);
#endif

	TCase *tc_pipe = tcase_create("Pipe");
	suite_add_tcase(s, tc_pipe);
	tcase_add_checked_fixture(tc_pipe, setup, teardown);
	tcase_add_test(tc_pipe, test_insert_messages);

	TCase *tc_misc = tcase_create("Misc");
	suite_add_tcase(s, tc_misc);
	tcase_add_checked_fixture(tc_misc, setup, teardown);
	tcase_add_test(tc_misc, test_dm_stresc);
	
	
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
	

