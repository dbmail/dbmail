/*
 *   Copyright (c) 2004-2013 NFG Net Facilities Group BV support@nfg.nl
 *   Copyright (c) 2014-2019 Paul J Stevens, The Netherlands, support@nfg.nl
 *   Copyright (c) 2020-2026 Alan Hicks, Persistent Objects Ltd support@p-o.co.uk
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
 *   Initialises test users
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

static ClientBase_T * ci_new(void)
{
  ClientBase_T *ci = g_new0(ClientBase_T,1);
  FILE *fd = fopen("/dev/null","w");
  ci->rx = fileno(stdin);
  ci->tx = dup(fileno(fd));
  fclose(fd);
  return ci;
}

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

Suite *dbmail_init_suite(void)
{
	Suite *s = suite_create("Dbmail Init");

	TCase *tc_init = tcase_create("Init");
	suite_add_tcase(s, tc_init);
	tcase_add_checked_fixture(tc_init, setup, teardown);

	// tcase_add_test(tc_auth, test_auth_connect);
	// tcase_add_test(tc_auth, test_auth_disconnect);
	// tcase_add_test(tc_auth, test_auth_user_exists);
	// tcase_add_test(tc_auth, test_auth_get_known_users);
	// tcase_add_test(tc_auth, test_auth_getclientid);
	// tcase_add_test(tc_auth, test_auth_getmaxmailsize);
	// tcase_add_test(tc_auth, test_auth_getencryption);
	// tcase_add_test(tc_auth, test_auth_check_user_ext);
	// tcase_add_test(tc_auth, test_auth_adduser);
	// tcase_add_test(tc_auth, test_auth_delete_user);
	// tcase_add_test(tc_auth, test_auth_change_username);
	// tcase_add_test(tc_auth, test_auth_change_password);
	// tcase_add_test(tc_auth, test_auth_change_clientid);
	// tcase_add_test(tc_auth, test_auth_change_mailboxsize);
	// tcase_add_test(tc_auth, test_auth_validate);
//	tcase_add_test(tc_auth, test_auth_md5_validate);
	// tcase_add_test(tc_auth, test_auth_get_userid);
//	tcase_add_test(tc_auth, test_auth_get_users_from_clientid);
	// tcase_add_test(tc_auth, test_auth_get_user_aliases);
	// tcase_add_test(tc_auth, test_auth_addalias);
	// tcase_add_test(tc_auth, test_auth_addalias_ext);
	// tcase_add_test(tc_auth, test_auth_removealias);
	// tcase_add_test(tc_auth, test_auth_removealias_ext);

	// TCase *tc_pipe = tcase_create("Pipe");
	// suite_add_tcase(s, tc_pipe);
	// tcase_add_checked_fixture(tc_pipe, setup, teardown);
	// tcase_add_test(tc_pipe, test_insert_messages);

	// // TCase *tc_misc = tcase_create("Misc");
	// // suite_add_tcase(s, tc_misc);
	// tcase_add_checked_fixture(tc_misc, setup, teardown);
	// tcase_add_test(tc_misc, test_dm_valid_format);
	// tcase_add_test(tc_misc, test_g_list_join);
	// tcase_add_test(tc_misc, test_g_string_split);
	// tcase_add_test(tc_misc, test_g_tree_keys);
	// tcase_add_test(tc_misc, test_g_tree_merge_or);
	// tcase_add_test(tc_misc, test_g_tree_merge_and);
	// tcase_add_test(tc_misc, test_g_tree_merge_not);
	// tcase_add_test(tc_misc, test_zap_between_both);
	// tcase_add_test(tc_misc, test_zap_between_left);
	// tcase_add_test(tc_misc, test_zap_between_right);
	// tcase_add_test(tc_misc, test_find_bounded);
	// tcase_add_test(tc_misc, test_zap_between_center);
  tcase_add_test(tc_init, test_auth_validate);
	return s;
}

int main(void)
{
	int nf;
	Suite *s = dbmail_init_suite();
	SRunner *sr = srunner_create(s);
	g_mime_init();
	srunner_run_all(sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	g_mime_shutdown();
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
	

