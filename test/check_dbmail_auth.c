/*
 *   Copyright (C) 2006  Aaron Stone  <aaron@serendipity.cx>
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
#include "dm_cram.h"

extern char configFile[PATH_MAX];
extern int quiet;
extern int reallyquiet;

static ClientBase_T * ci_new(void)
{
	Mempool_T pool = mempool_open();
	ClientBase_T *ci = mempool_pop(pool, sizeof(ClientBase_T));
	FILE *fd = fopen("/dev/null","w");
	ci->rx = fileno(stdin);
	ci->tx = fileno(fd);
	ci->pool = pool;
	return ci;
}

static void ci_delete(ClientBase_T *ci)
{
	Mempool_T pool = ci->pool;
	mempool_push(pool, ci, sizeof(ClientBase_T));
	mempool_close(&pool);
}

/*
 *
 * the test fixtures
 *
 */
void setup(void)
{
	reallyquiet = 1;
	config_get_file();
	config_read(configFile);
	configure_debug(NULL,511,0);
	GetDBParams();
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
	uint64_t user_idnr;
	int result;
	ClientBase_T *ci = ci_new();	

	result = auth_validate(ci, "testuser1", "test", &user_idnr);
	fail_unless(result==1,"auth_validate failed [%d]", result);
	result = auth_validate(ci, "nosuchtestuser", "testnosuchlogin", &user_idnr);
	fail_unless(result==0,"auth_validate failed [%d]", result);

	ci_delete(ci);
}
END_TEST

#if 0
START_TEST(test_auth_change_password)
{
	uint64_t user_idnr, user_idnr_check;
	int result, i;
	char *userid = "testchangepass";
	char *passwd = "newpassword";
	char *password;
	char *enctype;
	ClientBase_T *ci = ci_new();

	if (!auth_user_exists(userid, &user_idnr))
		auth_adduser(userid,"initialpassword","", 101, 1002400, &user_idnr);

	// Taken from dm_user.c, mkpassword:
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

		result = auth_validate(ci, userid, passwd, &user_idnr_check);
		fail_unless(result==1,"auth_validate failed, pwtype [%s]", pwtypes[i]);
		fail_unless(user_idnr_check == user_idnr, "User ID number mismatch from auth_validate.");
	}

	auth_delete_user(userid);
}
END_TEST

START_TEST(test_auth_change_password_raw)
{
	uint64_t user_idnr, user_idnr_check;
	int result, i;
	const char *userid = "testchangepass";
	const char *passwd = "yourtest";
	char *password;
	char *enctype;
	ClientBase_T *ci = ci_new();

	if (!auth_user_exists(userid, &user_idnr))
		auth_adduser(userid,"initialpassword","", 101, 1002400, &user_idnr);

	// Taken from dm_user.c, mkpassword:
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

		result = auth_validate(ci, userid, passwd, &user_idnr_check);
		fail_unless(result==1,"auth_validate failed, pwtype [%s]", pwtypes[i]);
		fail_unless(user_idnr_check == user_idnr, "User ID number mismatch from auth_validate.");
	}

	auth_delete_user(userid);
}
END_TEST
#endif

START_TEST(test_auth_cram_md5)
{
	Cram_T c;
	const char *challenge = "<1896.697170952@postoffice.reston.mci.net>";
	const char *response = "dGltIGI5MTNhNjAyYzdlZGE3YTQ5NWI0ZTZlNzMzNGQzODkw";
	const char *expect = "PDE4OTYuNjk3MTcwOTUyQHBvc3RvZmZpY2UucmVzdG9uLm1jaS5uZXQ+";
	const char *secret = "tanstaaftanstaaf";
	const char *ch;
        char *r;

	c = Cram_new();
	Cram_setChallenge(c, challenge);

	ch = Cram_getChallenge(c);
	r = g_base64_encode((guchar *)ch, strlen(ch));
	fail_unless(strcmp(r, expect)==0, "%s\n%s\n", r, expect);
	g_free(r);

	fail_unless(Cram_decode(c, response) == TRUE);
	fail_unless(Cram_verify(c, secret) == TRUE);

	Cram_free(&c);
}
END_TEST


Suite *dbmail_common_suite(void)
{
	Suite *s = suite_create("Dbmail Auth");
	TCase *tc_auth = tcase_create("Auth");
	
	suite_add_tcase(s, tc_auth);
	
	tcase_add_checked_fixture(tc_auth, setup, teardown);
	tcase_add_test(tc_auth, test_auth_validate);
	//tcase_add_test(tc_auth, test_auth_change_password);
	//tcase_add_test(tc_auth, test_auth_change_password_raw);
	tcase_add_test(tc_auth, test_auth_cram_md5);
	
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
	

