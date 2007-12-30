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
		simple = (char *)mailbox_remove_namespace(patterns[i], &namespace, &username);
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


START_TEST(test_dbmail_iconv_str_to_db) 
{
	const char *val = "=?windows-1251?B?0+/w4OLr5e335fHq6Okg8/fl8iDiIPHu4vDl7OXt7e7pIOru7O/g7ejo?=";
	const char *u71 = "Neue =?ISO-8859-1?Q?L=F6sung?= =?ISO-8859-1?Q?f=FCr?= unsere Kunden";
	const char *u72 = "=?ISO-8859-1?Q?L=F6sung?=";
	const char *u73 = "=?ISO-8859-1?Q?f=FCr?=";
	const char *u74 = "... =?ISO-8859-1?Q?=DCbergabe?= ...";
	const char *u75 = "=?iso-8859-1?q?a?=";
	const char *u76 = "=?utf-8?b?w6nDqcOp?=";
	const char *exp = "Neue Lösung für unsere Kunden";
	const char *exp2 = "Lösung";
	const char *exp3 = "für";

	char *u8, *val2, *u82, *u83, *val3;

	u8 = g_mime_utils_header_decode_text(val);
	val2 = g_mime_utils_header_encode_text(u8);
	u82 = g_mime_utils_header_decode_text(val2);

	fail_unless(strcmp(u8,u82)==0,"decode/encode failed in test_dbmail_iconv_str_to_db");

	val3 = dbmail_iconv_db_to_utf7(u8);
	u83 = g_mime_utils_header_decode_text(val3);

	fail_unless(strcmp(u8,u83)==0,"decode/encode failed in test_dbmail_iconv_str_to_db\n[%s]\n[%s]\n", u8, u83);
	g_free(u8);
	g_free(u82);
	g_free(u83);
	g_free(val2);
	g_free(val3);

	// 
	//
	u8 = dbmail_iconv_decode_text(u71);
	fail_unless(strcmp(u8,exp)==0, "decode failed [%s] != [%s]", u8, exp);
	u82 = dbmail_iconv_decode_text(u8);
	fail_unless(strcmp(u82,exp)==0, "decode failed [%s] != [%s]", u82, exp);
	g_free(u8);
	g_free(u82);

	u8 = dbmail_iconv_decode_text(u72);
	fail_unless(strcmp(u8,exp2)==0,"decode failed [%s] != [%s]", u8, exp2);
	g_free(u8);

	u8 = dbmail_iconv_decode_text(u73);
	fail_unless(strcmp(u8,exp3)==0,"decode failed [%s] != [%s]", u8, exp3);
	g_free(u8);

	u8 = g_mime_utils_header_decode_text(u74);
	u82 = dbmail_iconv_decode_text(u74);
	fail_unless(strcmp(u8,u82)==0, "decode failed [%s] != [%s]", u8, u82);
	g_free(u8);
	g_free(u82);

	u8 = g_mime_utils_header_decode_text(u75);
	u82 = dbmail_iconv_decode_text(u75);
	fail_unless(strcmp(u8,u82)==0, "decode failed [%s] != [%s]", u8, u82);
	g_free(u8);
	g_free(u82);

	u8 = g_mime_utils_header_decode_text(u76);
	u82 = dbmail_iconv_decode_text(u76);
	fail_unless(strcmp(u8,u82)==0, "decode failed [%s] != [%s]", u8, u82);
	g_free(u8);
	g_free(u82);



}
END_TEST

START_TEST(test_dbmail_iconv_decode_address)
{
	const char *u71 = "=?iso-8859-1?Q?::_=5B_Arrty_=5D_::_=5B_Roy_=28L=29_St=E8phanie_=5D?=  <over.there@hotmail.com>";
	const char *ex1 = "\":: [ Arrty ] :: [ Roy (L) Stèphanie ]\" <over.there@hotmail.com>";
	const char *u72 = "=?utf-8?Q?Jos=E9_M=2E_Mart=EDn?= <jmartin@onsager.ugr.es>"; // latin-1 masking as utf8
	const char *ex2 = "\"Jos? M. Mart?n\" <jmartin@onsager.ugr.es>";
	const char *u73 = "=?utf-8?B?Ik9ubGluZSBSZXplcnZhxI1uw60gU3lzdMOpbSBTTU9TSyI=?= <e@mail>";
	char *ex3;

	char *u8;
	
	u8 = dbmail_iconv_decode_address(u71);
	fail_unless(strcmp(u8,ex1)==0,"decode failed\n[%s] != \n[%s]\n", u8, ex1);
	g_free(u8);

	u8 = dbmail_iconv_decode_address(u72);
	fail_unless(strcmp(u8,ex2)==0,"decode failed\n[%s] != \n[%s]\n", u8, ex2);
	g_free(u8);

	u8 = dbmail_iconv_decode_address(u73);
	ex3 = g_mime_utils_header_decode_text(u73);
	fail_unless(strcmp(u8,ex3)==0,"decode failed\n[%s] != \n[%s]\n", u8, ex3);
	g_free(u8);
	g_free(ex3);

}
END_TEST

START_TEST(test_create_unique_id)
{
	char *a = g_new0(char, 64);
	char *b = g_new0(char, 64);
	create_unique_id(a,0);
	create_unique_id(b,0);
	fail_unless(strlen(a)==32, "create_unique_id produced incorrect string length [%s][%d]", a, strlen(a));
	fail_unless(strlen(b)==32, "create_unique_id produced incorrect string length [%s][%d]", b, strlen(b));
	fail_unless(!MATCH(a,b),"create_unique_id shouldn't produce identical output");

	g_free(a);
	g_free(b);
}
END_TEST

START_TEST(test_g_list_merge)
{
	char *s;

	GList *a = NULL, *b = NULL;
	a = g_list_append(a, g_strdup("A"));
	a = g_list_append(a, g_strdup("B"));
	a = g_list_append(a, g_strdup("C"));
	
	b = g_list_append(b, g_strdup("D"));

	g_list_merge(&a, b, IMAPFA_ADD, (GCompareFunc)g_ascii_strcasecmp);
	s = dbmail_imap_plist_as_string(a);
	fail_unless(MATCH(s,"(A B C D)"), "g_list_merge ADD failed 1");

	b = g_list_append(b, g_strdup("A"));

	g_list_merge(&a, b, IMAPFA_ADD, (GCompareFunc)g_ascii_strcasecmp);
	s = dbmail_imap_plist_as_string(a);
	fail_unless(MATCH(s,"(A B C D)"), "g_list_merge ADD failed 2");

	g_list_merge(&a, b, IMAPFA_REMOVE, (GCompareFunc)g_ascii_strcasecmp);
	s = dbmail_imap_plist_as_string(a);
	fail_unless(MATCH(s,"(B C)"), "g_list_merge REMOVE failed");

	g_list_merge(&a, b, IMAPFA_REPLACE, (GCompareFunc)g_ascii_strcasecmp);
	s = dbmail_imap_plist_as_string(a);
	fail_unless(MATCH(s,"(D A)"), "g_list_merge REPLACE failed");
}
END_TEST

START_TEST(test_dm_strtoull)
{
	fail_unless(dm_strtoull("10",NULL,10)==10);
	fail_unless(dm_strtoull(" 10",NULL,10)==10);
	fail_unless(dm_strtoull("-10",NULL,10)==0);
	fail_unless(dm_strtoull("10",NULL,16)==16);
	fail_unless(dm_strtoull("10",NULL,2)==2);
}
END_TEST

START_TEST(test_dm_strbinesc)
{
	char *from = "test\0d";
	char *to = dm_strbinesc(from);

	//printf("[%s]", to);
	g_free(to);
}
END_TEST

START_TEST(test_base64_decodev)
{
	int i;
	char **result;
	const char *in = "proxy\0user\0pass";
	gchar *out = g_base64_encode((const guchar *)in,16);

	result = base64_decodev(out);
	for (i=0; result[i] != NULL; i++)
		;

	fail_unless(i==4,"base64_decodev failed");
	fail_unless(MATCH(result[0],"proxy"), "base64_decodev failed");
	fail_unless(MATCH(result[1],"user"), "base64_decodev failed");
	fail_unless(MATCH(result[2],"pass"), "base64_decodev failed");
	
}
END_TEST

#define S(a,b) fail_unless(MATCH(dm_sha1((a)),(b)), "sha1 failed [%s] != [%s]")
START_TEST(test_sha1)
{
	// test vectors from https://www.cosic.esat.kuleuven.be/nessie/testvectors/hash/sha/index.html
	S("", "da39a3ee5e6b4b0d3255bfef95601890afd80709");
	S("a", "86f7e437faa5a7fce15d1ddcb9eaeaea377667b8");
	S("abc", "a9993e364706816aba3e25717850c26c9cd0d89d");
}
END_TEST

#define M(a,b) fail_unless(MATCH(dm_md5((a)),(b)), "md5 failed [%s] != [%s]")
START_TEST(test_md5)
{
	M("","D41D8CD98F00B204E9800998ECF8427E");
	M("a","0CC175B9C0F1B6A831C399E269772661");
	M("abc", "900150983CD24FB0D6963F7D28E17F72");
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
	tcase_add_test(tc_misc, test_dbmail_iconv_str_to_db);
	tcase_add_test(tc_misc, test_dbmail_iconv_decode_address);
	tcase_add_test(tc_misc, test_create_unique_id);
	tcase_add_test(tc_misc, test_g_list_merge);
 	tcase_add_test(tc_misc, test_dm_strtoull);
	tcase_add_test(tc_misc, test_dm_strbinesc);
	tcase_add_test(tc_misc, test_base64_decodev);
	tcase_add_test(tc_misc, test_sha1);
	tcase_add_test(tc_misc, test_md5);
	
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
