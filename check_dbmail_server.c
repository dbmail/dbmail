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
#include <stdio.h>
#include <string.h>

#include "dbmail.h"
#include "debug.h"
#include "db.h"
#include "pool.h"
#include "server.h"
#include "check.h"

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
}

void teardown(void)
{
	db_disconnect();
}

START_TEST(test_scoreboard_new)
{
	serverConfig_t *config = g_new0(serverConfig_t,1);
	scoreboard_new(config);
}
END_TEST


Suite *dbmail_server_suite(void)
{
	Suite *s = suite_create("Dbmail Server");
	TCase *tc_pool = tcase_create("ServerPool");
	
	suite_add_tcase(s, tc_pool);
	
	tcase_add_checked_fixture(tc_pool, setup, teardown);
	tcase_add_test(tc_pool, test_scoreboard_new);
	
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
	suite_free(s);
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
	

