/*
 *   Copyright (c) 2004-2006 NFG Net Facilities Group BV support@nfg.nl
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

extern char *multipart_message;
extern char *multipart_message_part;
extern char *raw_lmtp_data;

/*
 *
 * the test fixtures
 *
 */

	
void setup(void)
{
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
	config_free();
}

//struct DbmailMessage * dbmail_message_new(void);
START_TEST(test_dbmail_message_new)
{
	struct DbmailMessage *m = dbmail_message_new();
	fail_unless(m->id==0,"dbmail_message_new failed");
	dbmail_message_free(m);
}
END_TEST
//void dbmail_message_set_class(struct DbmailMessage *self, int klass);
START_TEST(test_dbmail_message_set_class)
{
	int result;
	struct DbmailMessage *m = dbmail_message_new();
	result = dbmail_message_set_class(m,DBMAIL_MESSAGE);
	fail_unless(result==0,"dbmail_message_set_class failed");
	result = dbmail_message_set_class(m,DBMAIL_MESSAGE_PART);
	fail_unless(result==0,"dbmail_message_set_class failed");
	result = dbmail_message_set_class(m,10);
	fail_unless(result!=0,"dbmail_message_set_class failed");
	
	dbmail_message_free(m);
}
END_TEST
//int dbmail_message_get_class(struct DbmailMessage *self);
START_TEST(test_dbmail_message_get_class)
{
	int klass;
	struct DbmailMessage *m = dbmail_message_new();
	
	/* default */
	klass = dbmail_message_get_class(m);
	fail_unless(klass==DBMAIL_MESSAGE,"dbmail_message_get_class failed");
	
	/* set explicitely */
	dbmail_message_set_class(m,DBMAIL_MESSAGE_PART);
	klass = dbmail_message_get_class(m);
	fail_unless(klass==DBMAIL_MESSAGE_PART,"dbmail_message_get_class failed");
	
	dbmail_message_set_class(m,DBMAIL_MESSAGE);
	klass = dbmail_message_get_class(m);
	fail_unless(klass==DBMAIL_MESSAGE,"dbmail_message_get_class failed");

	dbmail_message_free(m);
}
END_TEST

//struct DbmailMessage * dbmail_message_retrieve(struct DbmailMessage *self, u64_t physid, int filter);
START_TEST(test_dbmail_message_retrieve)
{
	struct DbmailMessage *m, *n;
	u64_t physid;

	m = dbmail_message_new();
	m = dbmail_message_init_with_string(m, g_string_new(multipart_message));
	dbmail_message_set_header(m, 
			"References", 
			"<20050326155326.1afb0377@ibook.linuks.mine.nu> <20050326181954.GB17389@khazad-dum.debian.net> <20050326193756.77747928@ibook.linuks.mine.nu> ");
	dbmail_message_store(m);

	physid = dbmail_message_get_physid(m);
	
	n = dbmail_message_new();
	n = dbmail_message_retrieve(n,physid,DBMAIL_MESSAGE_FILTER_HEAD);	
	fail_unless(n != NULL, "dbmail_message_retrieve failed");
	fail_unless(n->content != NULL, "dbmail_message_retrieve failed");

	dbmail_message_free(m);
	dbmail_message_free(n);

}
END_TEST
//struct DbmailMessage * dbmail_message_init_with_string(struct DbmailMessage *self, const GString *content);
START_TEST(test_dbmail_message_init_with_string)
{
	struct DbmailMessage *m;
	GTuples *t;
	GString *s;
	
	s = g_string_new(multipart_message);
	m = dbmail_message_new();
	m = dbmail_message_init_with_string(m,s);
	g_string_free(s,TRUE);
	
	t = g_relation_select(m->headers, "Received", 0);
	fail_unless(t->len==2,"Too few or too many headers in tuple");
	g_tuples_destroy(t);
	dbmail_message_free(m);
	
	s = g_string_new(simple_message_part);
	m = dbmail_message_new();
	m = dbmail_message_init_with_string(m, s);
	g_string_free(s,TRUE);
	
	fail_unless(dbmail_message_get_class(m)==DBMAIL_MESSAGE_PART, "init_with string failed");
	
	dbmail_message_free(m);
}
END_TEST

START_TEST(test_dbmail_message_get_internal_date)
{
	struct DbmailMessage *m;
	GString *s;

	s = g_string_new(rfc822);

	m = dbmail_message_new();
	m = dbmail_message_init_with_string(m, s);
	// From_ contains: Wed Sep 14 16:47:48 2005
	const char *expect = "2005-09-14 16:47:48";
	char *result = dbmail_message_get_internal_date(m);
		
	fail_unless(MATCH(expect,result),"dbmail_message_get_internal_date failed");

	g_free(result);
	g_string_free(s,TRUE);
	dbmail_message_free(m);
	
}
END_TEST

START_TEST(test_dbmail_message_to_string)
{
        char *result;
	GString *s;
	struct DbmailMessage *m;
        
	s = g_string_new(multipart_message);
	
	m = dbmail_message_new();
	m = dbmail_message_init_with_string(m, s);
	
        result = dbmail_message_to_string(m);
	fail_unless(strlen(result)==s->len, "dbmail_message_to_string failed");
	
        g_string_free(s,TRUE);
	g_free(result);
	dbmail_message_free(m);
}
END_TEST
    
//struct DbmailMessage * dbmail_message_init_with_stream(struct DbmailMessage *self, GMimeStream *stream, int type);
/*
START_TEST(test_dbmail_message_init_with_stream)
{
}
END_TEST
*/
//gchar * dbmail_message_hdrs_to_string(struct DbmailMessage *self);

START_TEST(test_dbmail_message_hdrs_to_string)
{
	char *result;
	GString *s;
	struct DbmailMessage *m;
	
	s = g_string_new(multipart_message);
	m = dbmail_message_new();
        m = dbmail_message_init_with_string(m, s);

	result = dbmail_message_hdrs_to_string(m);
	fail_unless(strlen(result)==485, "dbmail_message_hdrs_to_string failed");
	
	g_string_free(s,TRUE);
        dbmail_message_free(m);
	g_free(result);
}
END_TEST

//gchar * dbmail_message_body_to_string(struct DbmailMessage *self);

START_TEST(test_dbmail_message_body_to_string)
{
	char *result;
	GString *s;
	struct DbmailMessage *m;
	
	s = g_string_new(multipart_message);
	m = dbmail_message_new();
        m = dbmail_message_init_with_string(m,s);
	result = dbmail_message_body_to_string(m);
	fail_unless(strlen(result)==1045, "dbmail_message_body_to_string failed");
	
        dbmail_message_free(m);
	g_string_free(s,TRUE);
	g_free(result);

	s = g_string_new(outlook_multipart);
	m = dbmail_message_new();
        m = dbmail_message_init_with_string(m,s);
	result = dbmail_message_body_to_string(m);
//	printf("{%d} [%s]\n", strlen(result), result);
	fail_unless(strlen(result)==329, "dbmail_message_body_to_string failed");
	
        dbmail_message_free(m);
	g_string_free(s,TRUE);
	g_free(result);

	
}
END_TEST

//size_t dbmail_message_get_rfcsize(struct DbmailMessage *self);

START_TEST(test_dbmail_message_get_rfcsize)
{
	unsigned result;
	GString *s;
	struct DbmailMessage *m;
	
	s = g_string_new(multipart_message);
	m = dbmail_message_new();
        m = dbmail_message_init_with_string(m,s);
	result = dbmail_message_get_rfcsize(m);
	
	fail_unless(result==1572, "dbmail_message_get_rfcsize failed");
	
	g_string_free(s,TRUE);
        dbmail_message_free(m);
}
END_TEST

//void dbmail_message_free(struct DbmailMessage *self);
START_TEST(test_dbmail_message_free)
{
	struct DbmailMessage *m = dbmail_message_new();
	dbmail_message_free(m);
}
END_TEST

START_TEST(test_dbmail_message_new_from_stream)
{
	FILE *fd;
	struct DbmailMessage *m;
	u64_t whole_message_size = 0;
	fd = tmpfile();
	fprintf(fd, "%s", multipart_message);
	fseek(fd,0,0);
	m = dbmail_message_new_from_stream(fd, DBMAIL_STREAM_PIPE);
	whole_message_size = dbmail_message_get_size(m, FALSE);
	fail_unless(whole_message_size == strlen(multipart_message), 
			"read_whole_message_stream returned wrong message_size");
	dbmail_message_free(m);
	
	fseek(fd,0,0);
	fprintf(fd, "%s", raw_lmtp_data);
	
	
	m = dbmail_message_new_from_stream(fd, DBMAIL_STREAM_LMTP);
	whole_message_size = dbmail_message_get_size(m, FALSE);
	// note: we're comparing with multipart_message not raw_lmtp_data because
	// multipart_message == raw_lmtp_data - crlf - end-dot
	fail_unless(whole_message_size == strlen(multipart_message), 
			"read_whole_message_network returned wrong message_size");
	dbmail_message_free(m);
}
END_TEST

START_TEST(test_dbmail_message_set_header)
{
	struct DbmailMessage *m = dbmail_message_new();
	m = dbmail_message_init_with_string(m, g_string_new(multipart_message));
	dbmail_message_set_header(m, "X-Foobar","Foo Bar");
	fail_unless(dbmail_message_get_header(m, "X-Foobar")!=NULL, "set_header failed");
	dbmail_message_free(m);
}
END_TEST

START_TEST(test_dbmail_message_get_header)
{
	char *t;
	struct DbmailMessage *h = dbmail_message_new();
	struct DbmailMessage *m = dbmail_message_new();
	
	
	m = dbmail_message_init_with_string(m, g_string_new(multipart_message));
	t = dbmail_message_hdrs_to_string(m);
	h = dbmail_message_init_with_string(h, g_string_new(t));
	g_free(t);
	
	fail_unless(dbmail_message_get_header(m, "X-Foobar")==NULL, "get_header failed on full message");
	fail_unless(strcmp(dbmail_message_get_header(m,"Subject"),"multipart/mixed")==0,"get_header failed on full message");

	fail_unless(dbmail_message_get_header(h, "X-Foobar")==NULL, "get_header failed on headers-only message");
	fail_unless(strcmp(dbmail_message_get_header(h,"Subject"),"multipart/mixed")==0,"get_header failed on headers-only message");
	
	dbmail_message_free(m);
	dbmail_message_free(h);

}
END_TEST

START_TEST(test_dbmail_message_encoded)
{
	struct DbmailMessage *m = dbmail_message_new();
	GString *s = g_string_new(encoded_message_koi);

	m = dbmail_message_init_with_string(m, s);
	g_string_free(s,TRUE);

	fail_unless(strcmp(dbmail_message_get_header(m,"From"),"=?koi8-r?Q?=E1=CE=D4=CF=CE=20=EE=C5=C8=CF=D2=CF=DB=C9=C8=20?=<bad@foo.ru>")==0, 
			"dbmail_message_get_header failed for koi-8 encoded header");
	dbmail_message_free(m);
}
END_TEST
START_TEST(test_dbmail_message_cache_headers)
{
	struct DbmailMessage *m = dbmail_message_new();
	char *s = g_new0(char,20);
	m = dbmail_message_init_with_string(m, g_string_new(multipart_message));
	dbmail_message_set_header(m, 
			"References", 
			"<20050326155326.1afb0377@ibook.linuks.mine.nu> <20050326181954.GB17389@khazad-dum.debian.net> <20050326193756.77747928@ibook.linuks.mine.nu> ");
	dbmail_message_store(m);
	dbmail_message_free(m);

	sprintf(s,"%.*s",10,"abcdefghijklmnopqrstuvwxyz");
	fail_unless(MATCH(s,"abcdefghij"),"string truncate failed");
	g_free(s);
}
END_TEST

START_TEST(test_dbmail_message_get_header_addresses)
{
	GList * result;
	struct DbmailMessage *m;

	m = dbmail_message_new();
	m = dbmail_message_init_with_string(m,g_string_new(multipart_message));
	
	result = dbmail_message_get_header_addresses(m, "Cc");
	result = g_list_first(result);

	fail_unless(result != NULL, "dbmail_message_get_header_addresses failed");
	fail_unless(g_list_length(result)==2,"dbmail_message_get_header_addresses failed");
	fail_unless(strcmp((char *)result->data,"vol@inter7.com")==0, "dbmail_message_get_header_addresses failed");

	g_list_foreach(result,(GFunc)g_free, NULL);
	g_list_free(result);
	dbmail_message_free(m);
}
END_TEST

START_TEST(test_dbmail_message_get_header_repeated)
{
	GTuples *headers;
	struct DbmailMessage *m;

	m = dbmail_message_new();
	m = dbmail_message_init_with_string(m,g_string_new(multipart_message));
	
	headers = dbmail_message_get_header_repeated(m, "Received");

	fail_unless(headers != NULL, "dbmail_message_get_header_repeated failed");
	fail_unless(headers->len==2, "dbmail_message_get_header_repeated failed");
	
	g_tuples_destroy(headers);

	headers = dbmail_message_get_header_repeated(m, "received");

	fail_unless(headers != NULL, "dbmail_message_get_header_repeated failed");
	fail_unless(headers->len==2, "dbmail_message_get_header_repeated failed");
	
	g_tuples_destroy(headers);

	dbmail_message_free(m);
}
END_TEST

Suite *dbmail_message_suite(void)
{
	Suite *s = suite_create("Dbmail Message");
	TCase *tc_message = tcase_create("Message");
	
	suite_add_tcase(s, tc_message);
	tcase_add_checked_fixture(tc_message, setup, teardown);
	
	tcase_add_test(tc_message, test_dbmail_message_new);
	tcase_add_test(tc_message, test_dbmail_message_new_from_stream);
	tcase_add_test(tc_message, test_dbmail_message_set_class);
	tcase_add_test(tc_message, test_dbmail_message_get_class);
	tcase_add_test(tc_message, test_dbmail_message_get_internal_date);
	tcase_add_test(tc_message, test_dbmail_message_retrieve);
	tcase_add_test(tc_message, test_dbmail_message_init_with_string);
	tcase_add_test(tc_message, test_dbmail_message_to_string);
//	tcase_add_test(tc_message, test_dbmail_message_init_with_stream);
	tcase_add_test(tc_message, test_dbmail_message_hdrs_to_string);
	tcase_add_test(tc_message, test_dbmail_message_body_to_string);
	tcase_add_test(tc_message, test_dbmail_message_get_rfcsize);
	tcase_add_test(tc_message, test_dbmail_message_set_header);
	tcase_add_test(tc_message, test_dbmail_message_set_header);
	tcase_add_test(tc_message, test_dbmail_message_get_header);
	tcase_add_test(tc_message, test_dbmail_message_cache_headers);
	tcase_add_test(tc_message, test_dbmail_message_free);
	tcase_add_test(tc_message, test_dbmail_message_encoded);
	tcase_add_test(tc_message, test_dbmail_message_get_header_addresses);
	tcase_add_test(tc_message, test_dbmail_message_get_header_repeated);
	return s;
}

int main(void)
{
	int nf;
	g_mime_init(0);
	Suite *s = dbmail_message_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	g_mime_shutdown();
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
	

