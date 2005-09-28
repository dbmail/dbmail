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
 *  $Id: check_dbmail_deliver.c 1829 2005-08-01 14:53:53Z paul $ 
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
#include "dbmail-message.h"
#include "dbmail-mailbox.h"
#include <gmime/gmime.h>

#include "check_dbmail.h"

extern char * multipart_message;
extern char * configFile;
extern db_param_t _db_params;


/* we need this one because we can't directly link imapd.o */
int imap_before_smtp = 0;
	
static void init_testuser1(void) 
{
        u64_t user_idnr;
	if (! (auth_user_exists("testuser1",&user_idnr)))
		auth_adduser("testuser1","test", "md5", 101, 1024000, &user_idnr);
}

static u64_t get_first_user_idnr(void)
{
	u64_t user_idnr;
	GList *users = auth_get_known_users();
	users = g_list_first(users);
	auth_user_exists((char *)users->data,&user_idnr);
	return user_idnr;
}


void setup(void)
{
	configure_debug(5,1,0);
	config_read(configFile);
	GetDBParams(&_db_params);
	db_connect();
	auth_connect();
	g_mime_init(0);
	init_testuser1();
}

void teardown(void)
{
	auth_disconnect();
	db_disconnect();
	config_free();
	g_mime_shutdown();
}


/****************************************************************************************
 *
 *
 * TestCases
 *
 *
 ***************************************************************************************/
		
Suite *dbmail_mailbox_suite(void)
{
	Suite *s = suite_create("Dbmail Mailbox");

	TCase *tc_mailbox = tcase_create("Mailbox");
	suite_add_tcase(s, tc_mailbox);
	tcase_add_checked_fixture(tc_mailbox, setup, teardown);
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
	tcase_add_test(tc_misc, test_dm_valid_format);
//	tcase_add_test(tc_misc, test_dm_strip_folder);
//	tcase_add_test(tc_misc, test_dm_valid_folder);

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
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
	

