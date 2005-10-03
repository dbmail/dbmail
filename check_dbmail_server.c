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
 *  $Id: check_dbmail_server.c 1891 2005-10-03 10:01:21Z paul $ 
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


/* we need this one because we can't directly link imapd.o */
int imap_before_smtp = 0;
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
	g_mime_init(0);
}

void teardown(void)
{
	db_disconnect();
	g_mime_shutdown();
}

START_TEST(test_scoreboard_new)
{
	serverConfig_t *config = g_new0(serverConfig_t,1);
	scoreboard_new(config);
}
END_TEST

#define X(a,b,c,d) fail_unless(dm_sock_compare((b),(c),(d))==(a),"dm_sock_compare failed")
#define Y(a,b,c) fail_unless(dm_sock_score((b),(c))==(a),"dm_sock_score failed")
START_TEST(test_dm_sock_compare) 
{
	X(1,"inet:127.0.0.1:143","inet:127.0.0.1:143","inet:127.0.0.1:143");
	X(1,"inet:127.0.0.1:110","inet:127.0.0.1:143","");
	X(1,"inet:127.0.0.1:143","inet:127.0.0.2:143","");
	X(1,"inet:127.0.0.1:143","","inet:0.0.0.0:0");
	X(0,"inet:127.0.0.1:143","inet:127.0.0.1:143","");
	X(0,"inet:127.0.0.1:143","inet:127.0.0.1/8:143","");
	X(0,"inet:127.0.0.1:143","inet:127.0.0.0/8:143","inet:10.0.0.0/8");
	X(0,"inet:127.0.0.3:143","inet:127.0.0.0/8:143","inet:127.0.0.1/32");

	X(0,"unix:/var/run/dbmail-imapd.sock","unix:/var/run/dbmail-imapd.sock","");
	X(0,"unix:/var/run/dbmail-imapd.sock","unix:/var/run/dbmail*","");
	X(1,"unix:/var/run/dbmail-imapd.sock","unix:/var/lib/dbmail-imapd.sock","");

	Y(32,"inet:10.1.1.1:110","");
	Y(0,"inet:10.1.1.1/16:110","inet:11.1.1.1:110");
	Y(8,"inet:10.1.1.1/8:110","inet:10.1.1.1:110");
	Y(16,"inet:10.1.1.1/16:110","inet:10.1.1.1:110");
	Y(32,"inet:10.1.1.1/32:110","inet:10.1.1.1:110");
	
}
END_TEST

Suite *dbmail_server_suite(void)
{
	Suite *s = suite_create("Dbmail Server");
	TCase *tc_pool = tcase_create("ServerPool");
	TCase *tc_server = tcase_create("Server");
	
	suite_add_tcase(s, tc_pool);
	suite_add_tcase(s, tc_server);
	
	tcase_add_checked_fixture(tc_pool, setup, teardown);
	tcase_add_test(tc_pool, test_scoreboard_new);
	
	tcase_add_checked_fixture(tc_server, setup, teardown);
	tcase_add_test(tc_server, test_dm_sock_compare);
	
	return s;
}

int main(void)
{
	int nf;
	Suite *s = dbmail_server_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
	

