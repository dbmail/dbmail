/*
 *   Copyright (c) 2005-2006 NFG Net Facilities Group BV support@nfg.nl
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
 *  $Id: check_dbmail_common.c 1598 2005-02-23 08:41:02Z paul $ 
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
}
START_TEST(test_allocate)
{
	u64_t i = 200000;
	GList *l = NULL;
	u64_t *id;
	u64_t *rows = g_new0(u64_t, i);
	g_free(rows);

	while (i-- > 0) {
		id = g_new0(u64_t, 1);
		*id = i;
		l = g_list_prepend(l, id);
	}
	g_list_foreach(l, (GFunc)g_free, NULL);
	g_list_free(l);
}
END_TEST

START_TEST(test_db_icheck_envelope)
{
	GList *lost = NULL;
	fail_unless(0==db_icheck_envelope(&lost),"db_icheck_envelope failed");
}
END_TEST


Suite *dbmail_common_suite(void)
{
	Suite *s = suite_create("Dbmail Util");
	TCase *tc_util = tcase_create("Util");
	
	suite_add_tcase(s, tc_util);
	
	tcase_add_checked_fixture(tc_util, setup, teardown);
	tcase_add_test(tc_util, test_allocate);
	tcase_add_test(tc_util, test_db_icheck_envelope); 

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
	

