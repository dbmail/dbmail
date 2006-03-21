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

#include <check.h>
#include "check_dbmail.h"

extern char *configFile;
extern db_param_t _db_params;

#define DBPFX _db_params.pfx

/* we need this one because we can't directly link imapd.o */
int imap_before_smtp = 0;
extern u64_t msgbuf_idx;
extern u64_t msgbuf_buflen;

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
        u64_t user_idnr;
	if (! (auth_user_exists("testuser1",&user_idnr)))
		auth_adduser("testuser1","test", "md5", 101, 1024000, &user_idnr);
}
	
void setup(void)
{
	configure_debug(5,0);
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

START_TEST(test_dbmail_imap_plist_as_string)
{
	char *result;
	GList *l;

	l = NULL;
	l = g_list_append(l, "NIL");
	l = g_list_append(l, "NIL");
	result = dbmail_imap_plist_as_string(l);
	fail_unless(strcmp(result,"(NIL NIL)")==0,"plist construction failed");
	
	//g_list_foreach(l,(GFunc)g_free,NULL);
	g_free(result);
	
	l = NULL;
	l = g_list_append(l, "(NIL NIL)");
	result = dbmail_imap_plist_as_string(l);
	fail_unless(strcmp(result,"(NIL NIL)")==0,"plist construction failed");
	
	//g_list_foreach(l,(GFunc)g_free,NULL);
	g_free(result);

	l = g_list_append(NULL, "NIL");
	l = g_list_append(l, "NIL");
	l = g_list_append(l, "(NIL NIL)");
	l = g_list_append(l, "(NIL NIL)");
	result = dbmail_imap_plist_as_string(l);
	fail_unless(strcmp(result,"(NIL NIL (NIL NIL) (NIL NIL))")==0,"plist construction failed");
	
	//g_list_foreach(l,(GFunc)g_free,NULL);
	g_free(result);
}
END_TEST

START_TEST(test_dbmail_imap_plist_collapse)
{
	char *result;
	char *in = "(NIL) (NIL) (NIL)";
	result = dbmail_imap_plist_collapse(in);
	fail_unless(strcmp(result,"(NIL)(NIL)(NIL)")==0,"plist collapse failed");
}
END_TEST


#define A(x,y) fail_unless(strcmp(y,dbmail_imap_astring_as_string(x))==0,"dbmail_imap_astring_as_string failed")
START_TEST(test_dbmail_imap_astring_as_string)
{
	A("test","\"test\"");
	A("\"test\"","\"test\"");
	A("\"test","{5}\r\n\"test");
	A("testÃ","{5}\r\ntestÃ");
	A("test\"","{5}\r\ntest\"");
	A("test\"","{5}\r\ntest\"");
	A("test\\","{5}\r\ntest\\");
	A("test\225","{5}\r\ntest\225");
}
END_TEST

START_TEST(test_imap_session_new)
{
	struct ImapSession *s;
	s = dbmail_imap_session_new();
	fail_unless(s!=NULL, "Failed to initialize imapsession");
	dbmail_imap_session_delete(s);
}
END_TEST

START_TEST(test_imap_bodyfetch)
{
	int result;
	guint64 octet;
	struct ImapSession *s = dbmail_imap_session_new();

	dbmail_imap_session_bodyfetch_new(s);
	
	fail_unless(0 == dbmail_imap_session_bodyfetch_get_last_octetstart(s), "octetstart init value incorrect");
	fail_unless(0 == dbmail_imap_session_bodyfetch_get_last_octetcnt(s), "octetcnt init value incorrect");
	fail_unless(0 == dbmail_imap_session_bodyfetch_get_last_argstart(s), "argstart init value incorrect");
	
	dbmail_imap_session_bodyfetch_set_argstart(s,23);
	result = dbmail_imap_session_bodyfetch_get_last_argstart(s);
	fail_unless(result==23, "argstart incorrect");
	
	dbmail_imap_session_bodyfetch_set_octetstart(s,0);
	octet = dbmail_imap_session_bodyfetch_get_last_octetstart(s);
	fail_unless(octet==0, "octetstart incorrect");
	
	dbmail_imap_session_bodyfetch_set_octetcnt(s,12288);
	octet = dbmail_imap_session_bodyfetch_get_last_octetcnt(s);
	fail_unless(octet==12288, "octetcnt incorrect");
	
	dbmail_imap_session_delete(s);
		
}
END_TEST

//int find_deliver_to_header_addresses(struct DbmailMessage *self, char *scan_for_field, 
//	struct dm_list *targetlist)
//
START_TEST(test_find_deliver_to_header_addresses)
{
	int result;
	struct dm_list targetlist;
	struct DbmailMessage *m;

	m = dbmail_message_new();
	m = dbmail_message_init_with_string(m,g_string_new(multipart_message));
	
	dm_list_init(&targetlist);
	
	result = find_deliver_to_header_addresses(m, "Cc", &targetlist);
	struct element *el;
	el = targetlist.start;

	fail_unless(result==0, "find_deliver_to_header_addresses failed");
	fail_unless(targetlist.total_nodes==2,"find_deliver_to_header_addresses failed");
	fail_unless(strcmp((char *)el->data,"nobody@test123.com")==0, "find_deliver_to_header_addresses failed");

	dbmail_message_free(m);
}
END_TEST

START_TEST(test_g_mime_object_get_body)
{
	char * result;
	struct DbmailMessage *m;

	m = dbmail_message_new();
	m = dbmail_message_init_with_string(m,g_string_new(multipart_message));
	
	result = g_mime_object_get_body(GMIME_OBJECT(m->content));
	fail_unless(strlen(result)==1045,"g_mime_object_get_body failed");
	
}
END_TEST

START_TEST(test_imap_get_structure)
{
	struct DbmailMessage *message;
	char *result;
	char *expect = g_new0(char,1024);

	/* multipart */
	message = dbmail_message_new();
	message = dbmail_message_init_with_string(message, g_string_new(multipart_message));
	result = imap_get_structure(GMIME_MESSAGE(message->content), 1);
	strncpy(expect,"((\"text\" \"html\" NIL NIL NIL NIL 16 1 NIL (\"inline\") NIL NIL)"
			"(\"text\" \"plain\" (\"charset\" \"us-ascii\" \"name\" \"testfile\") NIL NIL \"base64\" 432 7 NIL NIL NIL NIL)"
			" \"mixed\" (\"boundary\" \"boundary\") NIL NIL NIL)",1024);
	//printf("\n[%s]\n[%s]\n", expect, result);
	fail_unless(strncasecmp(result,expect,1024)==0, "imap_get_structure failed");
	g_free(result);
	dbmail_message_free(message);

	/* multipart alternative */
	message = dbmail_message_new();
	message = dbmail_message_init_with_string(message, g_string_new(multipart_alternative));
	result = imap_get_structure(GMIME_MESSAGE(message->content), 1);
	strncpy(expect,"(((\"TEXT\" \"PLAIN\" (\"CHARSET\" \"ISO-8859-1\") NIL NIL \"7BIT\" 281 10 NIL NIL NIL NIL)(\"TEXT\" \"HTML\" (\"CHARSET\" \"ISO-8859-1\") NIL NIL \"7BIT\" 759 17 NIL NIL NIL NIL) \"ALTERNATIVE\" (\"BOUNDARY\" \"------------040302030903000400040101\") NIL NIL NIL)(\"IMAGE\" \"JPEG\" (\"NAME\" \"jesse_2.jpg\") NIL NIL \"BASE64\" 262 NIL (\"INLINE\" (\"FILENAME\" \"jesse_2.jpg\")) NIL NIL) \"MIXED\" (\"BOUNDARY\" \"------------050000030206040804030909\") NIL NIL NIL)",1024);

	fail_unless(strncasecmp(result,expect,1024)==0, "imap_get_structure failed");
	g_free(result);
	dbmail_message_free(message);
	
	/* text/plain */
	message = dbmail_message_new();
	message = dbmail_message_init_with_string(message, g_string_new(rfc822));
	result = imap_get_structure(GMIME_MESSAGE(message->content), 1);
	strncpy(expect,"(\"text\" \"plain\" (\"charset\" \"us-ascii\") NIL NIL \"7bit\" 32 4 NIL NIL NIL NIL)",1024);
	fail_unless(strncasecmp(result,expect,1024)==0, "imap_get_structure failed");
	g_free(result);
	g_free(expect);
	dbmail_message_free(message);

}
END_TEST

START_TEST(test_imap_get_envelope)
{
	struct DbmailMessage *message;
	char *result, *expect;
	
	expect = g_new0(char, 1024);
	
	/* text/plain */
	message = dbmail_message_new();
	message = dbmail_message_init_with_string(message, g_string_new(rfc822));
	result = imap_get_envelope(GMIME_MESSAGE(message->content));
	strncpy(expect,"(\"Thu, 01 Jan 1970 00:00:00 +0000\" \"dbmail test message\" ((NIL NIL \"somewher\" \"foo.org\")) ((NIL NIL \"somewher\" \"foo.org\")) ((NIL NIL \"somewher\" \"foo.org\")) ((NIL NIL \"testuser\" \"foo.org\")) NIL NIL NIL NIL)",1024);
	fail_unless(strncasecmp(result,expect,1024)==0, "imap_get_envelope failed");

	dbmail_message_free(message);
	g_free(result);
}
END_TEST

START_TEST(test_imap_get_partspec)
{
	struct DbmailMessage *message;
	GMimeObject *object;
	char *result, *expect;
	
	expect = g_new0(char, 1024);
	
	/* text/plain */
	message = dbmail_message_new();
	message = dbmail_message_init_with_string(message, g_string_new(rfc822));

	object = imap_get_partspec(GMIME_OBJECT(message->content),"HEADER");
	result = imap_get_logical_part(object,"HEADER");
//	printf("{%d} [%s]", strlen(result), result);
	fail_unless(strlen(result)==206,"imap_get_partspec failed");

	object = imap_get_partspec(GMIME_OBJECT(message->content),"TEXT");
	result = imap_get_logical_part(object,"TEXT");
	fail_unless(strlen(result)==29,"imap_get_partspec failed");

	dbmail_message_free(message);

	/* multipart */
	
	message = dbmail_message_new();
	message = dbmail_message_init_with_string(message, g_string_new(multipart_message));

	object = imap_get_partspec(GMIME_OBJECT(message->content),"1.TEXT");
	result = imap_get_logical_part(object,"TEXT");
	fail_unless(strlen(result)==16,"imap_get_partspec failed");

	object = imap_get_partspec(GMIME_OBJECT(message->content),"1.HEADER");
	result = imap_get_logical_part(object,"HEADER");
	fail_unless(strlen(result)==53,"imap_get_partspec failed");
	
	object = imap_get_partspec(GMIME_OBJECT(message->content),"2.MIME");
	result = imap_get_logical_part(object,"MIME");
	fail_unless(strlen(result)==93,"imap_get_partspec failed");

	
	g_free(result);
	g_free(expect);

}
END_TEST

static u64_t get_physid(void)
{
	u64_t id = 0;
	GString *q = g_string_new("");
	g_string_printf(q,"select id from %sphysmessage order by id desc limit 1", DBPFX);
	db_query(q->str);
	g_string_free(q,TRUE);
	id = db_get_result_u64(0,0);
	db_free_result();
	return id;
}

START_TEST(test_imap_message_fetch_headers)
{
	char *res;
	u64_t physid=get_physid();
	GString *headers = g_string_new("From To Cc Subject Date Message-ID Priority X-Priority References Newsgroups In-Reply-To Content-Type");
	GList *h = g_string_split(headers," ");
	
	res = imap_message_fetch_headers(physid,h,0);
	fail_unless(strlen(res) > 0,"imap_message_fetch_headers failed");
	g_free(res);

	res = imap_message_fetch_headers(physid,h,1);
	fail_unless(strlen(res) > 0,"imap_message_fetch_headers failed");
	g_free(res);

	g_string_free(headers,TRUE);
	
	g_list_foreach(h,(GFunc)g_free,NULL);
	g_list_free(h);


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
	fail_unless(g_list_length(list)==9, "number of slices incorrect");
	sub = g_string_split(g_string_new((gchar *)list->data), ",");
	fail_unless(g_list_length(sub)==s,"Slice length incorrect");

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
	fail_unless(g_list_length(list)==1, "number of slices incorrect");
	sub = g_string_split(g_string_new((gchar *)list->data), ",");
	fail_unless(g_list_length(sub)==j,"Slice length incorrect");

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

	
static char *wrap_base_subject(char *in) 
{
	gchar *out = g_strdup(in);
	dm_base_subject(out);
	return out;
}	

START_TEST(test_dm_base_subject)
{
	gchar *subject;

	fail_unless(strcmp(wrap_base_subject("Re: foo"),"foo")==0,"dm_base_subject failed");
	fail_unless(strcmp(wrap_base_subject("Fwd: foo"),"foo")==0,"dm_base_subject failed");
	fail_unless(strcmp(wrap_base_subject("Fw: foo"),"foo")==0,"dm_base_subject failed");
	fail_unless(strcmp(wrap_base_subject("[issue123] foo"),"foo")==0,"dm_base_subject failed");
	fail_unless(strcmp(wrap_base_subject("Re [issue123]: foo"),"foo")==0,"dm_base_subject failed");
	fail_unless(strcmp(wrap_base_subject("Re: [issue123] foo"),"foo")==0,"dm_base_subject failed");
	fail_unless(strcmp(wrap_base_subject("Re: [issue123] [Fwd: foo]"),"foo")==0,"dm_base_subject failed");
	fail_unless(strcmp(wrap_base_subject("[Dbmail-dev] [DBMail 0000240]: some bug report"),"some bug report")==0,"dm_base_subject failed");

	fail_unless(strcmp(wrap_base_subject("test\t\tspaces  here"),"test spaces here")==0,"cleanup of spaces failed");
	fail_unless(strcmp(wrap_base_subject("test strip trailer here (fwd) (fwd)"),"test strip trailer here")==0,"cleanup of sub-trailer failed");
	fail_unless(strcmp(wrap_base_subject("Re: Fwd: [fwd: some silly test subject] (fwd)"),"some silly test subject")==0, "stripping of subj-tlr/subj-ldr failed");
	
	subject = g_strdup("=?koi8-r?B?4snMxdTZIPcg5OXu+CDz8OXr9OHr7PEg9/Pl5+ThIOTs8SD34fMg8+8g8+vp5Ovv6iEg0yA=?=");
	dm_base_subject(subject);

}
END_TEST

#define Y(z,q) fail_unless(z==(q), "listex_match failed")
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

START_TEST(test_dm_getguid)
{
	fail_unless(dm_getguid(1) < dm_getguid(99), "dm_getguid failed");
	fail_unless(dm_getguid(99) < dm_getguid(1), "dm_getguid failed");
}
END_TEST

/* this test will fail if you're not in the CET timezone */
#define D(x,y) fail_unless(strncasecmp(date_sql2imap(x),y,IMAP_INTERNALDATE_LEN)==0,"date_sql2imap failed")
START_TEST(test_date_sql2imap)
{
//	printf("[%s]\n", date_sql2imap("2005-05-03 14:10:06"));
//	printf("[%s]\n", date_sql2imap("2005-01-03 14:10:06"));
        D("2005-05-03 14:10:06","03-May-2005 14:10:06 +0200");
        D("2005-01-03 14:10:06","03-Jan-2005 14:10:06 +0100");
}
END_TEST

Suite *dbmail_suite(void)
{
	Suite *s = suite_create("Dbmail Imap");
	TCase *tc_session = tcase_create("ImapSession");
	TCase *tc_mime = tcase_create("Mime");
	TCase *tc_util = tcase_create("Utils");
	TCase *tc_misc = tcase_create("Misc");
	
	suite_add_tcase(s, tc_session);
	suite_add_tcase(s, tc_mime);
	suite_add_tcase(s, tc_util);
	suite_add_tcase(s, tc_misc);
	
	tcase_add_checked_fixture(tc_session, setup, teardown);
	tcase_add_test(tc_session, test_imap_session_new);
	tcase_add_test(tc_session, test_imap_bodyfetch);
	tcase_add_test(tc_session, test_imap_get_structure);
	tcase_add_test(tc_session, test_imap_get_envelope);
	tcase_add_test(tc_session, test_imap_get_partspec);
	tcase_add_test(tc_session, test_imap_message_fetch_headers);
	
	tcase_add_checked_fixture(tc_mime, setup, teardown);
	tcase_add_test(tc_mime, test_find_deliver_to_header_addresses);
	tcase_add_test(tc_mime, test_g_mime_object_get_body);

	tcase_add_checked_fixture(tc_util, setup, teardown);
	tcase_add_test(tc_util, test_g_list_join);
	tcase_add_test(tc_util, test_dbmail_imap_plist_as_string);
	tcase_add_test(tc_util, test_dbmail_imap_plist_collapse);
	tcase_add_test(tc_util, test_dbmail_imap_astring_as_string);
	tcase_add_test(tc_util, test_g_list_slices);
	tcase_add_test(tc_util, test_listex_match);
	tcase_add_test(tc_util, test_date_sql2imap);

	tcase_add_checked_fixture(tc_misc, setup, teardown);
	tcase_add_test(tc_misc, test_dm_base_subject);
	tcase_add_test(tc_misc, test_dm_getguid);
	return s;
}

int main(void)
{
	int nf;
	Suite *s = dbmail_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
	

