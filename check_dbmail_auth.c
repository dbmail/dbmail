/*
 *  Copyright (C) 2006  Aaron Stone  <aaron@serendipity.cx>
 *  Copyright (c) 2005-2006 NFG Net Facilities Group BV support@nfg.nl
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
 *  $Id: check_dbmail_common.c 1598 2005-02-23 08:41:02Z paul $ 
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

extern char *configFile;
extern db_param_t _db_params;
extern int quiet;
extern int reallyquiet;

/*
 *
 * the test fixtures
 *
 */
void setup(void)
{
	reallyquiet = 1;
	configure_debug(5,0);
	config_read(configFile);
	GetDBParams(&_db_params);
	db_connect();
	auth_connect();
}

void teardown(void)
{
	db_disconnect();
	auth_disconnect();
}

START_TEST(test_auth_validate)
{
	u64_t user_idnr;
	int result;
	
	result = auth_validate(NULL, "testuser1", "test", &user_idnr);
	fail_unless(result==1,"auth_validate failed [%d]", result);
	result = auth_validate(NULL, "nosuchtestuser", "testnosuchlogin", &user_idnr);
	fail_unless(result==0,"auth_validate failed [%d]", result);

}
END_TEST

START_TEST(test_auth_change_password)
{
	u64_t user_idnr, user_idnr_check;
	int result, i;
	char *userid = "testchangepass";
	char *passwd = "newpassword";
	char *password;
	char *enctype;

	if (!auth_user_exists(userid, &user_idnr))
		auth_adduser(userid,"initialpassword","", 101, 1002400, &user_idnr);

	// Taken from dbmail-user.c, mkpassword:
	/* These are the easy text names. */
	const char * const pwtypes[] = {
		"plaintext",	"plaintext",	"crypt",	"crypt",
		"md5", 		"md5",		"md5sum",	"md5sum", 
		"md5-hash",	"md5-hash",	"md5-base64",	"md5-digest",
		"", 	"", 			NULL
	};

	for (i = 0; pwtypes[i] != NULL; i++) {
		result = mkpassword(userid, passwd, pwtypes[i], "", &password, &enctype);
		fail_unless(result==0, "Unable to create password using mkpassword, pwtype [%s].", pwtypes[i]);

		result = auth_change_password(user_idnr, password, enctype);
		fail_unless(result==0,"auth_change_password failed");

		result = auth_validate(NULL, userid, passwd, &user_idnr_check);
		fail_unless(result==1,"auth_validate failed, pwtype [%s]", pwtypes[i]);
		fail_unless(user_idnr_check == user_idnr, "User ID number mismatch from auth_validate.");
	}

	auth_delete_user(userid);
}
END_TEST

START_TEST(test_auth_change_password_raw)
{
	u64_t user_idnr, user_idnr_check;
	int result, i;
	char *userid = "testchangepass";
	char *passwd = "yourtest";
	char *password;
	char *enctype;

	if (!auth_user_exists(userid, &user_idnr))
		auth_adduser(userid,"initialpassword","", 101, 1002400, &user_idnr);

	// Taken from dbmail-user.c, mkpassword:
	/* These are the easy text names. */
	const char * const pwtypes[] = {
		"plaintext-raw",	"crypt-raw",
		"md5-raw",		"md5sum-raw", 
		"md5-hash-raw",		"md5-digest-raw",
		"md5-base64-raw",	NULL
	};

	// Passwords courtest of M.J. O'Brien.
	const char * const rawpasswds[] = {
		"yourtest",                             "sixG/7CU2FOtg",
		"$1$rjN6/GVE$6rPnLX388iJ1Dt7J/LRPf.",   "b22766fada4a17d0f1a67c258a1d93d7", 
		"$1$rjN6/GVE$6rPnLX388iJ1Dt7J/LRPf.",   "b22766fada4a17d0f1a67c258a1d93d7", 
		"sidm+tpKF9Dxpnwlih2T1w==",   NULL
	};

	for (i = 0; pwtypes[i] != NULL; i++) {
		result = mkpassword(userid, rawpasswds[i], pwtypes[i], "", &password, &enctype);
		fail_unless(result==0, "Unable to create password using mkpassword, pwtype [%s].", pwtypes[i]);

		fail_unless(strcmp(password, rawpasswds[i])==0, "Passwords don't match after mkpassword.");

		result = auth_change_password(user_idnr, password, enctype);
		fail_unless(result==0,"auth_change_password failed");

		result = auth_validate(NULL, userid, passwd, &user_idnr_check);
		fail_unless(result==1,"auth_validate failed, pwtype [%s]", pwtypes[i]);
		fail_unless(user_idnr_check == user_idnr, "User ID number mismatch from auth_validate.");
	}

	auth_delete_user(userid);
}
END_TEST

Suite *dbmail_common_suite(void)
{
	Suite *s = suite_create("Dbmail Auth");
	TCase *tc_auth = tcase_create("Auth");
	
	suite_add_tcase(s, tc_auth);
	
	tcase_add_checked_fixture(tc_auth, setup, teardown);
	tcase_add_test(tc_auth, test_auth_validate);
	tcase_add_test(tc_auth, test_auth_change_password);
	tcase_add_test(tc_auth, test_auth_change_password_raw);
	
	return s;
}

int main(void)
{
	int nf;
	Suite *s = dbmail_common_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
	

