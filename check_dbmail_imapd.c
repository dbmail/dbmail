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

#include "dbmail-imapsession.h"
#include "dbmail-message.h"
#include "mime.h"

/* we need this one because we can't directly link imapd.o */
int imap_before_smtp = 0;

/* simple testmessage. */
char *raw_message = "From: someone@foobar.nl\n"
	"To: nobody@foobar.nl\n"
	"Subject: testing\n"
	"Received: at someserver from otherserver\n"
	"Received: at otherserver from localhost\n"
	"\n"
	"test body\n";
	
char *raw_message_part = "Content-Type: text/plain;\n"
	" name=\"mime_alternative\"\n"
	"Content-Transfer-Encoding: 7bit\n"
	"Content-Disposition: inline;\n"
	" filename=\"mime_alternative\"\n"
	"\n"
	"From: <vol@inter7.com>\n"
	"To: <vol@inter7.com>\n"
	"Subject: multipart/alternative\n"
	"MIME-Version: 1.0\n"
	"Content-type: multipart/alternative; boundary=\"boundary\"\n"
	"\n"
	"MIME alternative sample body\n"
	"(user never sees this portion of the message)\n"
	"\n"
	"These messages are used to send multiple versions of the same\n"
	"message in different formats.  User-agent will decide which\n"
	"to display.\n"
	"\n"
	"--boundary\n"
	"Content-type: text/html\n"
	"\n"
	"<HTML><HEAD><TITLE>HTML version</TITLE></HEAD><BODY>\n"
	"<CENTER>HTML version</CENTER>\n"
	"</BODY></HTML>\n"
	"--test\n"
	"Content-type: text/plain\n"
	"\n"
	"Text version\n"
	"--boundary--\n"
	"\n";
	
/*
 *
 * the test fixtures
 *
 */

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
	
		
}
END_TEST

START_TEST(test_mime_readheader)
{
	int res;
	u64_t blkidx=0, headersize=0;
	struct list mimelist;
	
	list_init(&mimelist);
	res = mime_readheader(raw_message,&blkidx,&mimelist,&headersize);
	fail_unless(res==6, "number of newlines incorrect");
	fail_unless(blkidx==144, "blkidx incorrect");
	fail_unless(headersize==blkidx+res, "headersize incorrect");
	fail_unless(mimelist.total_nodes==5, "number of mime-headers incorrect");
	list_freelist(&mimelist.start);
	
	blkidx = 0; headersize = 0;

	list_init(&mimelist);
	res = mime_readheader(raw_message_part, &blkidx, &mimelist, &headersize);
	fail_unless(res==6, "number of newlines incorrect");
	fail_unless(blkidx==142, "blkidx incorrect");
	fail_unless(headersize==blkidx+res, "headersize incorrect");
	fail_unless(mimelist.total_nodes==3, "number of mime-headers incorrect");
	list_freelist(&mimelist.start);
}
END_TEST

START_TEST(test_mime_fetch_headers)
{
	struct list mimelist;
	struct mime_record *mr;
	
	list_init(&mimelist);
	mime_fetch_headers(raw_message,&mimelist);
	fail_unless(mimelist.total_nodes==5, "number of mime-headers incorrect");
	mr = (mimelist.start)->data;
	fail_unless(strcmp(mr->field, "Received")==0, "Field name incorrect");
	fail_unless(strcmp(mr->value, "at otherserver from localhost")==0, "Field value incorrect");
	
	list_freelist(&mimelist.start);

	list_init(&mimelist);
	mime_fetch_headers(raw_message_part,&mimelist);
	fail_unless(mimelist.total_nodes==3, "number of mime-headers incorrect");
	mr = (mimelist.start)->data;
	fail_unless(strcmp(mr->field, "Content-Disposition")==0, "Field name incorrect");
	fail_unless(strcmp(mr->value, "inline; filename=\"mime_alternative\"")==0, "Field value incorrect");
	
	list_freelist(&mimelist.start);


}
END_TEST

START_TEST(test_dbmail_message)
{
	struct DbmailMessage *m;
	GTuples *t;
	m = dbmail_message_new();
	m = dbmail_message_init_with_string(m, g_string_new(raw_message));
	t = g_relation_select(m->headers, (gpointer)"Received", 0);
	fail_unless(t->len==2,"Too few headers in tuple");
}
END_TEST

START_TEST(test_dbmail_message_part)
{
	struct DbmailMessage *m;
	GTuples *t;
	m = dbmail_message_new();
	dbmail_message_set_class(m,DBMAIL_MESSAGE_PART);
	m = dbmail_message_init_with_string(m, g_string_new(raw_message_part));
}
END_TEST



Suite *dbmail_suite(void)
{
	Suite *s = suite_create("Dbmail");
	TCase *tc_session = tcase_create("ImapSession");
	TCase *tc_message = tcase_create("DbmailMessage");
	TCase *tc_mime = tcase_create("Mime");
	
	suite_add_tcase(s, tc_session);
	suite_add_tcase(s, tc_message);
	suite_add_tcase(s, tc_mime);
	
	tcase_add_test(tc_session, test_imap_session_new);
	tcase_add_test(tc_session, test_imap_bodyfetch);
	
	tcase_add_test(tc_message, test_dbmail_message);
	tcase_add_test(tc_message, test_dbmail_message_part);
	
	tcase_add_test(tc_mime, test_mime_readheader);
	tcase_add_test(tc_mime, test_mime_fetch_headers);
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
	suite_free(s);
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
	

