/*
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
extern DBParam_T db_params;


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

	
void setup(void)
{
	config_get_file();
	config_read(configFile);
	configure_debug(NULL,255,0);
	GetDBParams();
	db_connect();
}

void teardown(void)
{
	db_disconnect();
	config_free();
}

START_TEST(test_read_config)
{
	fail_unless(db_params.host != NULL, "db_host is NULL");
}
END_TEST

START_TEST(test_db_connect)
{
	int res;
	db_disconnect();
	res = db_connect();
	fail_unless(res==0, "Unable to connect to db");
}
END_TEST

START_TEST(test_glog)
{
	// this should not print anything on stdout/stderr
	g_log("test",G_LOG_LEVEL_CRITICAL, "this should not be printed");
}
END_TEST

Suite *dbmail_common_suite(void)
{
	Suite *s = suite_create("Dbmail Common");
	TCase *tc_config = tcase_create("Config");
	//TCase *tc_main = tcase_create("Main");
	
	suite_add_tcase(s, tc_config);
	//suite_add_tcase(s, tc_main);
	
	tcase_add_checked_fixture(tc_config, setup, teardown);
	tcase_add_test(tc_config, test_read_config);
	tcase_add_test(tc_config, test_db_connect);
	tcase_add_test(tc_config, test_glog);
	
	//tcase_add_checked_fixture(tc_main, setup, teardown);
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
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
	

