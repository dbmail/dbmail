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

#include <stdlib.h>
#include <check.h>
#include <gmime/gmime.h>
#include <stdio.h>
#include <string.h>

#include "check_dbmail.h"
#include "debug.h"
#include "db.h"
#include "auth.h"
#include "dbmail-message.h"

extern char *configFile;
extern db_param_t _db_params;

extern char *raw_message;
extern char *raw_message_part;
extern char *raw_lmtp_data;

/*
 *
 * the test fixtures
 *
 */

	
void setup(void)
{
	configure_debug(5,1,0);
	config_read(configFile);
	GetDBParams(&_db_params);
	db_connect();
	auth_connect();
	g_mime_init(0);
}

void teardown(void)
{
	db_disconnect();
}

//struct DbmailMessage * dbmail_message_new(void);
START_TEST(test_dbmail_message_new)
{
	struct DbmailMessage *msg = dbmail_message_new();
	fail_unless(msg->id==0,"dbmail_message_new failed");
}
END_TEST
//void dbmail_message_set_class(struct DbmailMessage *self, int klass);
START_TEST(test_dbmail_message_set_class)
{
	int result;
	struct DbmailMessage *msg = dbmail_message_new();
	result = dbmail_message_set_class(msg,DBMAIL_MESSAGE);
	fail_unless(result==0,"dbmail_message_set_class failed");
	result = dbmail_message_set_class(msg,DBMAIL_MESSAGE_PART);
	fail_unless(result==0,"dbmail_message_set_class failed");
	result = dbmail_message_set_class(msg,10);
	fail_unless(result!=0,"dbmail_message_set_class failed");
}
END_TEST
//int dbmail_message_get_class(struct DbmailMessage *self);
START_TEST(test_dbmail_message_get_class)
{
	int klass;
	struct DbmailMessage *msg = dbmail_message_new();
	
	/* default */
	klass = dbmail_message_get_class(msg);
	fail_unless(klass==DBMAIL_MESSAGE,"dbmail_message_get_class failed");
	
	/* set explicitely */
	dbmail_message_set_class(msg,DBMAIL_MESSAGE_PART);
	klass = dbmail_message_get_class(msg);
	fail_unless(klass==DBMAIL_MESSAGE_PART,"dbmail_message_get_class failed");
	
	dbmail_message_set_class(msg,DBMAIL_MESSAGE);
	klass = dbmail_message_get_class(msg);
	fail_unless(klass==DBMAIL_MESSAGE,"dbmail_message_get_class failed");

}
END_TEST
//struct DbmailMessage * dbmail_message_retrieve(struct DbmailMessage *self, u64_t physid, int filter);
START_TEST(test_dbmail_message_retrieve)
{
	struct DbmailMessage *msg = dbmail_message_new();
	u64_t physid = 191967;
	msg = dbmail_message_retrieve(msg,physid,DBMAIL_MESSAGE_FILTER_HEAD);	
	fail_unless(msg != NULL, "dbmail_message_retrieve failed");
	fail_unless(msg->content != NULL, "dbmail_message_retrieve failed");
}
END_TEST
//struct DbmailMessage * dbmail_message_init_with_string(struct DbmailMessage *self, const GString *content);
START_TEST(test_dbmail_message_init_with_string)
{
	struct DbmailMessage *m;
	GTuples *t;
	m = dbmail_message_new();
	m = dbmail_message_init_with_string(m, g_string_new(raw_message));
	
	t = g_relation_select(m->headers, (gpointer)"Received", 0);
	fail_unless(t->len==2,"Too few headers in tuple");
	
	dbmail_message_set_class(m,DBMAIL_MESSAGE_PART);
	m = dbmail_message_init_with_string(m, g_string_new(raw_message_part));
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
	struct DbmailMessage *m = dbmail_message_new();
	m = dbmail_message_init_with_string(m, g_string_new(raw_message));
	result = dbmail_message_hdrs_to_string(m);
	fail_unless(strlen(result)==235, "dbmail_message_hdrs_to_string failed");
}
END_TEST

//gchar * dbmail_message_body_to_string(struct DbmailMessage *self);

START_TEST(test_dbmail_message_body_to_string)
{
	char *result;
	struct DbmailMessage *m = dbmail_message_new();
	m = dbmail_message_init_with_string(m, g_string_new(raw_message));
	result = dbmail_message_body_to_string(m);
	fail_unless(strlen(result)==1046, "dbmail_message_body_to_string failed");
}
END_TEST

//size_t dbmail_message_get_rfcsize(struct DbmailMessage *self);

START_TEST(test_dbmail_message_get_rfcsize)
{
	int result;
	struct DbmailMessage *m = dbmail_message_new();
	m = dbmail_message_init_with_string(m, g_string_new(raw_message));
	result = dbmail_message_get_rfcsize(m);
	fail_unless(result==1319, "dbmail_message_get_rfcsize failed");
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
	struct DbmailMessage *msg;
	u64_t whole_message_size = 0;
	fd = tmpfile();
	fprintf(fd, "%s", raw_message);
	fseek(fd,0,0);
	msg = dbmail_message_new_from_stream(fd, DBMAIL_STREAM_PIPE);
	whole_message_size = dbmail_message_get_size(msg);
	fail_unless(whole_message_size == strlen(raw_message), 
			"read_whole_message_stream returned wrong message_size");
	
	fseek(fd,0,0);
	fprintf(fd, "%s", raw_lmtp_data);
	
	msg = dbmail_message_new_from_stream(fd, DBMAIL_STREAM_LMTP);
	whole_message_size = dbmail_message_get_size(msg);
	// note: we're comparing with raw_message not raw_lmtp_data because
	// raw_message == raw_lmtp_data - crlf - end-dot
	fail_unless(whole_message_size == strlen(raw_message), 
			"read_whole_message_network returned wrong message_size");
}
END_TEST

START_TEST(test_dbmail_message_set_header)
{
	struct DbmailMessage *m = dbmail_message_new();
	m = dbmail_message_init_with_string(m, g_string_new(raw_message));
	dbmail_message_set_header(m, "X-Foobar","Foo Bar");
	fail_unless(dbmail_message_get_header(m, "X-Foobar")!=NULL, "set_header failed");
}
END_TEST

START_TEST(test_dbmail_message_get_header)
{
	struct DbmailMessage *h = dbmail_message_new();
	struct DbmailMessage *m = dbmail_message_new();
	
	m = dbmail_message_init_with_string(m, g_string_new(raw_message));
	h = dbmail_message_init_with_string(h, g_string_new(dbmail_message_hdrs_to_string(m)));
	
	fail_unless(dbmail_message_get_header(m, "X-Foobar")==NULL, "get_header failed on full message");
	fail_unless(strcmp(dbmail_message_get_header(m,"Subject"),"multipart/mixed")==0,"get_header failed on full message");

	fail_unless(dbmail_message_get_header(h, "X-Foobar")==NULL, "get_header failed on headers-only message");
	fail_unless(strcmp(dbmail_message_get_header(h,"Subject"),"multipart/mixed")==0,"get_header failed on headers-only message");

}
END_TEST

START_TEST(test_dbmail_message_cache_headers)
{
	struct DbmailMessage *m = dbmail_message_new();
	m = dbmail_message_init_with_string(m, g_string_new(raw_message));
	dbmail_message_set_header(m, 
			"References", 
			"<20050326155326.1afb0377@ibook.linuks.mine.nu> <20050326181954.GB17389@khazad-dum.debian.net> <20050326193756.77747928@ibook.linuks.mine.nu> ");
	dbmail_message_store(m);
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
//	tcase_add_test(tc_message, test_dbmail_message_retrieve);
	tcase_add_test(tc_message, test_dbmail_message_init_with_string);
//	tcase_add_test(tc_message, test_dbmail_message_init_with_stream);
	tcase_add_test(tc_message, test_dbmail_message_hdrs_to_string);
	tcase_add_test(tc_message, test_dbmail_message_body_to_string);
	tcase_add_test(tc_message, test_dbmail_message_get_rfcsize);
	tcase_add_test(tc_message, test_dbmail_message_set_header);
	tcase_add_test(tc_message, test_dbmail_message_set_header);
	tcase_add_test(tc_message, test_dbmail_message_get_header);
	
	tcase_add_test(tc_message, test_dbmail_message_cache_headers);

	tcase_add_test(tc_message, test_dbmail_message_free);
	return s;
}

int main(void)
{
	int nf;
	Suite *s = dbmail_message_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
	

