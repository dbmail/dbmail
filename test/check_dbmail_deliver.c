/*
 *   Copyright (c) 2004-2013 NFG Net Facilities Group BV support@nfg.nl
 *   Copyright (c) 2014-2019 Paul J Stevens, The Netherlands, support@nfg.nl
 *   Copyright (c) 2020-2023 Alan Hicks, Persistent Objects Ltd support@p-o.co.uk
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
 *   
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

#include <check.h>
#include "check_dbmail.h"

extern char *multipart_message;
extern char configFile[PATH_MAX];

/* we need this one because we can't directly link imapd.o */
int imap_before_smtp = 0;
	
void init_testuser1(void) 
{
        uint64_t user_idnr;

	char user1[] = "testuser1";
	char user2[] = "testuser2";
	char passwd[] = "test";
	char passwdtype[] = "md5-hash";
	char *password = NULL;
	char *enctype = NULL;
	char *passwdfile = NULL;

	if (! (auth_user_exists(user1,&user_idnr))) {
		mkpassword(user1, passwd, passwdtype, passwdfile, &password, &enctype);
		auth_adduser(user1,password, enctype, 101, 1024000, &user_idnr);
	}

	if (! (auth_user_exists(user2,&user_idnr))) {
		mkpassword(user2, passwd, passwdtype, passwdfile, &password, &enctype);
		auth_adduser(user2,password, enctype, 101, 1024000, &user_idnr);
	}

}
	
void setup(void)
{
	config_get_file();
	config_read(configFile);
	configure_debug(NULL,255,0);
	GetDBParams();
	db_connect();
	auth_connect();
	init_testuser1();
}

void teardown(void)
{
	auth_disconnect();
	db_disconnect();
	config_free();
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
//int insert_messages(DbmailMessage *message, GList *dsnusers)

START_TEST(test_insert_messages)
{
	int result;
	DbmailMessage *message;
	Mempool_T pool = mempool_open();
	List_T dsnusers = p_list_new(pool);
	Delivery_T *dsnuser = g_new0(Delivery_T,1);
	
	message = dbmail_message_new(NULL);
	message = dbmail_message_init_with_string(message,multipart_message);

	dsnuser_init(dsnuser);
	dsnuser->address = g_strdup("testuser1");
	dsnusers = p_list_prepend(dsnusers, dsnuser);
	
	result = insert_messages(message, dsnusers);

	fail_unless(result==0,"insert_messages failed");

	dsnuser_free_list(dsnusers);
	dbmail_message_free(message);
	mempool_close(&pool);
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
//				   uint64_t message_size,
//				   uint64_t msgidnr);



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

static uint64_t get_first_user_idnr(void);

uint64_t get_first_user_idnr(void)
{
	uint64_t user_idnr;
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
	auth_connect();
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
//int auth_user_exists(const char *username, uint64_t * user_idnr);
START_TEST(test_auth_user_exists)
{
	uint64_t uid;
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
//int auth_getclientid(uint64_t user_idnr, uint64_t * client_idnr);

START_TEST(test_auth_getclientid)
{
	int result;
	uint64_t client_idnr;
	uint64_t user_idnr = get_first_user_idnr();
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
//int auth_getmaxmailsize(uint64_t user_idnr, uint64_t * maxmail_size);
START_TEST(test_auth_getmaxmailsize)
{
	int result;
	uint64_t maxmail_size;
	uint64_t user_idnr = get_first_user_idnr();
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
//char *auth_getencryption(uint64_t user_idnr);
START_TEST(test_auth_getencryption)
{
	char * result = NULL;
	uint64_t user_idnr = get_first_user_idnr();
	result = auth_getencryption(user_idnr);
	fail_unless(result!=NULL,"auth_getencryption failed");
	g_free(result);
}
END_TEST


/**
 * \brief as auth_check_user() but adds the numeric ID of the user found to
 * userids or the forward to the fwds list
 * \param username
 * \param userids list of user id's (empty on call)
 * \param fwds list of forwards (emoty on call)
 * \param checks used internally, \b should be 0 on call
 * \return number of deliver_to addresses found
 */
//int auth_check_user_ext(const char *username, GList **userids, GList **fwds, int checks);
START_TEST(test_auth_check_user_ext)
{
	GList *uids = NULL;
	GList *fwds = NULL;
	int checks = 0;
	int result;
	result = auth_check_user_ext("foobar@foobar.org",&uids,&fwds,checks);
	fail_unless(result >= 0);
	g_list_destroy(uids);
	g_list_destroy(fwds);
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
//		 uint64_t clientid, uint64_t maxmail, uint64_t * user_idnr);

START_TEST(test_auth_adduser)
{
	int result;
	uint64_t user_idnr;
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
	fail_unless(result==TRUE,"auth_delete_user failed");
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
//int auth_change_username(uint64_t user_idnr, const char *new_name);
START_TEST(test_auth_change_username)
{
	uint64_t user_idnr, new_idnr;
	char *old="beforerename";
	char *new="afterrename";
	int result;
	auth_adduser(old,"sometestfoopass", "md5", 101, 1024000, &user_idnr);
	auth_user_exists(old,&user_idnr);
	result = auth_change_username(user_idnr, new);
	auth_user_exists(new,&new_idnr);
	auth_delete_user(new);
	fail_unless(result==TRUE,"auth_change_username failed");
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
//int auth_change_password(uint64_t user_idnr,
//			 const char *new_pass, const char *enctype);
START_TEST(test_auth_change_password)
{
	uint64_t user_idnr;
	int result;
	char *userid = "testchangepass";
	auth_adduser(userid,"sometestpass","md5", 101, 1002400, &user_idnr);
	auth_user_exists(userid, &user_idnr);
	result = auth_change_password(user_idnr, "newtestpass", "md5");
	fail_unless(result==TRUE,"auth_change_password failed");
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
//int auth_change_clientid(uint64_t user_idnr, uint64_t new_cid);
START_TEST(test_auth_change_clientid)
{
	uint64_t user_idnr;
	int result;
	char *userid = "testchangeclientid";
	auth_adduser(userid, "testpass", "md5", 101, 1000, &user_idnr);
	auth_user_exists(userid, &user_idnr);
	result = auth_change_clientid(user_idnr, 102);
	fail_unless(result=TRUE, "auth_change_clientid failed");
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
//int auth_change_mailboxsize(uint64_t user_idnr, uint64_t new_size);
START_TEST(test_auth_change_mailboxsize)
{
	uint64_t user_idnr;
	int result;
	char *userid = "testchangemaxm";
	auth_adduser(userid, "testpass", "md5", 101, 1000, &user_idnr);
	auth_user_exists(userid, &user_idnr);
	result = auth_change_mailboxsize(user_idnr, 2000);
	fail_unless(result==TRUE, "auth_change_mailboxsize failed");
	auth_delete_user(userid);
}
END_TEST


/**
 * \brief try to validate a user (used for login to server). 
 * \param username 
 * \param password
 * \param user_idnr will hold the user_idnr after return. Must be a pointer
 * to a valid uint64_t variable on call.
 * \return
 *     - -1 on error
 *     -  0 if not validated
 *     -  1 if OK
 */
//int auth_validate(char *username, char *password, uint64_t * user_idnr);

static ClientBase_T * ci_new(void)
{
	ClientBase_T *ci = g_new0(ClientBase_T,1);
	FILE *fd = fopen("/dev/null","w");
	ci->rx = fileno(stdin);
	ci->tx = dup(fileno(fd));
	fclose(fd);
	return ci;
}


START_TEST(test_auth_validate) 
{
	int result;
	ClientBase_T *ci = ci_new();

	uint64_t user_idnr = 0;
	result = auth_validate(ci,"testuser1","test",&user_idnr);
	fail_unless(result==TRUE,"auth_validate positive failure [%d:%" PRIu64 "]", result, user_idnr);
	fail_unless(user_idnr > 0,"auth_validate couldn't find user_idnr");
	
	user_idnr = 0;
	result = auth_validate(ci,"testuser1","wqer",&user_idnr);
	fail_unless(result==FALSE,"auth_validate negative failure");
	fail_unless(user_idnr == 0,"auth_validate shouldn't find user_idnr");

	close(ci->tx);
	g_free(ci);
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
//uint64_t auth_md5_validate(char *username, unsigned char *md5_apop_he,
//			char *apop_stamp);
/**
 * \brief get username for a user_idnr
 * \param user_idnr
 * \return
 *    - NULL on error
 *    - username otherwise
 * \attention caller should free username string
 */
//char *auth_get_userid(uint64_t user_idnr);
START_TEST(test_auth_get_userid)
{
	uint64_t testidnr;
	uint64_t user_idnr = get_first_user_idnr();
	char *username = auth_get_userid(user_idnr);
	fail_unless(strlen(username)>3,"auth_get_userid failed");
	auth_user_exists(username, &testidnr);
	fail_unless(testidnr==user_idnr,"auth_get_userid: auth_user_exists returned wrong idnr");
	g_free(username);
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
//int auth_get_users_from_clientid(uint64_t client_id, 
//			       uint64_t ** user_ids,
//			       unsigned *num_users);

/**
 * \brief get a list of aliases associated with a user's user_idnr
 * \param user_idnr idnr of user
 * \return aliases list of aliases
 */
//GList * auth_get_user_aliases(uint64_t user_idnr);
START_TEST(test_auth_get_user_aliases)
{
	uint64_t user_idnr;
	char *username = "testaliasuser";
	char *passwd = "test";
	char *passwdtype = "md5-hash";
	char *password = NULL;
	char *enctype = NULL;
	if (! (auth_user_exists(username,&user_idnr))) {
		mkpassword(username, passwd, passwdtype, NULL, &password, &enctype);
		auth_adduser(username,password, enctype, 101, 1024000, &user_idnr);
	}
	GList *aliases;
	int result;
	result = auth_user_exists(username, &user_idnr);
	aliases = auth_get_user_aliases(user_idnr);
	fail_unless(result >= 0);
	fail_unless(aliases == NULL);
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
//int auth_addalias(uint64_t user_idnr, const char *alias, uint64_t clientid);

START_TEST(test_auth_addalias)
{
	int result;
	uint64_t user_idnr;
	char *username="testuser1";
	result = auth_user_exists(username,&user_idnr);
	result = auth_addalias(user_idnr,"addalias@foobar.org",0);
	fail_unless(result==TRUE,"auth_addalias failed");
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
//		    uint64_t clientid);
START_TEST(test_auth_addalias_ext)
{
	int result;
	result = auth_addalias_ext("foobar@foo.org","foobar@bar.org",0);
	fail_unless(result==TRUE,"auth_addalias_ext failed");
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
//int auth_removealias(uint64_t user_idnr, const char *alias);
START_TEST(test_auth_removealias)
{
	int result;
	uint64_t user_idnr;
	char *username="testuser1";
	result = auth_user_exists(username,&user_idnr);
	result = auth_removealias(user_idnr,"addalias@foobar.org");
	fail_unless(result==TRUE,"auth_removealias failed");
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
	fail_unless(result==TRUE,"auth_removealias_ext failed");
}
END_TEST

START_TEST(test_dm_valid_format)
{
	fail_unless(dm_valid_format("some-%s@foo.com")==0,"dm_valid_format");
	fail_unless(dm_valid_format("some-%s@%s.foo.com")==1,"dm_valid_format");
	fail_unless(dm_valid_format("some-%@foo.com")==1,"dm_valid_format");
	fail_unless(dm_valid_format("some-%@%foo.com")==1,"dm_valid_format");
	fail_unless(dm_valid_format("some-%%s@foo.com")==1,"dm_valid_format");
	fail_unless(dm_valid_format("some@foo.com")==1,"dm_valid_format");
}
END_TEST

START_TEST(test_g_list_join)
{
	GString *result;
	GList *l = NULL;
	l = g_list_append(l, "NIL");
	l = g_list_append(l, "NIL");
	l = g_list_append(l, "(NIL NIL)");
	l = g_list_append(l, "(NIL NIL)");
	result = g_list_join(l," ");
	fail_unless(strcmp(result->str,"NIL NIL (NIL NIL) (NIL NIL)")==0,"g_list_join failed");
	g_string_free(result,TRUE);

	l = NULL;
	l = g_list_append(l, "NIL");
	result = g_list_join(l," ");
	fail_unless(strcmp(result->str,"NIL")==0,"g_list_join failed");
	g_string_free(result,TRUE);

}
END_TEST

START_TEST(test_g_string_split)
{
	GString *s = g_string_new("");
	GList *l = NULL;

	g_string_printf(s,"a b");
	l = g_string_split(s," ");

	fail_unless(g_list_length(l)==2,"g_string_split failed");
	g_list_destroy(l);

	g_string_printf(s,"a,b,c");
	l = g_string_split(s,",");

	fail_unless(g_list_length(l)==3,"g_string_split failed");
	g_list_destroy(l);

	l = g_string_split(s," ");

	fail_unless(g_list_length(l)==1,"g_string_split failed");
	g_list_destroy(l);

	g_string_free(s,TRUE);
}
END_TEST

START_TEST(test_g_tree_keys)
{
	GTree *a;
	GList *akeys;
	uint64_t *k, *v;
	int i=0;
	
	a = g_tree_new_full((GCompareDataFunc)ucmpdata,NULL,(GDestroyNotify)g_free,(GDestroyNotify)g_free);
	akeys = g_tree_keys(a);

	fail_unless(g_tree_nnodes(a)==0,"g_tree_keys failed");
	fail_unless(g_list_length(akeys)==0,"g_tree_keys failed");

	akeys = g_list_first(akeys);
	g_list_free(akeys);
	
	for (i=0; i<4; i++) {
		k = g_new0(uint64_t,1);
		v = g_new0(uint64_t,1);
		*k = i;
		*v = i;
		g_tree_insert(a,k,v);
	}
	
	akeys = g_tree_keys(a);
	fail_unless(g_tree_nnodes(a)==4,"g_tree_keys failed");
	fail_unless(g_list_length(akeys)==4,"g_tree_keys failed");
	
	akeys = g_list_first(akeys);
	g_list_free(akeys);
	g_tree_destroy(a);
}
END_TEST


/*
 * boolean merge of two GTrees. The result is stored in GTree *a.
 * the state of GTree *b is undefined: it may or may not have been changed, 
 * depending on whether or not key/value pairs were moved from b to a.
 * Both trees are safe to destroy afterwards, assuming g_tree_new_full was used
 * for their construction.
 */

static void tree_add_key(GTree *t, uint64_t u)
{
	uint64_t *k, *v;

	k = g_new0(uint64_t,1);
	v = g_new0(uint64_t,1);
	*k = u;
	*v = u;
	g_tree_insert(t,k,v);
}

START_TEST(test_g_tree_merge_not)
{
	uint64_t r = 0;
	GTree *a, *b;
	
	a = g_tree_new_full((GCompareDataFunc)ucmpdata,NULL,(GDestroyNotify)g_free,(GDestroyNotify)g_free);
	b = g_tree_new_full((GCompareDataFunc)ucmpdata,NULL,(GDestroyNotify)g_free,(GDestroyNotify)g_free);
	
	tree_add_key(b,1);
	for (r=90000; r<=100000; r+=2) {
		tree_add_key(b, r);
	}

	g_tree_merge(a,b,IST_SUBSEARCH_NOT);
	fail_unless(g_tree_nnodes(a)==5002,"g_tree_merge failed. Too few nodes in a. [%d]", g_tree_nnodes(a));
	
	g_tree_destroy(a);
	g_tree_destroy(b);
	
	a = g_tree_new_full((GCompareDataFunc)ucmpdata,NULL,(GDestroyNotify)g_free,(GDestroyNotify)g_free);
	b = g_tree_new_full((GCompareDataFunc)ucmpdata,NULL,(GDestroyNotify)g_free,(GDestroyNotify)g_free);
	
	tree_add_key(a, 1);
	for (r=90000; r<=100000; r+=2)
		tree_add_key(a, r);

	g_tree_merge(a,b,IST_SUBSEARCH_NOT);
	fail_unless(g_tree_nnodes(a)==5002,"g_tree_merge failed. Too few nodes in a. [%d]", g_tree_nnodes(a));
	
	g_tree_destroy(a);
	g_tree_destroy(b);

}
END_TEST

START_TEST(test_g_tree_merge_or)
{
	uint64_t r = 0;
	GTree *a, *b;
	
	a = g_tree_new_full((GCompareDataFunc)ucmpdata,NULL,(GDestroyNotify)g_free,(GDestroyNotify)g_free);
	b = g_tree_new_full((GCompareDataFunc)ucmpdata,NULL,(GDestroyNotify)g_free,(GDestroyNotify)g_free);
	
	tree_add_key(b, 1);
	for (r=10000; r<=100000; r+=2)
		tree_add_key(b, r);

	g_tree_merge(a,b,IST_SUBSEARCH_OR);
	fail_unless(g_tree_nnodes(a)==45002,"g_tree_merge failed. Too many nodes in a. [%d]", g_tree_nnodes(a));
	
	g_tree_destroy(a);
	g_tree_destroy(b);

}
END_TEST

START_TEST(test_g_tree_merge_and)
{
	uint64_t r = 0;
	GTree *a, *b;
	
	a = g_tree_new_full((GCompareDataFunc)ucmpdata,NULL,(GDestroyNotify)g_free,(GDestroyNotify)g_free);
	b = g_tree_new_full((GCompareDataFunc)ucmpdata,NULL,(GDestroyNotify)g_free,(GDestroyNotify)g_free);
	
	tree_add_key(a, 1);
	for (r=10000; r<100000; r+=10) {
		tree_add_key(a, r);
	}

	tree_add_key(b, 1);
	for (r=10000; r<100000; r++) {
		tree_add_key(b, r);
	}

	g_tree_merge(a,b,IST_SUBSEARCH_AND);
	fail_unless(g_tree_nnodes(a)==9001,"g_tree_merge failed. Too few nodes in a.[%d]", g_tree_nnodes(a));
	
	g_tree_destroy(a);
	g_tree_destroy(b);

	a = g_tree_new_full((GCompareDataFunc)ucmpdata,NULL,(GDestroyNotify)g_free,(GDestroyNotify)g_free);
	b = g_tree_new_full((GCompareDataFunc)ucmpdata,NULL,(GDestroyNotify)g_free,(GDestroyNotify)g_free);
	
	tree_add_key(a, 1);
	for (r=10000; r<100000; r+=2)
		tree_add_key(a, r);

	tree_add_key(b, 1);
	for (r=10000; r<100000; r++)
		tree_add_key(b, r);

	g_tree_merge(a,b,IST_SUBSEARCH_AND);
	fail_unless(g_tree_nnodes(a)==45001,"g_tree_merge failed. Too few nodes in a. [%d]", g_tree_nnodes(a));
	
	g_tree_destroy(a);
	g_tree_destroy(b);

}
END_TEST


START_TEST(test_find_bounded)
{
	char *newaddress;
	size_t newaddress_len, last_pos;

	find_bounded("fail+success@failure", '+', '@',
			&newaddress, &newaddress_len, &last_pos);

	fail_unless(strcmp("success", newaddress)==0,
			"find_bounded is broken. "
			"Should be success: %s", newaddress);

	g_free(newaddress);
}
END_TEST

START_TEST(test_zap_between_both)
{
	char *newaddress;
	size_t newaddress_len, zapped_len;

	zap_between("suc+failure@cess", -'+', -'@',
			&newaddress, &newaddress_len, &zapped_len);

	fail_unless(strcmp("success", newaddress)==0,
			"zap_between is both broken. "
			"Should be success: %s", newaddress);

	g_free(newaddress);
}
END_TEST

START_TEST(test_zap_between_left)
{
	char *newaddress;
	size_t newaddress_len, zapped_len;

	zap_between("suc+failure@cess", -'+', '@',
			&newaddress, &newaddress_len, &zapped_len);

	fail_unless(strcmp("suc@cess", newaddress)==0,
			"zap_between is left broken. "
			"Should be suc@cess: %s", newaddress);

	g_free(newaddress);
}
END_TEST

START_TEST(test_zap_between_right)
{
	char *newaddress;
	size_t newaddress_len, zapped_len;

	zap_between("suc+failure@cess", '+', -'@',
			&newaddress, &newaddress_len, &zapped_len);

	fail_unless(strcmp("suc+cess", newaddress)==0,
			"zap_between is right broken. "
			"Should be suc+cess: %s", newaddress);

	g_free(newaddress);
}
END_TEST

START_TEST(test_zap_between_center)
{
	char *newaddress;
	size_t newaddress_len, zapped_len;

	zap_between("suc+failure@cess", '+', '@',
			&newaddress, &newaddress_len, &zapped_len);

	fail_unless(strcmp("suc+@cess", newaddress)==0,

			"zap_between is center broken. "
			"Should be suc+@cess: %s", newaddress);

	g_free(newaddress);
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

	TCase *tc_pipe = tcase_create("Pipe");
	suite_add_tcase(s, tc_pipe);
	tcase_add_checked_fixture(tc_pipe, setup, teardown);
	tcase_add_test(tc_pipe, test_insert_messages);

	TCase *tc_misc = tcase_create("Misc");
	suite_add_tcase(s, tc_misc);
	tcase_add_checked_fixture(tc_misc, setup, teardown);
	tcase_add_test(tc_misc, test_dm_valid_format);
	tcase_add_test(tc_misc, test_g_list_join);
	tcase_add_test(tc_misc, test_g_string_split);
	tcase_add_test(tc_misc, test_g_tree_keys);
	tcase_add_test(tc_misc, test_g_tree_merge_or);
	tcase_add_test(tc_misc, test_g_tree_merge_and);
	tcase_add_test(tc_misc, test_g_tree_merge_not);
	tcase_add_test(tc_misc, test_zap_between_both);
	tcase_add_test(tc_misc, test_zap_between_left);
	tcase_add_test(tc_misc, test_zap_between_right);
	tcase_add_test(tc_misc, test_find_bounded);
	tcase_add_test(tc_misc, test_zap_between_center);
	return s;
}

int main(void)
{
	int nf;
	Suite *s = dbmail_deliver_suite();
	SRunner *sr = srunner_create(s);
	g_mime_init();
	srunner_run_all(sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	g_mime_shutdown();
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
	

