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
#include "pipe.h"

#include "check_dbmail.h"

extern char *configFile;
extern db_param_t _db_params;


extern char *msgbuf_buf;
extern u64_t msgbuf_idx;
extern u64_t msgbuf_buflen;

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

Suite *dbmail_common_suite(void)
{
	Suite *s = suite_create("Dbmail Common");
	TCase *tc_config = tcase_create("Config");
	TCase *tc_main = tcase_create("Main");
	
	suite_add_tcase(s, tc_config);
	suite_add_tcase(s, tc_main);
	
	tcase_add_checked_fixture(tc_config, setup, teardown);
	tcase_add_test(tc_config, test_read_config);
	tcase_add_test(tc_config, test_db_connect);
	
	tcase_add_checked_fixture(tc_main, setup, teardown);
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
	

