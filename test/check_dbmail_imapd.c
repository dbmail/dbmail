/*
 * vim: set fileencodings=utf-8
 *
 *   Copyright (c) 2004-2013 NFG Net Facilities Group BV support@nfg.nl
 *   Copyright (c) 2014-2019 Paul J Stevens, The Netherlands, support@nfg.nl
 *   Copyright (c) 2020-2025 Alan Hicks, Persistent Objects Ltd support@p-o.co.uk
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
extern DBParam_T db_params;
extern Mempool_T queue_pool;

#define DBPFX db_params.pfx

/* we need this one because we can't directly link imapd.o */
extern uint64_t msgbuf_idx;
extern uint64_t msgbuf_buflen;

extern char *multipart_message;
extern char *multipart_message_part;
extern char *raw_lmtp_data;

/*
 *
 * the test fixtures
 *
 */
	
void init_testuser1(void) 
{
        uint64_t user_idnr;
	if (! (auth_user_exists("testuser1",&user_idnr)))
		auth_adduser("testuser1","test", "md5", 101, 1024000, &user_idnr);
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
	queue_pool = mempool_open();
}

void teardown(void)
{
	auth_disconnect();
	db_disconnect();
	config_free();
	mempool_close(&queue_pool);
}

START_TEST(test_dbmail_imap_plist_as_string)
{
	char *result;
	GList *l;

	l = NULL;
	l = g_list_append(l, "NIL");
	l = g_list_append(l, "NIL");
	result = dbmail_imap_plist_as_string(l);
	ck_assert_str_eq(result,"(NIL NIL)");
	
	//g_list_foreach(l,(GFunc)g_free,NULL);
	g_free(result);
	
	l = NULL;
	l = g_list_append(l, "(NIL NIL)");
	result = dbmail_imap_plist_as_string(l);
	ck_assert_str_eq(result,"(NIL NIL)");
	
	//g_list_foreach(l,(GFunc)g_free,NULL);
	g_free(result);

	l = g_list_append(NULL, "NIL");
	l = g_list_append(l, "NIL");
	l = g_list_append(l, "(NIL NIL)");
	l = g_list_append(l, "(NIL NIL)");
	result = dbmail_imap_plist_as_string(l);
	ck_assert_str_eq(result,"(NIL NIL (NIL NIL) (NIL NIL))");
	
	//g_list_foreach(l,(GFunc)g_free,NULL);
	g_free(result);
}
END_TEST

START_TEST(test_dbmail_imap_plist_collapse)
{
	char *result;
	char *in = "(NIL) (NIL) (NIL)";
	result = dbmail_imap_plist_collapse(in);
	ck_assert_str_eq(result,"(NIL)(NIL)(NIL)");
	g_free(result);
}
END_TEST


#define A(x,y) s=dbmail_imap_astring_as_string(x); \
	ck_assert_str_eq(y,s); \
	g_free(s)

START_TEST(test_dbmail_imap_astring_as_string)
{
	char *s;
	A("test","\"test\"");
	A("\"test\"","\"test\"");
	A("\"test\" \"test\"","{13}\r\n\"test\" \"test\"");
	A("\"test","{5}\r\n\"test");
	A("testÃ","{6}\r\ntestÃ");
	A("test\"","{5}\r\ntest\"");
	A("test\"","{5}\r\ntest\"");
	A("test\\","{5}\r\ntest\\");
	A("test\225","{5}\r\ntest\225");
}
END_TEST

#ifdef OLD
static ClientBase_T * ci_new(void)
{
	ClientBase_T *ci = g_new0(ClientBase_T,1);
	FILE *fd = fopen("/dev/null","w");
	ci->rx = fileno(stdin);
	ci->tx = fileno(fd);
	return ci;
}

static char *tempfile;
static ClientBase_T * ci_new_writable(void)
{
	ClientBase_T *ci = ci_new();

	tempfile = tmpnam(NULL);
	mkfifo(tempfile, 0600);

	// Open r+ because we're controlling both sides.
	ci->rx = fileno(fopen(tempfile, "r+"));
	ci->tx = fileno(fopen(tempfile, "r+"));
	return ci;
}

static void ci_free_writable(ClientBase_T *ci)
{
	if (ci->tx > 0) close(ci->tx);
	if (ci->rx >= 0) close(ci->rx);
	unlink(tempfile);
}
#endif

//ImapSession * dbmail_imap_session_new(void);
START_TEST(test_imap_session_new)
{
	ImapSession *s;
	Mempool_T pool = mempool_open();
	s = dbmail_imap_session_new(pool);
	ck_assert_ptr_nonnull(s);
	dbmail_imap_session_delete(&s);
}
END_TEST

START_TEST(test_imap_get_structure_bare_bones)
{
	DbmailMessage *message;
	char *result;
	char expect[4096];

	memset(&expect, 0, sizeof(expect));

	/* bare bones */
	message = dbmail_message_new(NULL);
	message = dbmail_message_init_with_string(message, simple);
	result = imap_get_structure(GMIME_MESSAGE(message->content), 1);
	dbmail_message_free(message);
	g_free(result);

}
END_TEST
START_TEST(test_imap_get_structure_text_plain)
{
	DbmailMessage *message;
	char *body;
	char *result;
	char expect[4096];

	memset(&expect, 0, sizeof(expect));

	/* text/plain */
	message = dbmail_message_new(NULL);
	message = dbmail_message_init_with_string(message, rfc822);
	result = imap_get_structure(GMIME_MESSAGE(message->content), 1);
	g_strlcpy(expect,"(\"text\" \"plain\" (\"charset\" \"us-ascii\") NIL NIL \"7bit\" 32 4 NIL NIL NIL NIL)",1024);
	body = g_mime_object_get_body(message->content);
	ck_assert_str_eq(result, expect);
	g_free(body);
	g_free(result);
	dbmail_message_free(message);

}
END_TEST
START_TEST(test_imap_get_structure_multipart)
{
	DbmailMessage *message;
	char *result;
	char expect[4096];

	memset(&expect, 0, sizeof(expect));

	/* multipart */
	message = dbmail_message_new(NULL);
	message = dbmail_message_init_with_string(message, multipart_message);
	result = imap_get_structure(GMIME_MESSAGE(message->content), 1);
	g_strlcpy(expect,"((\"text\" \"html\" NIL NIL NIL \"7BIT\" 30 3 NIL (\"inline\" NIL) NIL NIL)"
			"(\"text\" \"plain\" (\"charset\" \"us-ascii\" \"name\" \"testfile\") NIL NIL \"base64\" 432 7 NIL NIL NIL NIL)"
			" \"multipart\" \"mixed\" (\"boundary\" \"boundary\") NIL NIL NIL NIL)",1024);
	ck_assert_str_eq(result, expect);
	g_free(result);
	dbmail_message_free(message);

}
END_TEST
START_TEST(test_imap_get_structure_multipart_alternative)
{
	DbmailMessage *message;
	char *result;
	char expect[4096];

	memset(&expect, 0, sizeof(expect));

	/* multipart alternative */
	message = dbmail_message_new(NULL);
	message = dbmail_message_init_with_string(message, multipart_alternative);
	result = imap_get_structure(GMIME_MESSAGE(message->content), 1);
	g_strlcpy(expect,"(((\"text\" \"plain\" (\"charset\" \"ISO-8859-1\") NIL NIL \"7bit\" 281 10 NIL NIL NIL NIL)(\"text\" \"html\" (\"charset\" \"ISO-8859-1\") NIL NIL \"7bit\" 759 17 NIL NIL NIL NIL) \"multipart\" \"alternative\" (\"boundary\" \"------------040302030903000400040101\") NIL NIL NIL NIL)(\"image\" \"jpeg\" (\"name\" \"jesse_2.jpg\") NIL NIL \"base64\" 262 NIL (\"inline\" (\"filename\" \"jesse_2.jpg\")) NIL NIL) \"multipart\" \"mixed\" (\"boundary\" \"------------050000030206040804030909\") NIL NIL NIL NIL)",1024);

	ck_assert_str_eq(result, expect);
	g_free(result);
	dbmail_message_free(message);
}
END_TEST
START_TEST(test_imap_get_structure_multipart_apple)
{
	DbmailMessage *message;
	char *result;
	char expect[4096];

	memset(&expect, 0, sizeof(expect));

	/* multipart apple */
	message = dbmail_message_new(NULL);
	message = dbmail_message_init_with_string(message, multipart_apple);
	result = imap_get_structure(GMIME_MESSAGE(message->content), 1);
	g_strlcpy(expect, "((\"text\" \"plain\" (\"charset\" \"windows-1252\") NIL NIL \"quoted-printable\" 6 2 NIL NIL NIL NIL)((\"text\" \"html\" (\"charset\" \"us-ascii\") NIL NIL \"7bit\" 39 1 NIL NIL NIL NIL)(\"application\" \"vnd.openxmlformats-officedocument.wordprocessingml.document\" (\"name\" \"„Tradition hat Potenzial\".docx\") NIL NIL \"base64\" 256 NIL (\"attachment\" (\"filename\" \"„Tradition hat Potenzial\".docx\")) NIL NIL)(\"text\" \"html\" (\"charset\" \"windows-1252\") NIL NIL \"quoted-printable\" 147 4 NIL NIL NIL NIL) \"multipart\" \"mixed\" (\"boundary\" \"Apple-Mail=_3A2FC16D-D077-44C8-A239-A7B36A86540F\") NIL NIL NIL NIL) \"multipart\" \"alternative\" (\"boundary\" \"Apple-Mail=_E6A72268-1DAC-4E40-8270-C4CBE68157E0\") NIL NIL NIL NIL)", 4096);
	ck_assert_str_eq(result, expect);
	g_free(result);
	dbmail_message_free(message);
}
END_TEST
START_TEST(test_imap_get_structure_rfc2231)
{
	DbmailMessage *message;
	char *result;
	char expect[4096];

	memset(&expect, 0, sizeof(expect));

	/* rfc2231 encoded content-disposition */
	message = dbmail_message_new(NULL);
	message = dbmail_message_init_with_string(message, multipart_message7);
	result = imap_get_structure(GMIME_MESSAGE(message->content), 1);
	g_strlcpy(expect, "((\"text\" \"plain\" (\"charset\" \"UTF-8\") NIL NIL \"7bit\" 9 2 NIL NIL NIL NIL)(\"image\" \"png\" (\"name\" \"phönny.png\") NIL NIL \"base64\" 225 NIL (\"attachment\" (\"filename\" \"phönny.png\")) NIL NIL) \"multipart\" \"mixed\" (\"boundary\" \"------------000706040608020005040505\") NIL NIL NIL NIL)", 1024);

	ck_assert_str_eq(result, expect);
	g_free(result);
	dbmail_message_free(message);
}
END_TEST
START_TEST(test_imap_get_structure_multipart_signed)
{
	DbmailMessage *message;
	char *result;
	char expect[4096];

	memset(&expect, 0, sizeof(expect));

	message = dbmail_message_new(NULL);
	message = dbmail_message_init_with_string(message, multipart_signed);
	result = imap_get_structure(GMIME_MESSAGE(message->content), 1);
	g_strlcpy(expect, "(((\"text\" \"plain\" (\"charset\" \"UTF-8\") NIL NIL \"quoted-printable\" 12 1 NIL NIL NIL NIL)(\"image\" \"gif\" (\"name\" \"image.gif\") NIL NIL \"base64\" 142 NIL (\"attachment\" (\"filename\" \"image.gif\")) NIL NIL)(\"message\" \"rfc822\" NIL NIL NIL \"7bit\" 610 (\"Mon, 19 Aug 2013 14:54:05 +0200\" \"msg1\" ((NIL NIL \"d\" \"b\"))((NIL NIL \"d\" \"b\"))((NIL NIL \"e\" \"b\"))((NIL NIL \"a\" \"b\")) NIL NIL NIL NIL)((\"text\" \"plain\" (\"charset\" \"ISO-8859-1\") NIL NIL \"quoted-printable\" 12 1 NIL NIL NIL NIL)(\"text\" \"html\" (\"charset\" \"ISO-8859-1\") NIL NIL \"quoted-printable\" 11 2 NIL NIL NIL NIL) \"multipart\" \"alternative\" (\"boundary\" \"b1_7ad0d7cccab59d27194f9ad69c14606001f05f531376916845\") NIL NIL NIL NIL) 26 NIL (\"attachment\" (\"filename\" \"msg1.eml\")) NIL NIL)(\"message\" \"rfc822\" (\"name\" \"msg2.eml\") NIL NIL \"7bit\" 608 (\"Mon, 19 Aug 2013 14:54:05 +0200\" \"msg2\" ((NIL NIL \"d\" \"b\"))((NIL NIL \"d\" \"b\"))((NIL NIL \"e\" \"b\"))((NIL NIL \"a\" \"b\")) NIL NIL NIL NIL)((\"text\" \"plain\" (\"charset\" \"ISO-8859-1\") NIL NIL \"quoted-printable\" 12 1 NIL NIL NIL NIL)(\"text\" \"html\" (\"charset\" \"ISO-8859-1\") NIL NIL \"quoted-printable\" 11 2 NIL NIL NIL NIL) \"multipart\" \"alternative\" (\"boundary\" \"b1_7ad0d7cccab59d27194f9ad69c14606001f05f531376916845\") NIL NIL NIL NIL) 25 NIL (\"attachment\" (\"filename\" \"msg2.eml\")) NIL NIL)(\"application\" \"x-php\" (\"name\" \"script.php\") NIL NIL \"base64\" 122 NIL (\"attachment\" (\"filename\" \"script.php\")) NIL NIL) \"multipart\" \"mixed\" (\"boundary\" \"------------090808030504030005030705\") NIL NIL NIL NIL)(\"application\" \"pgp-signature\" (\"name\" \"signature.asc\") NIL \"OpenPGP digital signature\" \"7BIT\" 271 NIL (\"attachment\" (\"filename\" \"signature.asc\")) NIL NIL) \"multipart\" \"signed\" (\"micalg\" \"pgp-sha1\" \"protocol\" \"application/pgp-signature\" \"boundary\" \"DQGSJUrIXg9lgq2GBFumjRDhuJtiugxAX\") NIL NIL NIL NIL)", 4096);

	ck_assert_str_eq(result, expect);
	g_free(result);
	dbmail_message_free(message);
	/* done */
}
END_TEST

START_TEST(test_internet_address_list_parse_string)
{
	char * trythese [][2] = { 
		{ "undisclosed-recipients", "((NIL NIL \"undisclosed-recipients\" NIL))"},
		{ "undisclosed-recipients;", "((NIL NIL \"undisclosed-recipients\" NIL))"},
		{ "undisclosed-recipients:", "((NIL NIL \"undisclosed-recipients\" NIL)(NIL NIL NIL NIL))"}, 
		{ "undisclosed-recipients:;", "((NIL NIL \"undisclosed-recipients\" NIL)(NIL NIL NIL NIL))"},
		{ "undisclosed-recipients: ;", "((NIL NIL \"undisclosed-recipients\" NIL)(NIL NIL NIL NIL))"},
		{ NULL, NULL }
		};
	int i;

	for (i = 0; trythese[i][0] != NULL; i++) {

		char *input = trythese[i][0];
		char *expect = trythese[i][1];
		char *t, *result;

		InternetAddressList *alist;
		GList *list = NULL;

		t = imap_cleanup_address(input);
		alist = internet_address_list_parse(NULL, t);
		g_free(t);
		list = dbmail_imap_append_alist_as_plist(list, alist);
		g_object_unref(alist);
		alist = NULL;

		result = dbmail_imap_plist_as_string(list);

		ck_assert_str_eq(result, expect);

		g_list_destroy(list);
		g_free(result);

	}

	char * testlist[][2] = {
		{ "<i_am_not@broken.org>", "((NIL NIL \"i_am_not\" \"broken.org\"))" },
		{ "Break me: <foo@bar.org>", "((NIL NIL \"Break me\" NIL)(NIL NIL \"foo\" \"bar.org\")(NIL NIL NIL NIL))" },
		{ "Joe's Friends: mary@joe.com, joe@joe.com, jane@joe.com;",
			"((NIL NIL \"Joe's Friends\" NIL)(NIL NIL \"mary\" \"joe.com\")"
			"(NIL NIL \"joe\" \"joe.com\")(NIL NIL \"jane\" \"joe.com\")(NIL NIL NIL NIL))" },
		// These have the wrong separator; ms lookout style.
		{ "one@my.dom;two@my.dom", "((NIL NIL \"one\" \"my.dom\")(NIL NIL \"two\" \"my.dom\"))" },
		{ "one@my.dom; two@my.dom", "((NIL NIL \"one\" \"my.dom\")(NIL NIL \"two\" \"my.dom\"))" },
		{ "Group: one@my.dom;, two@my.dom", "((NIL NIL \"Group\" NIL)(NIL NIL \"one\" \"my.dom\")(NIL NIL NIL NIL)(NIL NIL \"two\" \"my.dom\"))" },
		{ NULL, NULL }
	};

	char *input, *expect;

	for (i = 0; testlist[i][0] != NULL; i++) {
		input = testlist[i][0];
		expect = testlist[i][1];

		InternetAddressList *alist;
		GList *list = NULL;
		char *result;
		char *t;
		t = imap_cleanup_address(input);
		alist = internet_address_list_parse(NULL, t);
		list = dbmail_imap_append_alist_as_plist(list, alist);
		result = dbmail_imap_plist_as_string(list);
		ck_assert_str_eq(result, expect);

		g_object_unref(alist);
		alist = NULL;
		g_list_destroy(list);
		g_free(result);
		g_free(t);
	}
}
END_TEST

START_TEST(test_imap_get_envelope)
{
	DbmailMessage *message;
	char *result, *expect;

	expect = g_new0(char, 1024);

	/* text/plain */
	message = dbmail_message_new(NULL);
	message = dbmail_message_init_with_string(message, rfc822);
	result = imap_get_envelope(GMIME_MESSAGE(message->content));
	g_strlcpy(expect,"(NIL \"dbmail test message\" ((NIL NIL \"somewher\" \"foo.org\")) ((NIL NIL \"somewher\" \"foo.org\")) ((NIL NIL \"somewher\" \"foo.org\")) ((NIL NIL \"testuser\" \"foo.org\")) NIL NIL NIL NIL)",1024);

	ck_assert_str_eq(result, expect);

	dbmail_message_free(message);
	g_free(result);
	result = NULL;

	//
	g_free(expect);
	expect = NULL;
}
END_TEST

START_TEST(test_imap_get_envelope_baremessage)
{
	DbmailMessage *message;
	char *result, *expect;

	expect = g_new0(char, 1024);

	/* bare bones message */
	message = dbmail_message_new(NULL);
	message = dbmail_message_init_with_string(message, simple);
	result = imap_get_envelope(GMIME_MESSAGE(message->content));

	g_strlcpy(expect,"(NIL \"dbmail test message\" NIL NIL NIL NIL NIL NIL NIL NIL)", 1024);

	ck_assert_str_eq(result, expect);

	dbmail_message_free(message);
	g_free(result);
	result = NULL;

	//
	g_free(expect);
	expect = NULL;
}
END_TEST

START_TEST(test_imap_get_envelope_groupaddresses)
{
	DbmailMessage *message;
	char *result, *expect;

	expect = g_new0(char, 1024);

	/* text/plain */
	message = dbmail_message_new(NULL);
	message = dbmail_message_init_with_string(message, rfc822);
	result = imap_get_envelope(GMIME_MESSAGE(message->content));
	g_strlcpy(expect,"(NIL \"dbmail test message\" ((NIL NIL \"somewher\" \"foo.org\")) ((NIL NIL \"somewher\" \"foo.org\")) ((NIL NIL \"somewher\" \"foo.org\")) ((NIL NIL \"testuser\" \"foo.org\")) NIL NIL NIL NIL)",1024);

	ck_assert_str_eq(result, expect);

	dbmail_message_free(message);
	g_free(result);
	result = NULL;

	/* bare bones message with group addresses*/
	message = dbmail_message_new(NULL);
	message = dbmail_message_init_with_string(message, simple_groups);
	result = imap_get_envelope(GMIME_MESSAGE(message->content));

	g_strlcpy(expect,"(\"Thu, 15 Feb 2007 01:02:03 +0200\" NIL ((\"Real Name\" NIL \"user\" \"domain\")) ((\"Real Name\" NIL \"user\" \"domain\")) ((\"Real Name\" NIL \"user\" \"domain\")) ((NIL NIL \"group\" NIL)(NIL NIL \"g1\" \"d1.org\")(NIL NIL \"g2\" \"d2.org\")(NIL NIL NIL NIL)(NIL NIL \"group2\" NIL)(NIL NIL \"g3\" \"d3.org\")(NIL NIL NIL NIL)) NIL NIL NIL NIL)", 1024);

	ck_assert_str_eq(result, expect);

	dbmail_message_free(message);
	g_free(result);
	result = NULL;

	//
	g_free(expect);
	expect = NULL;
}
END_TEST

START_TEST(test_imap_get_envelope_brokenfrom)
{
	DbmailMessage *message;
	char *result, *expect;

	expect = g_new0(char, 1024);

	/* bare message with broken From address*/
	message = dbmail_message_new(NULL);
	message = dbmail_message_init_with_string(message, broken_message3);
	result = imap_get_envelope(GMIME_MESSAGE(message->content));

	g_strlcpy(expect,"(\"Fri, 11 Sep 2009 17:42:32 +0100\" \"Re: Anexo II para RO\" NIL NIL NIL ((\"Foo Bar\" NIL \"foo\" \"bar.pt\")) NIL NIL NIL \"<002001ca32fe$dc7668b0$9600000a@ricardo>\")",1024);

	ck_assert_str_eq(result, expect);

	dbmail_message_free(message);
	g_free(result);
	result = NULL;

	//
	g_free(expect);
	expect = NULL;
}
END_TEST

START_TEST(test_imap_get_envelope_invaliddate)
{
	DbmailMessage *message;
	char *result, *expect;

	expect = g_new0(char, 1024);

	/* message with invalid date*/
	message = dbmail_message_new(NULL);
	message = dbmail_message_init_with_string(message, broken_message4);
	result = imap_get_envelope(GMIME_MESSAGE(message->content));

	g_strlcpy(expect,"(\"Fri, 3 Jun 202225 20:03:00 +0100\" \"Broken test message 4 with invalid date\" NIL NIL NIL ((\"Foo Bar\" NIL \"foo\" \"bar.pt\")) NIL NIL NIL \"<broken-test-04@p-o.co.uk>\")",1024);

	ck_assert_str_eq(result, expect);

	dbmail_message_free(message);
	g_free(result);
	result = NULL;

	//
	g_free(expect);
	expect = NULL;
}
END_TEST


START_TEST(test_imap_get_envelope_8bit_id)
{
	DbmailMessage *message;
	char *result, *expect;
	
	const char *msgid = "<000001c1f64e$c4a34180$0100007f@z=F0=B5=D241>";
	expect = g_new0(char, 1024);
	
	/* text/plain */
	message = dbmail_message_new(NULL);
	message = dbmail_message_init_with_string(message, rfc822);
	dbmail_message_set_header(message,"Message-ID",msgid);
	
	result = imap_get_envelope(GMIME_MESSAGE(message->content));
	g_strlcpy(expect,"(NIL \"dbmail test message\" ((NIL NIL \"somewher\" \"foo.org\")) ((NIL NIL \"somewher\" \"foo.org\")) ((NIL NIL \"somewher\" \"foo.org\")) ((NIL NIL \"testuser\" \"foo.org\")) NIL NIL NIL NIL)",1024);
	ck_assert_str_eq(result, expect);
	
	dbmail_message_set_header(message,"Message-ID","<123123123@foo.bar>");
	result = imap_get_envelope(GMIME_MESSAGE(message->content));
	g_strlcpy(expect,"(NIL \"dbmail test message\" ((NIL NIL \"somewher\" \"foo.org\")) ((NIL NIL \"somewher\" \"foo.org\")) ((NIL NIL \"somewher\" \"foo.org\")) ((NIL NIL \"testuser\" \"foo.org\")) NIL NIL NIL \"<123123123@foo.bar>\")",1024);

	ck_assert_str_eq(result, expect);

	dbmail_message_free(message);
	g_free(result);
	g_free(expect);
}
END_TEST

START_TEST(test_imap_get_envelope_koi)
{
	char *t;
	const char *exp = "(NIL \"test\" ((\"Антон Нехороших\" NIL \"bad\" \"foo.ru\")) ((\"Антон Нехороших\" NIL \"bad\" \"foo.ru\")) ((\"Антон Нехороших\" NIL \"bad\" \"foo.ru\")) ((NIL NIL \"nobody\" \"foo.ru\")) NIL NIL NIL NIL)";
	DbmailMessage *m = dbmail_message_new(NULL);

	m = dbmail_message_init_with_string(m, encoded_message_koi);
	t = imap_get_envelope(GMIME_MESSAGE(m->content));
 	ck_assert_str_eq(t, exp);

	g_free(t);
	dbmail_message_free(m);
	
}
END_TEST


			
#define F(a,b) c = imap_cleanup_address(a);\
	ck_assert_ptr_nonnull(c);\
	ck_assert_str_eq(c, b);\
	free(c);
#define Fnull(a,b) c = imap_cleanup_address(a);\
	ck_assert_ptr_nonnull(c); \
	ck_assert_str_eq(c, b);\
	free(c);

START_TEST(test_imap_cleanup_address)
{
	char *c = NULL;

	F("=?iso-8859-1?Q?B=BA_V._F._Z=EAzere?= <nobody@nowhere.org>","\"=?iso-8859-1?Q?B=BA_V._F._Z=EAzere?=\" <nobody@nowhere.org>");
	F("=?iso-8859-1?Q?\"B=BA_V._F._Z=EAzere\"?=<nobody@nowhere.org>","\"=?iso-8859-1?Q?B=BA_V._F._Z=EAzere?=\" <nobody@nowhere.org>");
	F("=?iso-8859-1?Q?B=BA_V._F._Z=EAzere?=<nobody@nowhere.org>","\"=?iso-8859-1?Q?B=BA_V._F._Z=EAzere?=\" <nobody@nowhere.org>");
	F("\"=?iso-8859-1?Q?B=BA_V._F._Z=EAzere?=\" <nobody@nowhere.org>","\"=?iso-8859-1?Q?B=BA_V._F._Z=EAzere?=\" <nobody@nowhere.org>");
	F("", "");
	Fnull(NULL, "");
	F("Some One <some@foo.org>", "Some One <some@foo.org>");
	F(" <some@foo.org>", "<some@foo.org>");
	F("=?ISO-8859-2?Q? \"Verlag=20Dash=F6fer=20-=20DU.cz?= =?ISO-8859-2?Q?\" ?= <e-noviny@smtp.dashofer.cz>",
	"\"=?ISO-8859-2?Q?Verlag=20Dash=F6fer=20-=20DU.cz?= =?ISO-8859-2?Q?"
		/* Stringify here to kill the '??=' trigraph. */ "?=\" <e-noviny@smtp.dashofer.cz>");
	F("=?ISO-8859-2?Q?=22Miroslav_=A9ulc_=28fordfrog=29=22?=\n"
	"	<fordfrog@gentoo.org>\n", "\"=?ISO-8859-2?Q?=22Miroslav_=A9ulc_=28fordfrog=29=22?=\" <fordfrog@gentoo.org>");

	F("=?iso-8859-1?Q?::_=5B_Arrty_=5D_::_=5B_Roy_=28L=29_St=E8phanie_=5D?=  <over.there@hotmail.com>",
		"\"=?iso-8859-1?Q?::_=5B_Arrty_=5D_::_=5B_Roy_=28L=29_St=E8phanie_=5D?=\" <over.there@hotmail.com>");

	F("\"First Address\" <first@foo.com>, =?iso-8859-1?Q?::_=5B_Arrty_=5D_::_=5B_Roy_=28L=29_St=E8phanie_=5D?=  <over.there@hotmail.com>",
		"\"First Address\" <first@foo.com>, \"=?iso-8859-1?Q?::_=5B_Arrty_=5D_::_=5B_Roy_=28L=29_St=E8phanie_=5D?=\" <over.there@hotmail.com>");

	//printf("[%s]\n", imap_cleanup_address("pr.latinnet <pr.latinnet@gmail.com>"));
	F("=?iso-8859-1?Q?Bombeiros_Vol._Mort=E1gua?=","\"=?iso-8859-1?Q?Bombeiros_Vol._Mort=E1gua?=\"");


}
END_TEST

START_TEST(test_imap_get_envelope_latin)
{
	char *result;
	char *expect = g_new0(char,1024);
	DbmailMessage *m;

	/*  */
	m = dbmail_message_new(NULL);
	m = dbmail_message_init_with_string(m, encoded_message_latin_1);
	
	result = imap_get_envelope(GMIME_MESSAGE(m->content));
	g_strlcpy(expect,"(NIL \"Re: Módulo Extintores\" ((\"Bº V. F. Zêzere\" NIL \"nobody\" \"nowhere.org\")) ((\"Bº V. F. Zêzere\" NIL \"nobody\" \"nowhere.org\")) ((\"Bº V. F. Zêzere\" NIL \"nobody\" \"nowhere.org\")) ((NIL NIL \"nobody\" \"foo.org\")) NIL NIL NIL NIL)",1024);
	ck_assert_str_eq(result, expect);

	g_free(result);
	dbmail_message_free(m);

	/*  */
	m = dbmail_message_new(NULL);
	m = dbmail_message_init_with_string(m, encoded_message_latin_2);
	
	g_strlcpy(expect,"(NIL \"Re: [gentoo-dev] New developer:  Miroslav Šulc (fordfrog)\" ((\"Miroslav Šulc (fordfrog)\" NIL \"fordfrog\" \"gentoo.org\")) ((\"Miroslav Šulc (fordfrog)\" NIL \"fordfrog\" \"gentoo.org\")) ((\"Miroslav Šulc (fordfrog)\" NIL \"fordfrog\" \"gentoo.org\")) ((NIL NIL \"gentoo-dev\" \"lists.gentoo.org\")) NIL NIL NIL NIL)",1024);
	result = imap_get_envelope(GMIME_MESSAGE(m->content));
	ck_assert_str_eq(result, expect);
	
	g_free(result);
	dbmail_message_free(m);

	/*  */
	m = dbmail_message_new(NULL);
	m = dbmail_message_init_with_string(m, encoded_message_utf8);

	g_strlcpy(expect,"(NIL \"ééé\" ((NIL NIL \"nobody\" \"nowhere.org\")) ((NIL NIL \"nobody\" \"nowhere.org\")) ((NIL NIL \"nobody\" \"nowhere.org\")) ((NIL NIL \"nobody\" \"foo.org\")) NIL NIL NIL NIL)",1024);
	result = imap_get_envelope(GMIME_MESSAGE(m->content));
	ck_assert_str_eq(result, expect);

	g_free(result);
	g_free(expect);
	dbmail_message_free(m);
}
END_TEST


START_TEST(test_imap_get_partspec1)
{
	DbmailMessage *message;
	GMimeObject *object;
	char *result, *expect;

	/* text/plain */
	message = dbmail_message_new(NULL);
	message = dbmail_message_init_with_string(message, rfc822);

	object = imap_get_partspec(GMIME_OBJECT(message->content),"HEADER");
	result = g_mime_object_to_string(object, NULL);
	expect = g_strdup(
		"Content-Type: text/plain; charset=\"us-ascii\"\n"
		"MIME-Version: 1.0\n"
		"Content-Transfer-Encoding: 7bit\n"
		"Message-Id: <1199706209l.3020l.1l@(none)>\n"
		"To: testuser@foo.org\n"
		"From: somewher@foo.org\n"
		"Subject: dbmail test message\n"
		"\n"
		"\n"
		"    this is a test message\n"
		"\n"
	);

	ck_assert_str_eq(result, expect);

	g_free(result);
	g_free(expect);

	dbmail_message_free(message);

}
END_TEST

START_TEST(test_imap_get_partspec2)
{
	DbmailMessage *message;
	GMimeObject *object;
	char *result, *expect;

	/* text/plain */
	message = dbmail_message_new(NULL);
	message = dbmail_message_init_with_string(message, rfc822);

	object = imap_get_partspec(GMIME_OBJECT(message->content),"HEADER");
	result = imap_get_logical_part(object,"HEADER");
	expect = g_strdup("Content-Type: text/plain; charset=\"us-ascii\"\r\n"
			"MIME-Version: 1.0\r\n"
			"Content-Transfer-Encoding: 7bit\r\n"
			"Message-Id: <1199706209l.3020l.1l@(none)>\r\n"
			"To: testuser@foo.org\r\n"
			"From: somewher@foo.org\r\n"
			"Subject: dbmail test message\r\n"
			"\r\n");

	ck_assert_str_eq(result, expect);

	g_free(result);
	g_free(expect);

	dbmail_message_free(message);

}
END_TEST

START_TEST(test_imap_get_partspec3)
{
	DbmailMessage *message;
	GMimeObject *object;
	char *result, *expect;

	/* text/plain */
	message = dbmail_message_new(NULL);
	message = dbmail_message_init_with_string(message, rfc822);

	object = imap_get_partspec(GMIME_OBJECT(message->content),"TEXT");
	result = imap_get_logical_part(object,"TEXT");
	expect = g_strdup("\r\n"
			"    this is a test message\r\n"
			"\r\n");
	ck_assert_str_eq(result, expect);

	g_free(result);
	g_free(expect);

	dbmail_message_free(message);

}
END_TEST

START_TEST(test_imap_get_partspec4)
{
	DbmailMessage *message;
	GMimeObject *object;
	char *result, *expect;

	/* multipart */
	message = dbmail_message_new(NULL);
	message = dbmail_message_init_with_string(message, multipart_message);

	/* test a simple mime part */
	object = imap_get_partspec(GMIME_OBJECT(message->content),"1");
	result = imap_get_logical_part(object,"MIME");
	expect = g_strdup("Content-type: text/html\r\n"
	        "Content-disposition: inline\r\n\r\n");
	ck_assert_str_eq(result, expect);

	g_free(result);
	g_free(expect);

	dbmail_message_free(message);

}
END_TEST

START_TEST(test_imap_get_partspec5)
{
	DbmailMessage *message;
	GMimeObject *object;
	char *result, *expect;

	/* multipart */
	message = dbmail_message_new(NULL);
	message = dbmail_message_init_with_string(message, multipart_message);

	object = imap_get_partspec(GMIME_OBJECT(message->content),"1");
	result = imap_get_logical_part(object,NULL);
	expect = g_strdup("Test message one\r\n and more.\r\n");
	ck_assert_str_eq(result, expect);

	g_free(result);
	g_free(expect);

	dbmail_message_free(message);

}
END_TEST

START_TEST(test_imap_get_partspec6)
{
	DbmailMessage *message;
	GMimeObject *object;
	char *result, *expect;

	/* object isn't a message/rfc822 so these are
	 * acually invalid. Let's try anyway */
	message = dbmail_message_new(NULL);
	message = dbmail_message_init_with_string(message, multipart_message);

	object = imap_get_partspec(GMIME_OBJECT(message->content),"1");
	result = imap_get_logical_part(object,"HEADER");
	expect = g_strdup("Content-type: text/html\r\n"
			"Content-disposition: inline\r\n\r\n");
	ck_assert_str_eq(result, expect);

	g_free(result);
	g_free(expect);

	dbmail_message_free(message);

}
END_TEST

START_TEST(test_imap_get_partspec7)
{
	DbmailMessage *message;
	GMimeObject *object;
	char *result, *expect;

	message = dbmail_message_new(NULL);
	message = dbmail_message_init_with_string(message, multipart_message);

	object = imap_get_partspec(GMIME_OBJECT(message->content),"1");
	result = imap_get_logical_part(object,"TEXT");
	expect = g_strdup("Test message one\r\n and more.\r\n");
	ck_assert_str_eq(result, expect);

	g_free(result);
	g_free(expect);

	dbmail_message_free(message);

}
END_TEST

START_TEST(test_imap_get_partspec8)
{
	DbmailMessage *message;
	GMimeObject *object;
	char *result, *expect;

	/* moving on */
	message = dbmail_message_new(NULL);
	message = dbmail_message_init_with_string(message, multipart_message);
	object = imap_get_partspec(GMIME_OBJECT(message->content),"2");
	result = imap_get_logical_part(object,"MIME");
	expect = g_strdup(
			"Content-type: text/plain; charset=us-ascii; name=testfile\r\n"
			"Content-transfer-encoding: base64\r\n"
			"\r\n"
			);
	ck_assert_str_eq(result, expect);

	g_free(result);
	g_free(expect);

	dbmail_message_free(message);

}
END_TEST

START_TEST(test_imap_get_partspec9)
{
	DbmailMessage *message;
	GMimeObject *object;
	char *result, *expect;
	int result2;

	/* multipart mixed */
	message = dbmail_message_new(NULL);
	message = dbmail_message_init_with_string(message, multipart_mixed);

	object = imap_get_partspec(GMIME_OBJECT(message->content),"2");
	result = imap_get_logical_part(object,"HEADER");
	expect = g_strdup("From: \"try\" <try@test.kisise>");
	ck_assert_str_eq(
		g_utf8_substring(expect, 0, g_utf8_strlen(expect, 29)),
		g_utf8_substring(result, 0, g_utf8_strlen(expect, 29))
	);

	g_free(result);
	g_free(expect);

	dbmail_message_free(message);

}
END_TEST

START_TEST(test_imap_get_partspec10)
{
	DbmailMessage *message;
	GMimeObject *object;
	char *result, *expect;

	message = dbmail_message_new(NULL);
	message = dbmail_message_init_with_string(message, multipart_mixed);

	object = imap_get_partspec(GMIME_OBJECT(message->content),"2.1.1");
	result = imap_get_logical_part(object,NULL);

	expect = g_strdup("Body of doc2\r\n\r\n");
	ck_assert_str_eq(result, expect);

	g_free(result);
	g_free(expect);

	dbmail_message_free(message);

}
END_TEST

START_TEST(test_imap_get_partspec11)
{
	DbmailMessage *message;
	GMimeObject *object;
	char *result, *expect;

	message = dbmail_message_new(NULL);
	message = dbmail_message_init_with_string(message, multipart_mixed);

	object = imap_get_partspec(GMIME_OBJECT(message->content),"2.1.1");
	result = imap_get_logical_part(object,"MIME");

	expect = g_strdup("Content-Type: text/plain;\r\n"
			"	charset=\"us-ascii\"\r\n"
			"Content-Transfer-Encoding: 7bit\r\n\r\n");
	ck_assert_str_eq(result, expect);

	g_free(result);
	g_free(expect);

	dbmail_message_free(message);

}
END_TEST

START_TEST(test_imap_get_partspec12)
{
	DbmailMessage *message;
	GMimeObject *object;
	char *result, *expect;

	/* multipart signed */
	message = dbmail_message_new(NULL);
	message = dbmail_message_init_with_string(message, multipart_signed);

	object = imap_get_partspec(GMIME_OBJECT(message->content),"1.1");
	result = imap_get_logical_part(object,NULL);
	expect = g_strdup("quo-pri text");
	ck_assert_str_eq(result, expect);

	g_free(result);
	g_free(expect);

	dbmail_message_free(message);

}
END_TEST

START_TEST(test_imap_get_partspec13)
{
	DbmailMessage *message;
	GMimeObject *object;
	char *result, *expect;

	message = dbmail_message_new(NULL);
	message = dbmail_message_init_with_string(message, multipart_signed);

	object = imap_get_partspec(GMIME_OBJECT(message->content),"1.3");
	result = g_mime_object_to_string(object, NULL);
	expect = g_strdup("Content-Type: message/rfc822;\n"
			"Content-Transfer-Encoding: 7bit\n"
			"Content-Disposition: attachment;\n"
			" filename=\"msg1.eml\"\n"
			"\n"
			"Date: Mon, 19 Aug 2013 14:54:05 +0200\n"
			"To: a@b\n"
			"From: d@b\n"
			"Reply-To: e@b\n"
			"Subject: msg1\n"
			"MIME-Version: 1.0\n"
			"Content-Type: multipart/alternative;\n"
			"	boundary=b1_7ad0d7cccab59d27194f9ad69c14606001f05f531376916845\n"
			"\n"
			"\n"
			"--b1_7ad0d7cccab59d27194f9ad69c14606001f05f531376916845\n"
			"Content-Type: text/plain; charset=\"ISO-8859-1\"\n"
			"Content-Transfer-Encoding: quoted-printable\n"
			"\n"
			"quo-pri text\n"
			"--b1_7ad0d7cccab59d27194f9ad69c14606001f05f531376916845\n"
			"Content-Type: text/html; charset=\"ISO-8859-1\"\n"
			"Content-Transfer-Encoding: quoted-printable\n"
			"\n"
			"html text\n"
			"\n"
			"--b1_7ad0d7cccab59d27194f9ad69c14606001f05f531376916845--\n"
			"\n"
			"\n"
			"\n");

	ck_assert_str_eq(result, expect);

	g_free(result);
	g_free(expect);

	dbmail_message_free(message);

}
END_TEST

START_TEST(test_imap_get_partspec14)
{
	DbmailMessage *message;
	GMimeObject *object;
	char *result, *expect;

	message = dbmail_message_new(NULL);
	message = dbmail_message_init_with_string(message, multipart_signed);

	object = imap_get_partspec(GMIME_OBJECT(message->content),"1.3");
	result = imap_get_logical_part(object,"MIME");
	expect = g_strdup("Content-Type: message/rfc822;\r\n"
			"Content-Transfer-Encoding: 7bit\r\n"
			"Content-Disposition: attachment;\r\n"
			" filename=\"msg1.eml\"\r\n"
			"\r\n");
	ck_assert_str_eq(result, expect);

	g_free(result);
	g_free(expect);

	dbmail_message_free(message);

}
END_TEST

START_TEST(test_imap_get_partspec15)
{
	DbmailMessage *message;
	GMimeObject *object;
	char *result, *expect;

	message = dbmail_message_new(NULL);
	message = dbmail_message_init_with_string(message, multipart_signed);

	object = imap_get_partspec(GMIME_OBJECT(message->content),"1.3");
	result = imap_get_logical_part(object,NULL);
	expect = g_strdup("Date: Mon, 19 Aug 2013 14:54:05 +0200\r\n"
			"To: a@b\r\n"
			"From: d@b\r\n"
			"Reply-To: e@b\r\n"
			"Subject: msg1\r\n"
			"MIME-Version: 1.0\r\n"
			"Content-Type: multipart/alternative;\r\n"
			"	boundary=b1_7ad0d7cccab59d27194f9ad69c14606001f05f531376916845\r\n"
			"\r\n"
			"\r\n"
			"--b1_7ad0d7cccab59d27194f9ad69c14606001f05f531376916845\r\n"
			"Content-Type: text/plain; charset=\"ISO-8859-1\"\r\n"
			"Content-Transfer-Encoding: quoted-printable\r\n"
			"\r\n"
			"quo-pri text\r\n"
			"--b1_7ad0d7cccab59d27194f9ad69c14606001f05f531376916845\r\n"
			"Content-Type: text/html; charset=\"ISO-8859-1\"\r\n"
			"Content-Transfer-Encoding: quoted-printable\r\n"
			"\r\n"
			"html text\r\n"
			"\r\n"
			"--b1_7ad0d7cccab59d27194f9ad69c14606001f05f531376916845--\r\n"
			"\r\n"
			"\r\n"
			"\r\n");

	ck_assert_str_eq(result, expect);

	g_free(result);
	g_free(expect);

	dbmail_message_free(message);

}
END_TEST

START_TEST(test_imap_get_partspec16)
{
	DbmailMessage *message;
	GMimeObject *object;
	char *result, *expect;

	message = dbmail_message_new(NULL);
	message = dbmail_message_init_with_string(message, multipart_signed);

	object = imap_get_partspec(GMIME_OBJECT(message->content),"1.3");
	result = imap_get_logical_part(object,"MIME");
	expect = g_strdup("Content-Type: message/rfc822;\r\n"
			"Content-Transfer-Encoding: 7bit\r\n"
			"Content-Disposition: attachment;\r\n"
			" filename=\"msg1.eml\"\r\n"
			"\r\n");
	ck_assert_str_eq(result, expect);

	g_free(result);
	g_free(expect);

	dbmail_message_free(message);

}
END_TEST

START_TEST(test_imap_get_partspec17)
{
	DbmailMessage *message;
	GMimeObject *object;
	char *result, *expect;

	message = dbmail_message_new(NULL);
	message = dbmail_message_init_with_string(message, multipart_signed);

	object = imap_get_partspec(GMIME_OBJECT(message->content),"1.3");
	result = imap_get_logical_part(object,"HEADER");
	expect = g_strdup("Date: Mon, 19 Aug 2013 14:54:05 +0200\r\n"
			"To: a@b\r\n"
			"From: d@b\r\n"
			"Reply-To: e@b\r\n"
			"Subject: msg1\r\n"
			"MIME-Version: 1.0\r\n"
			"Content-Type: multipart/alternative;\r\n"
			"\tboundary=b1_7ad0d7cccab59d27194f9ad69c14606001f05f531376916845\r\n"
			"\r\n");

	ck_assert_str_eq(result, expect);

	g_free(result);
	g_free(expect);

	dbmail_message_free(message);
}
END_TEST

START_TEST(test_g_list_slices)
{
	unsigned i=0;
	unsigned j=98;
	unsigned s=11;
	GList *list = NULL;
	GList *sub = NULL;
	for (i=0; i< j; i++) 
		list = g_list_append_printf(list, "ELEM_%d", i);
	list = g_list_slices(list, s);
	list = g_list_first(list);
	ck_assert_int_eq(g_list_length(list), 9);
	sub = g_string_split(g_string_new((gchar *)list->data), ",");
	ck_assert_uint_eq(g_list_length(sub), s);

	g_list_foreach(list,(GFunc)g_free,NULL);
	g_list_foreach(sub,(GFunc)g_free,NULL);
	
	i=0;
	j=17;
	s=100;
	list = NULL;
	sub = NULL;
	for (i=0; i< j; i++) 
		list = g_list_append_printf(list, "ELEM_%d", i);
	list = g_list_slices(list, s);
	list = g_list_first(list);
	ck_assert_int_eq(g_list_length(list), 1);
	sub = g_string_split(g_string_new((gchar *)list->data), ",");
	ck_assert_int_eq(g_list_length(sub), j);

}
END_TEST

START_TEST(test_g_list_slices_u64)
{
	unsigned i=0;
	unsigned j=98;
	unsigned s=11;
	uint64_t *l;
	GList *list = NULL;
	GList *sub = NULL;
	for (i=0; i< j; i++) {
		l = g_new0(uint64_t,1);
		*l = i;
		list = g_list_append(list, l);
	}
		
	list = g_list_slices_u64(list, s);
	list = g_list_first(list);
	ck_assert_int_eq(g_list_length(list), 9);
	
	sub = g_string_split(g_string_new((gchar *)list->data), ",");
	ck_assert_int_eq(g_list_length(sub), s);

	g_list_foreach(list,(GFunc)g_free,NULL);
	g_list_foreach(sub,(GFunc)g_free,NULL);
	
	i=0;
	j=17;
	s=100;
	list = NULL;
	sub = NULL;
	for (i=0; i< j; i++) 
		list = g_list_append_printf(list, "ELEM_%d", i);
	list = g_list_slices(list, s);
	list = g_list_first(list);
	ck_assert_int_eq(g_list_length(list), 1);
	sub = g_string_split(g_string_new((gchar *)list->data), ",");
	ck_assert_int_eq(g_list_length(sub), j);

}
END_TEST

unsigned int get_bound_lo(unsigned int * set, unsigned int setlen) {
	unsigned int index = 0;
	while (set[index]==0 && index < setlen) 
		index++;
	return index;
}
unsigned int get_bound_hi(unsigned int * set, unsigned int setlen) {
	int index = setlen;
	while (set[index]==0 && index >= 0) 
		index--;
	return (unsigned)index;
}
unsigned int get_count_on(unsigned int * set, unsigned int setlen) {
	unsigned int i, count = 0;
	for (i=0; i<setlen; i++)
		if (set[i])
			count++;
	return count;
}

#define BS(x, y) { \
	char *res = dm_base_subject(x);\
	ck_assert_str_eq(res, y);\
	free(res); \
}

START_TEST(test_dm_base_subject_re)
{
	BS("Re: foo","foo");
	BS("Re [issue123]: foo","foo");
	BS("Re: [issue123] foo","foo");
	BS("Re: [issue123] [Fwd: foo]","foo");
}
END_TEST
START_TEST(test_dm_base_subject_square)
{
	BS("[issue123] foo","foo");
	BS("[Dbmail-dev] [DBMail 0000240]: some bug report","some bug report");
}
END_TEST
START_TEST(test_dm_base_subject_fwd)
{
	BS("Fwd: foo","foo");
	BS("Fw: foo","foo");
}
END_TEST
START_TEST(test_dm_base_subject_test)
{
	BS("test\t\tspaces  here","test spaces here");
	BS("test strip trailer here (fwd) (fwd)","test strip trailer here");
}
END_TEST
START_TEST(test_dm_base_subject_silly)
{
	BS("Re: Fwd: [fwd: some silly test subject] (fwd)","some silly test subject");
	
}
END_TEST
START_TEST(test_dm_base_subject_koi8)
{
	BS("=?koi8-r?B?4snMxdTZIPcg5OXu+CDz8OXr9OHr7PEg9/Pl5+ThIOTs8SD34fMg8+8g8+vp5Ovv6iEg0yA=?=",
	   "билеты в день спектакля всегда для вас со скидкой! с");
//            "=?koi8-r?B?4snMxdTZIPcg5OXu+CDz8OXr9OHr7PEg9/Pl5+ThIOTs8SD34fMg8+8g8+vp5Ovv6iEg0yA=?=");
}
END_TEST
START_TEST(test_dm_base_subject_misc1)
{
	BS("[foo] Fwd: [bar] Re: fw: b (fWd)  (fwd)", "b");
	BS("b (a)", "b (a)");
	BS("Re: [FWD: c]", "c");
}
END_TEST
START_TEST(test_dm_base_subject_misc2)
{
	BS("[xyz]", "[xyz]");
	BS("=?iso-8859-1?q?_?=", "");
	BS("Re: =?utf-8?q?b?=", "b");
	BS("=?iso-8859-1?q?RE:_C?=", "c");
	BS("=?us-ascii?b?UmU6IGM=?=", "c");
}
END_TEST
START_TEST(test_dm_base_subject_misc3)
{
	BS("Ad: Re: Ad: Re: Ad: x", "ad: re: ad: re: ad: x");
	BS("re: [fwd: [fwd: re: [fwd: babylon]]]", "babylon");
	BS("C", "c");
	BS(" ", "");
}
END_TEST
START_TEST(test_dm_base_subject_misc4)
{
	BS("=?ISO-8859-1?B?SWYgeW91IGNhbiByZWFkIHRoaXMgeW8=?=\n"
		" =?ISO-8859-2?B?dSB1bmRlcnN0YW5kIHRoZSBleGFtcGxlLg==?=",
		"if you can read this you understand the example.")
	BS("=?UTF-8?B?SGVsbG8gV29ybGQK?=", "hello world");
	BS("=?UTF-8?B?0J/RgNC40LLQtdGCINC40Lcg0KDQvtGB0YHQuNC4IChIZWxsbyBmcm8=?="
		" =?UTF-8?B?bSBSdXNzaWEp?=", "привет из россии (hello from russia)");
}
END_TEST
START_TEST(test_dm_base_subject_re_blank)
{
	BS("Re:", "");
	BS("Re: Re: ", "");
	BS("Re: Fwd: ", "");
}
END_TEST
START_TEST(test_dm_base_subject_utf8)
{
	BS("=?UTF-8?B?16nXnNeV150g15HXoteR16jXmdeqIChIZWxsbyBpbiBIZWJyZXcpIA==?=",
			"שלום בעברית (hello in hebrew)");
	BS("=?UTF-8?B?2YXYsdit2KjYpyDYqNin2YTZhNi62Kkg2KfZhNi52LHYqNmK2KkgKEg=?= =?UTF-8?B?ZWxsb3cgaW4gQXJhYmljKQ==?=",
			"مرحبا باللغة العربية (hellow in arabic)");
}
END_TEST

#define Y(z,q) ck_assert_int_eq(z, q)
#define X(z,a,b) Y(z,listex_match(a,b,".",0))
#define N(z,a,b) Y(z,listex_match(a,b,"¿",0))
START_TEST(test_listex_match)
{
	X(1, "INBOX", "INBOX");
	X(0, "INBOX", "INBOX.Foo");
	X(0, "INBOX", "INBOX.Foo.Bar");

	X(0, "INBOX.%", "INBOX");
	X(1, "INBOX.%", "INBOX.Foo");
	X(0, "INBOX.%", "INBOX.Foo.Bar");

	X(0, "INBOX.*", "INBOX");
	X(1, "INBOX.*", "INBOX.Foo");
	X(1, "INBOX.*", "INBOX.Foo.Bar");

	X(1, "INBOX*", "INBOX");
	X(1, "INBOX*", "INBOX.Foo");
	X(1, "INBOX*", "INBOX.Foo.Bar");

	X(1, "%", "INBOX");
	X(0, "%.%", "INBOX");
	X(1, "%.%", "INBOX.Foo");
	X(0, "%.%.%", "INBOX.Foo");
	X(1, "%.%.%", "INBOX.Foo.Bar");
	X(1, "%.%o.%", "INBOX.Foo.Bar");
	X(0, "%.%oo.%", "INBOX.Foa.Bar");
	X(1, "%.%o.%ar", "INBOX.Foo.Bar");

	X(1, "INBOX%", "INBOX");
	X(0, "INBOX%", "INBOX.Foo");
	X(0, "INBOX%", "INBOX.Foo.Bar");

	X(0, "INBOX*Foo", "INBOX");
	X(1, "INBOX*Foo", "INBOX.Foo");
	X(0, "INBOX*Foo", "INBOX.Foo.Bar");

	X(0, "INBOX*Bar", "INBOX");
	X(0, "INBOX*Bar", "INBOX.Foo");
	X(1, "INBOX*Bar", "INBOX.Foo.Bar");

	X(0, "INBOX.*Bar", "INBOX");
	X(0, "INBOX.*Bar", "INBOX.Foo");
	X(1, "INBOX.*Bar", "INBOX.Foo.Bar");

	N(0, "INBOX\317\200*Bar", "INBOX");
	N(0, "INBOX\317\200*Bar", "INBOX\317\200""Foo");
	N(1, "INBOX\317\200*Bar", "INBOX\317\200""Foo\317\200""Bar");

}
END_TEST

#define D(x,y) c = date_sql2imap(x); ck_assert_str_eq(c, y); free(c)
START_TEST(test_date_sql2imap)
{
	char *c = NULL;
        D("2005-05-03 14:10:06","03-May-2005 14:10:06 +0000");
        D("2005-01-03 14:10:06","03-Jan-2005 14:10:06 +0000");
}
END_TEST

Suite *dbmail_suite(void)
{
	Suite *s = suite_create("Dbmail Imap");
	TCase *tc_session = tcase_create("ImapSession");
	TCase *tc_util = tcase_create("Utils");
	TCase *tc_misc = tcase_create("Misc");
	
	suite_add_tcase(s, tc_session);
	suite_add_tcase(s, tc_util);
	suite_add_tcase(s, tc_misc);
	
	tcase_add_checked_fixture(tc_session, setup, teardown);
	tcase_add_test(tc_session, test_imap_session_new);
	tcase_add_test(tc_session, test_imap_get_structure_bare_bones);
	tcase_add_test(tc_session, test_imap_get_structure_text_plain);
	tcase_add_test(tc_session, test_imap_get_structure_multipart);
	tcase_add_test(tc_session, test_imap_get_structure_multipart_alternative);
	tcase_add_test(tc_session, test_imap_get_structure_multipart_apple);
	tcase_add_test(tc_session, test_imap_get_structure_rfc2231);
	tcase_add_test(tc_session, test_imap_get_structure_multipart_signed);
	tcase_add_test(tc_session, test_imap_cleanup_address);
	tcase_add_test(tc_session, test_internet_address_list_parse_string);
	tcase_add_test(tc_session, test_imap_get_envelope);
	tcase_add_test(tc_session, test_imap_get_envelope_baremessage);
	tcase_add_test(tc_session, test_imap_get_envelope_groupaddresses);
	tcase_add_test(tc_session, test_imap_get_envelope_brokenfrom);
	tcase_add_test(tc_session, test_imap_get_envelope_invaliddate);
	tcase_add_test(tc_session, test_imap_get_envelope_8bit_id);
	tcase_add_test(tc_session, test_imap_get_envelope_koi);
	tcase_add_test(tc_session, test_imap_get_envelope_latin);
	tcase_add_test(tc_session, test_imap_get_partspec1);
	tcase_add_test(tc_session, test_imap_get_partspec2);
	tcase_add_test(tc_session, test_imap_get_partspec3);
	tcase_add_test(tc_session, test_imap_get_partspec4);
	tcase_add_test(tc_session, test_imap_get_partspec5);
	tcase_add_test(tc_session, test_imap_get_partspec6);
	tcase_add_test(tc_session, test_imap_get_partspec7);
	tcase_add_test(tc_session, test_imap_get_partspec8);
	tcase_add_test(tc_session, test_imap_get_partspec9);
	tcase_add_test(tc_session, test_imap_get_partspec10);
	tcase_add_test(tc_session, test_imap_get_partspec11);
	tcase_add_test(tc_session, test_imap_get_partspec12);
	tcase_add_test(tc_session, test_imap_get_partspec13);
	tcase_add_test(tc_session, test_imap_get_partspec14);
	tcase_add_test(tc_session, test_imap_get_partspec15);
	tcase_add_test(tc_session, test_imap_get_partspec16);
	tcase_add_test(tc_session, test_imap_get_partspec17);
	tcase_add_checked_fixture(tc_util, setup, teardown);
	tcase_add_test(tc_util, test_dbmail_imap_plist_as_string);
	tcase_add_test(tc_util, test_dbmail_imap_plist_collapse);
	tcase_add_test(tc_util, test_dbmail_imap_astring_as_string);
	tcase_add_test(tc_util, test_g_list_slices);
	tcase_add_test(tc_util, test_g_list_slices_u64);
	tcase_add_test(tc_util, test_listex_match);
	tcase_add_test(tc_util, test_date_sql2imap);
	tcase_add_checked_fixture(tc_misc, setup, teardown);
	tcase_add_test(tc_misc, test_dm_base_subject_re);
	tcase_add_test(tc_misc, test_dm_base_subject_square);
	tcase_add_test(tc_misc, test_dm_base_subject_fwd);
	tcase_add_test(tc_misc, test_dm_base_subject_test);
	tcase_add_test(tc_misc, test_dm_base_subject_silly);
	tcase_add_test(tc_misc, test_dm_base_subject_koi8);
	tcase_add_test(tc_misc, test_dm_base_subject_misc1);
	tcase_add_test(tc_misc, test_dm_base_subject_misc2);
	tcase_add_test(tc_misc, test_dm_base_subject_misc3);
	tcase_add_test(tc_misc, test_dm_base_subject_misc4);
	tcase_add_test(tc_misc, test_dm_base_subject_re_blank);
	tcase_add_test(tc_misc, test_dm_base_subject_utf8);
	return s;
}

int main(void)
{
	int nf;
	g_mime_init();
	Suite *s = dbmail_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_VERBOSE);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	g_mime_shutdown();
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
	

