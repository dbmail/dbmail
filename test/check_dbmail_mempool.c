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

extern char configFile[PATH_MAX];

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
}

void teardown(void)
{
	config_free();
}

START_TEST(test_mempool_new)
{
	Mempool_T M = mempool_open();
	fail_unless(M != NULL);
	mempool_close(&M);
}
END_TEST

START_TEST(test_mempool_pop)
{
	int i;
	Mempool_T M = mempool_open();
	for (i = 0; i < 1024; i++) {
		uint64_t *i = mempool_pop(M, sizeof(uint64_t));
		fail_unless(i != NULL);
		mempool_push(M, i, sizeof(uint64_t));
	}
	mempool_close(&M);
}
END_TEST

START_TEST(test_mempool_push)
{
	int i;
	struct test_data {
		void *data;
		char key[128];
		uint64_t id;
	};

	Mempool_T M = mempool_open();
	for (i = 0; i < 1024; i++) {
		uint64_t *i = mempool_pop(M, sizeof(uint64_t));
		fail_unless(i != NULL);
		mempool_push(M, i, sizeof(*i));
	}

	struct test_data *data;
	for (i = 0; i < 1024; i++) {
		data = mempool_pop(M, sizeof(*data));
		fail_unless(data != NULL);
		mempool_push(M, data, sizeof(*data));
	}
	mempool_close(&M);
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

