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

extern char configFile[PATH_MAX];
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
	config_get_file();
	config_read(configFile);
	configure_debug(NULL,255,0);
	GetDBParams();
	db_connect();
	auth_connect();
}

void teardown(void)
{
	db_disconnect();
	auth_disconnect();
}


START_TEST(test_g_strcasestr)
{
	char *s = "asdfqwer";
	fail_unless(g_strcasestr(s,"SD")!=NULL,"g_strcasestr failed 1");
	fail_unless(g_strcasestr(s,"er")!=NULL,"g_strcasestr failed 2");
	fail_unless(g_strcasestr(s,"As")!=NULL,"g_strcasestr failed 3");
}
END_TEST

char * g_strchomp_c(char *string, char c)
{
	size_t len;
	if (! string) return NULL;

	len = strlen(string);
	while(len--) {
		if (string[len] && string[len] == c)
			string[len] = '\0';
		else
			break;
	}
	return string;
}

START_TEST(test_g_strchomp_c)
{
	int i;
	const char *expect = "#Users";
	char *u, *t, *s[] = {
		"#Users",
		"#Users/",
		"#Users//",
		"#Users///",
		"#Users////",
		NULL
	};
	for (i=0; s[i]; i++) {
		u = g_strdup(s[i]);
		t = g_strchomp_c(u,'/');
		fail_unless(strcmp(t,expect)==0,"g_strchomp_c failed [%s] != [%s]", t, expect);
		g_free(u);
	}

}
END_TEST

START_TEST(test_mailbox_remove_namespace)
{

	char *t, *simple, *username, *namespace;
	char *patterns[] = {
		"#Users/foo/mailbox", 
		"#Users/foo/*", 
		"#Users/foo*",
		"#Users/", 
		"#Users//", 
		"#Users///", 
		"#Users/%", 
		"#Users*", 
		"#Users",

		"#Public/foo/mailbox", 
		"#Public/foo/*", 
		"#Public/foo*",
		"#Public/", 
		"#Public//", 
		"#Public///", 
		"#Public/%", 
		"#Public*", 
		"#Public", NULL
		};

	char *expected[18][3] = {
		{ NAMESPACE_USER, "foo", "mailbox" },
		{ NAMESPACE_USER, "foo", "*" },
		{ NAMESPACE_USER, "foo", "*" },
		{ NAMESPACE_USER, NULL, NULL },
		{ NAMESPACE_USER, NULL, NULL },
		{ NAMESPACE_USER, NULL, NULL },
		{ NAMESPACE_USER, NULL, "%" },
		{ NAMESPACE_USER, NULL, "*" },
		{ NAMESPACE_USER, NULL, NULL },

		{ NAMESPACE_PUBLIC, PUBLIC_FOLDER_USER, "foo/mailbox" },
		{ NAMESPACE_PUBLIC, PUBLIC_FOLDER_USER, "foo/*" },
		{ NAMESPACE_PUBLIC, PUBLIC_FOLDER_USER, "foo*" },
		{ NAMESPACE_PUBLIC, PUBLIC_FOLDER_USER, "" },
		{ NAMESPACE_PUBLIC, PUBLIC_FOLDER_USER, "" },
		{ NAMESPACE_PUBLIC, PUBLIC_FOLDER_USER, "" },
		{ NAMESPACE_PUBLIC, PUBLIC_FOLDER_USER, "%" },
		{ NAMESPACE_PUBLIC, PUBLIC_FOLDER_USER, "*" },
		{ NAMESPACE_PUBLIC, PUBLIC_FOLDER_USER, "" }
		
		};
	int i;

	for (i = 0; patterns[i]; i++) {
		t = g_strdup(patterns[i]);
		simple = mailbox_remove_namespace(t, &namespace, &username);
		fail_unless(((simple == NULL && expected[i][2] == NULL) || strcmp(simple, expected[i][2])==0),
			"\nmailbox_remove_namespace failed on [%s] [%s] != [%s]\n", patterns[i], simple, expected[i][2] );
		fail_unless( ((username== NULL && expected[i][1] == NULL) || strcmp(username, expected[i][1])==0),
			"\nmailbox_remove_namespace failed on [%s] [%s] != [%s]\n" , patterns[i], username, expected[i][1]);
		fail_unless( ((namespace == NULL && expected[i][0] == NULL) || strcmp(namespace, expected[i][0])==0),
			"\nmailbox_remove_namespace failed on [%s] [%s] != [%s]\n" , patterns[i], namespace, expected[i][0]);
		g_free(t);
		g_free(username);
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
	const char *u77 = "=?iso-8859-1?Q?ver_isto_=3D=3E_FW:_Envio_de_erro_da_aplica=E7=E3o_Decimal?=\n\t=?iso-8859-1?Q?Fire-Direc=E7=E3o?=";
	const char *exp1 = "Neue Lösungfür unsere Kunden";
	const char *exp2 = "Lösung";
	const char *exp3 = "für";
	const char *exp4 = "... Übergabe ...";
	const char *exp5 = "a";
	const char *exp6 = "ééé";
	const char *exp7 = "ver isto => FW: Envio de erro da aplicação DecimalFire-Direcção";

	char *u8, *val2, *u82, *u83, *val3;

	u8 = g_mime_utils_header_decode_text(NULL, val);
	val2 = g_mime_utils_header_encode_text(NULL, u8, NULL);
	u82 = g_mime_utils_header_decode_text(NULL, val2);

	fail_unless(strcmp(u8,u82)==0,"decode/encode failed in test_dbmail_iconv_str_to_db");

	val3 = dbmail_iconv_db_to_utf7(u8);
	u83 = g_mime_utils_header_decode_text(NULL, val3);

	fail_unless(strcmp(u8,u83)==0,"decode/encode failed in test_dbmail_iconv_str_to_db\n[%s]\n[%s]\n", u8, u83);
	g_free(u8);
	g_free(u82);
	g_free(u83);
	g_free(val2);
	g_free(val3);

	// 
	//
	u8 = dbmail_iconv_decode_text(u71);
	fail_unless(strcmp(u8,exp1)==0, "decode failed [%s] != [%s]", u8, exp1);
	g_free(u8);

	u8 = dbmail_iconv_decode_text(u72);
	fail_unless(strcmp(u8,exp2)==0,"decode failed [%s] != [%s]", u8, exp2);
	g_free(u8);

	u8 = dbmail_iconv_decode_text(u73);
	fail_unless(strcmp(u8,exp3)==0,"decode failed [%s] != [%s]", u8, exp3);
	g_free(u8);

	u8 = dbmail_iconv_decode_text(u74);
	fail_unless(strcmp(u8,exp4)==0, "decode failed [%s] != [%s]", u8, exp4);
	g_free(u8);

	u8 = dbmail_iconv_decode_text(u75);
	fail_unless(strcmp(u8,exp5)==0, "decode failed [%s] != [%s]", u8, exp5);
	g_free(u8);

	u8 = dbmail_iconv_decode_text(u76);
	fail_unless(strcmp(u8,exp6)==0, "decode failed [%s] != [%s]", u8, exp6);
	g_free(u8);

	u8 = dbmail_iconv_decode_text(u77);
	fail_unless(strcmp(u8,exp7)==0,"decode failed [%s] != [%s]", u8, exp7);
	g_free(u8);

}
END_TEST

START_TEST(test_dbmail_iconv_decode_address)
{
	char *u71 = "=?iso-8859-1?Q?::_=5B_Arrty_=5D_::_=5B_Roy_=28L=29_St=E8phanie_=5D?=  <over.there@hotmail.com>";
	//const char *ex1 = "\":: [ Arrty ] :: [ Roy (L) Stèphanie ]\" <over.there@hotmail.com>";
	char *u72 = "=?utf-8?Q?Jos=E9_M=2E_Mart=EDn?= <jmartin@onsager.ugr.es>"; // latin-1 masking as utf8
	const char *ex2 = "Jos? M. Mart?n <jmartin@onsager.ugr.es>";
	char *u73 = "=?utf-8?B?Ik9ubGluZSBSZXplcnZhxI1uw60gU3lzdMOpbSBTTU9TSyI=?= <e@mail>";
	char *ex3;

	char *u8;
	
	u8 = dbmail_iconv_decode_address(u71);
//	fail_unless(strcmp(u8,ex1)==0,"decode failed\n[%s] != \n[%s]\n", u8, ex1);
	g_free(u8);

	u8 = dbmail_iconv_decode_address(u72);
	fail_unless(strcmp(u8,ex2)==0,"decode failed\n[%s] != \n[%s]\n", u8, ex2);
	g_free(u8);

	u8 = dbmail_iconv_decode_address(u73);
	ex3 = g_mime_utils_header_decode_text(NULL, u73);
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
	fail_unless(strlen(a)==32, "create_unique_id produced incorrect string length [%s][%lu]", a, strlen(a));
	fail_unless(strlen(b)==32, "create_unique_id produced incorrect string length [%s][%lu]", b, strlen(b));
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
	g_free(s);

	b = g_list_append(b, g_strdup("A"));

	g_list_merge(&a, b, IMAPFA_ADD, (GCompareFunc)g_ascii_strcasecmp);
	s = dbmail_imap_plist_as_string(a);
	fail_unless(MATCH(s,"(A B C D)"), "g_list_merge ADD failed 2");
	g_free(s);

	g_list_merge(&a, b, IMAPFA_REMOVE, (GCompareFunc)g_ascii_strcasecmp);
	s = dbmail_imap_plist_as_string(a);
	fail_unless(MATCH(s,"(B C)"), "g_list_merge REMOVE failed");
	g_free(s);

	g_list_merge(&a, b, IMAPFA_REPLACE, (GCompareFunc)g_ascii_strcasecmp);
	s = dbmail_imap_plist_as_string(a);
	fail_unless(MATCH(s,"(D A)"), "g_list_merge REPLACE failed");
	g_free(s);
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

START_TEST(test_base64_decode)
{
	int i;
	uint64_t l;
	char *result;
	const char *in = "123456123456";
	gchar *out = g_base64_encode((const guchar *)in,16);
	for (i=0; i<100; i++) {
		result = dm_base64_decode(out, &l);
		fail_unless(strncmp(in, result, l)==0);
		g_free(result);
	}

	g_free(out);

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

	g_strfreev(result);
	g_free(out);
	
}
END_TEST

#define S1(a,b) \
	memset(hash,0,sizeof(hash)); dm_sha1((a),hash); \
	fail_unless(SMATCH(hash,(b)), "sha1 failed [%s] != [%s]", hash, b)
START_TEST(test_sha1)
{
	char hash[FIELDSIZE]; 
	// test vectors from https://www.cosic.esat.kuleuven.be/nessie/testvectors/hash/sha/index.html
	S1("", "da39a3ee5e6b4b0d3255bfef95601890afd80709");
	S1("a", "86f7e437faa5a7fce15d1ddcb9eaeaea377667b8");
	S1("abc", "a9993e364706816aba3e25717850c26c9cd0d89d");
}
END_TEST

#define S2(a,b) \
	memset(hash,0,sizeof(hash)); dm_sha256((a),hash); \
	fail_unless(SMATCH(hash,(b)), "sha256 failed [%s] != [%s]", hash, b)
START_TEST(test_sha256)
{
	char hash[FIELDSIZE]; 
	// test vectors from https://www.cosic.esat.kuleuven.be/nessie/testvectors/hash/sha/index.html
	S2("", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	S2("a", "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb");
	S2("abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}
END_TEST

#define S3(a,b) \
	memset(hash,0,sizeof(hash)); dm_sha512((a),hash); \
	fail_unless(SMATCH(hash,(b)), "sha512 failed [%s] != [%s]", hash, b)
START_TEST(test_sha512)
{
	char hash[FIELDSIZE]; 
	// test vectors from https://www.cosic.esat.kuleuven.be/nessie/testvectors/hash/sha/index.html
	S3("", "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e");
	S3("a", "1f40fc92da241694750979ee6cf582f2d5d7d28e18335de05abc54d0560e0f5302860c652bf08d560252aa5e74210546f369fbbbce8c12cfc7957b2652fe9a75");
	S3("abc", "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
}
END_TEST

#define M(a,b) \
	memset(hash,0,sizeof(hash)); dm_md5((a),hash); \
	fail_unless(SMATCH(hash,(b)), "md5 failed [%s] != [%s]", hash, b)
START_TEST(test_md5)
{
	char hash[FIELDSIZE]; 
	M("","d41d8cd98f00b204e9800998ecf8427e");
	M("a","0cc175b9c0f1b6a831c399e269772661");
	M("abc", "900150983cd24fb0d6963f7d28e17f72");
}
END_TEST

#define T(a,b) \
	memset(hash,0,sizeof(hash)); dm_tiger((a),hash); \
	fail_unless(SMATCH(hash,(b)), "tiger failed [%s] != [%s]", hash, b)
START_TEST(test_tiger)
{
	char hash[FIELDSIZE]; 
	T("","3293ac630c13f0245f92bbb1766e16167a4e58492dde73f3");
	T("a","77befbef2e7ef8ab2ec8f93bf587a7fc613e247f5f247809");
	T("abc","2aab1484e8c158f2bfb8c5ff41b57a525129131c957b5f93");
}
END_TEST

#define W(a,b) \
	memset(hash,0,sizeof(hash)); dm_whirlpool((a),hash); \
	fail_unless(SMATCH(hash,(b)), "whirlpool failed [%s] != [%s]", hash, b)
START_TEST(test_whirlpool)
{
	char hash[FIELDSIZE]; 
	// the whirlpool test vectors are taken from the reference implementation (iso-test-vectors.txt)
	W("","19fa61d75522a4669b44e39c1d2e1726c530232130d407f89afee0964997f7a73e83be698b288febcf88e3e03c4f0757ea8964e59b63d93708b138cc42a66eb3");
	W("a","8aca2602792aec6f11a67206531fb7d7f0dff59413145e6973c45001d0087b42d11bc645413aeff63a42391a39145a591a92200d560195e53b478584fdae231a");
	W("abc","4e2448a4c6f486bb16b6562c73b4020bf3043e3a731bce721ae1b303d97e6d4c7181eebdb6c57e277d0e34957114cbd6c797fc9d95d8b582d225292076d4eef5");
}
END_TEST

START_TEST(test_get_crlf_encoded_opt1)
{
	char *in[] = {
		"a\n",
		"a\nb\nc\n",
		"a\nb\r\nc\n",
		"a\nb\rc\n",
		"a\nb\r\r\nc\n",
		NULL
	};
	char *out[] = {
		"a\r\n",
		"a\r\nb\r\nc\r\n",
		"a\r\nb\r\nc\r\n",
		"a\r\nb\rc\r\n",
		"a\r\nb\r\r\nc\r\n",
		NULL
	};
	int i=0;
	while (in[i]) {
		char *r = get_crlf_encoded_opt(in[i],0);
		fail_unless(MATCH(r,out[i]), "get_crlf_encoded failed [%s]!=[%s]", r, out[i]);
		g_free(r);
		i++;
	}
}
END_TEST

START_TEST(test_get_crlf_encoded_opt2)
{
	char *in[] = {
		"a\nb\nc.\n",
		"a\n.b\r\nc.\n",
		"a\nb\r.c.\n",
		"a\nb\r\r\n.\nc\n",
		NULL
	};
	char *out[] = {
		"a\r\nb\r\nc.\r\n",
		"a\r\n..b\r\nc.\r\n",
		"a\r\nb\r.c.\r\n",
		"a\r\nb\r\r\n..\r\nc\r\n",
		NULL
	};
	int i=0;
	while (in[i]) {
		char *r = get_crlf_encoded_opt(in[i],1);
		fail_unless(MATCH(r,out[i]), "get_crlf_encoded failed [%s]!=[%s]", r, out[i]);
		g_free(r);
		i++;
	}
}
END_TEST

START_TEST(test_date_imap2sql)
{
	char r[SQL_INTERNALDATE_LEN];
	int i = 0;
	char *in[] = {
		"01-Jan-2001",
		"1-Jan-2001",
		"blah",
		NULL
	};
	char *out[] = {
		"2001-01-01 00:00:00",
		"2001-01-01 00:00:00",
		"",
		NULL
	};

	while (in[i]) {
		memset(r, 0, sizeof(r));
		date_imap2sql(in[i], r);
		fail_unless(SMATCH(out[i], r), "[%s] != [%s]", r, out[i]);
		i++;
	}

}
END_TEST

START_TEST(test_date_sql2imap)
{
	char *r;
	int i = 0;
	char *in[] = {
		"2001-02-03 04:05:06",
		"blah",
		NULL
	};
	char *out[] = {
		"03-Feb-2001 04:05:06 +0000",
		"03-Nov-1979 00:00:00 +0000",
		NULL
	};

	while (in[i]) {
		r = date_sql2imap(in[i]);
		fail_unless(MATCH(out[i], r), "[%s] != [%s]", r, out[i]);
		g_free(r);
		i++;
	}

}
END_TEST


Suite *dbmail_misc_suite(void)
{
	Suite *s = suite_create("Dbmail Misc");
	TCase *tc_misc = tcase_create("Misc");
	
	suite_add_tcase(s, tc_misc);
	
	tcase_add_checked_fixture(tc_misc, setup, teardown);
	tcase_add_test(tc_misc, test_g_strcasestr);
	tcase_add_test(tc_misc, test_g_strchomp_c);
	tcase_add_test(tc_misc, test_mailbox_remove_namespace);
	tcase_add_test(tc_misc, test_dbmail_iconv_str_to_db);
	tcase_add_test(tc_misc, test_dbmail_iconv_decode_address);
	tcase_add_test(tc_misc, test_create_unique_id);
	tcase_add_test(tc_misc, test_g_list_merge);
 	tcase_add_test(tc_misc, test_dm_strtoull);
	tcase_add_test(tc_misc, test_base64_decode);
	tcase_add_test(tc_misc, test_base64_decodev);
	tcase_add_test(tc_misc, test_sha1);
	tcase_add_test(tc_misc, test_sha256);
	tcase_add_test(tc_misc, test_sha512);
	tcase_add_test(tc_misc, test_whirlpool);
	tcase_add_test(tc_misc, test_md5);
	tcase_add_test(tc_misc, test_tiger);
	tcase_add_test(tc_misc, test_get_crlf_encoded_opt1);
	tcase_add_test(tc_misc, test_get_crlf_encoded_opt2);
	tcase_add_test(tc_misc, test_date_imap2sql);
	tcase_add_test(tc_misc, test_date_sql2imap);

	return s;
}

int main(void)
{
	int nf;
	g_mime_init();
	Suite *s = dbmail_misc_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	g_mime_shutdown();
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
