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
#include "rfcmsg.h"
#include "dbmsgbuf.h"
#include "imaputil.h"
#include "config.h"

char *configFile = DEFAULT_CONFIG_FILE;
extern db_param_t _db_params;


/* we need this one because we can't directly link imapd.o */
int imap_before_smtp = 0;
extern char *msgbuf_buf;
extern u64_t msgbuf_idx;
extern u64_t msgbuf_buflen;

/* simple testmessage. */
char *raw_message = "From: <vol@inter7.com>\n"
	"To: <vol@inter7.com>\n"
	"Subject: multipart/mixed\n"
	"Received: at mx.inter7.com from localhost\n"
	"Received: at localhost from localhost\n"
	"MIME-Version: 1.0\n"
	"Content-type: multipart/mixed; boundary=\"boundary\"\n"
	"X-Dbmail-ID: 12345\n"
	"\n"
	"MIME multipart messages specify that there are multiple\n"
	"messages of possibly different types included in the\n"
	"message.  All peices will be availble by the user-agent\n"
	"if possible.\n"
	"\n"
	"The header 'Content-disposition: inline' states that\n"
	"if possible, the user-agent should display the contents\n"
	"of the attachment as part of the email, rather than as\n"
	"a file, or message attachment.\n"
	"\n"
	"(This message will not be seen by the user)\n"
	"\n"
	"--boundary\n"
	"Content-type: text/html\n"
	"Content-disposition: inline\n"
	"\n"
	"Test message one\n"
	"--boundary\n"
	"Content-type: text/plain; charset=us-ascii; name=\"testfile\"\n"
	"Content-transfer-encoding: base64\n"
	"\n"
	"IyEvYmluL2Jhc2gNCg0KY2xlYXINCmVjaG8gIi4tLS0tLS0tLS0tLS0tLS0t\n"
	"LS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS0tLS4i\n"
	"DQplY2hvICJ8IE1hcmNoZXcuSHlwZXJyZWFsIHByZXNlbnRzOiB2aXhpZSBj\n"
	"cm9udGFiIGV4cGxvaXQgIzcyODM3MSB8Ig0KZWNobyAifD09PT09PT09PT09\n"
	"PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09PT09\n"
	"PT09fCINCmVjaG8gInwgU2ViYXN0aWFuIEtyYWhtZXIgPGtyYWhtZXJAc2Vj\n"
	"dXJpdHkuaXM+ICAgICAgICAgICAgICAgICAgIHwiDQplY2hvICJ8IE1pY2hh\n"
	"--boundary--\n";

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

	
void setup(void)
{
	configure_debug(4,0,1);
	config_read(configFile);
	GetDBParams(&_db_params);
	db_connect();
}

void teardown(void)
{
	db_disconnect();
}

START_TEST(test_read_config)
{
	int res;
	res = config_read(configFile);
	fail_unless(res==0, "Unable to read configFile");
	GetDBParams(&_db_params);
	fail_unless(_db_params.host != NULL, "db_host is NULL");
}
END_TEST

START_TEST(test_db_connect)
{
	int res;
	config_read(configFile);
	GetDBParams(&_db_params);
	res = db_connect();
	fail_unless(res==0, "Unable to connect to db");
}
END_TEST

START_TEST(test_auth_get_known_users)
{
	GList *users = auth_get_known_users();
	fail_unless(users != NULL,"Unable to get known users");
	fail_unless(g_list_length(users) >= 1, "Usercount too low");
}
END_TEST
	
Suite *dbmail_common_suite(void)
{
	Suite *s = suite_create("Dbmail Common");
	TCase *tc_config = tcase_create("Config");
	
	suite_add_tcase(s, tc_config);
	
	tcase_add_checked_fixture(tc_config, setup, teardown);
	tcase_add_test(tc_config, test_read_config);
	tcase_add_test(tc_config, test_db_connect);
	tcase_add_test(tc_config, test_auth_get_known_users);
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
	suite_free(s);
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
	

