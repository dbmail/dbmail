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
	g_mime_init(0);
}

void teardown(void)
{
	db_disconnect();
	auth_disconnect();
	g_mime_shutdown();
}


START_TEST(test_g_strcasestr)
{
	char *s = "asdfqwer";
	fail_unless(g_strcasestr(s,"SD")!=NULL,"g_strcasestr failed 1");
	fail_unless(g_strcasestr(s,"er")!=NULL,"g_strcasestr failed 2");
	fail_unless(g_strcasestr(s,"As")!=NULL,"g_strcasestr failed 3");
}
END_TEST

START_TEST(test_mailbox_remove_namespace)
{

	char *simple, *username, *namespace;
	char *patterns[] = {
		"#Users/foo/mailbox", "#Users/foo/*", "#Users/foo*",
		"#Users/", "#Users//", "#Users///", "#Users/%", "#Users*", "#Users",
		"#Public/foo/mailbox", "#Public/foo/*", "#Public/foo*",
		"#Public/", "#Public//", "#Public///", "#Public/%", "#Public*", "#Public", NULL
		};

	char *expected[18][3] = {
		{ NAMESPACE_USER, "foo", "mailbox" },
		{ NAMESPACE_USER, "foo", "*" },
		{ NAMESPACE_USER, "foo", "*" },

		{ NAMESPACE_USER, NULL, NULL },
		{ NAMESPACE_USER, NULL, NULL },
		{ NAMESPACE_USER, NULL, NULL },
		{ NAMESPACE_USER, NULL, NULL },
		{ NAMESPACE_USER, NULL, NULL },
		{ NAMESPACE_USER, NULL, NULL },

		{ NAMESPACE_PUBLIC, PUBLIC_FOLDER_USER, "foo/mailbox" },
		{ NAMESPACE_PUBLIC, PUBLIC_FOLDER_USER, "foo/*" },
		{ NAMESPACE_PUBLIC, PUBLIC_FOLDER_USER, "foo*" },
		{ NAMESPACE_PUBLIC, PUBLIC_FOLDER_USER, "" },
		{ NAMESPACE_PUBLIC, PUBLIC_FOLDER_USER, "/" },
		{ NAMESPACE_PUBLIC, PUBLIC_FOLDER_USER, "//" },
		{ NAMESPACE_PUBLIC, PUBLIC_FOLDER_USER, "%" },
		{ NAMESPACE_PUBLIC, PUBLIC_FOLDER_USER, "*" },
		{ NAMESPACE_PUBLIC, PUBLIC_FOLDER_USER, "" }
		
		};
	int i;

	for (i = 0; patterns[i]; i++) {
		simple = mailbox_remove_namespace(patterns[i], &namespace, &username);
		fail_unless(
			((simple == NULL && expected[i][2] == NULL) || strcmp(simple, expected[i][2])==0) &&
			((username== NULL && expected[i][1] == NULL) || strcmp(username, expected[i][1])==0) &&
			((namespace == NULL && expected[i][0] == NULL) || strcmp(namespace, expected[i][0])==0),
			"\n  mailbox_remove_namespace failed on [%s]\n"
			"  Expected: namespace [%s] user [%s] simple [%s]\n"
			"  Received: namespace [%s] user [%s] simple [%s]",
			patterns[i], expected[i][0], expected[i][1], expected[i][2],
			namespace, username, simple);
	}

}
END_TEST

START_TEST(test_convert_8bit_field) 
{
	const char *val = "=?windows-1251?B?0+/w4OLr5e335fHq6Okg8/fl8iDiIPHu4vDl7OXt7e7pIOru7O/g7ejo?=";
	char *u8, *val2, *u82, *u83, *val3;

	u8 = g_mime_utils_header_decode_text((const unsigned char *)val);
	val2 = g_mime_utils_header_encode_text((const unsigned char *)u8);
	u82 = g_mime_utils_header_decode_text((const unsigned char *)val2);

	fail_unless(strcmp(u8,u82)==0,"decode/encode failed in test_convert_8bit_field");

	val3 = convert_8bit_db_to_mime(u8);
	u83 = g_mime_utils_header_decode_text((const unsigned char *)val3);

	fail_unless(strcmp(u8,u83)==0,"decode/encode failed in test_convert_8bit_field\n[%s]\n[%s]\n", u8, u83);
	g_free(u8);
	g_free(u82);
	g_free(u83);
	g_free(val2);
	g_free(val3);
}
END_TEST

Suite *dbmail_misc_suite(void)
{
	Suite *s = suite_create("Dbmail Misc");
	TCase *tc_misc = tcase_create("Misc");
	
	suite_add_tcase(s, tc_misc);
	
	tcase_add_checked_fixture(tc_misc, setup, teardown);
	tcase_add_test(tc_misc, test_g_strcasestr);
	tcase_add_test(tc_misc, test_mailbox_remove_namespace);
	tcase_add_test(tc_misc, test_convert_8bit_field);
	
	return s;
}

int main(void)
{
	int nf;
	Suite *s = dbmail_misc_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
