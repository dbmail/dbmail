/*
 *  Copyright (c) 2008-2012 NFG Net Facilities Group BV support@nfg.nl
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
 *   Basic unit-test framework for dbmail (www.dbmail.org)
 *
 *   See http://check.sf.net for details and docs.
 *
 *   Run 'make check' to see some action.
 *
 */ 

#include <check.h>
#include "check_dbmail.h"
#include "dm_mempool.h"

/*
 *
 * the test fixtures
 *
 */
void setup(void)
{
	configure_debug(255,0);
	config_read(configFile);
}

void teardown(void)
{
	config_free();
}

START_TEST(test_mempool_new)
{
	Mempool_T M = mempool_new(512, sizeof(uint64_t));
	fail_unless(M != NULL);
	mempool_free(&M);
}
END_TEST

START_TEST(test_mempool_pop)
{
	Mempool_T M = mempool_new(512, sizeof(uint64_t));
	for (int i = 0; i < 1024; i++) {
		uint64_t *i = mempool_pop(M);
		fail_unless(i != NULL);
		g_free(i);
	}
	mempool_free(&M);
}
END_TEST

START_TEST(test_mempool_push)
{
	struct test_data {
		void *data;
		char key[128];
		uint64_t id;
	};

	Mempool_T M = mempool_new(512, sizeof(uint64_t));
	for (int i = 0; i < 1024; i++) {
		uint64_t *i = mempool_pop(M);
		fail_unless(i != NULL);
		mempool_push(M, i);
	}
	mempool_free(&M);

	struct test_data *data;
	M = mempool_new(512, sizeof(*data));
	for (int i = 0; i < 1024; i++) {
		struct test_data *data = mempool_pop(M);
		fail_unless(data != NULL);
		mempool_push(M, data);
	}
	mempool_free(&M);
}
END_TEST



Suite *dbmail_mempool_suite(void)
{
	Suite *s = suite_create("Dbmail Mempool");
	TCase *tc_mempool = tcase_create("Mempool");
	
	suite_add_tcase(s, tc_mempool);
	
	tcase_add_checked_fixture(tc_mempool, setup, teardown);
	tcase_add_test(tc_mempool, test_mempool_new);
	tcase_add_test(tc_mempool, test_mempool_pop);
	tcase_add_test(tc_mempool, test_mempool_push);
	
	return s;
}

int main(void)
{
	int nf;
	Suite *s = dbmail_mempool_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

