/*
 *   Copyright (c) 2005-2013 NFG Net Facilities Group BV support@nfg.nl
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
#include "dbmail.h"


extern char configFile[PATH_MAX];

/*
 *
 * the test fixtures
 *
 */

#define ABCD "ABCDEFGHIJKLMNOPQRSTUVWXYZ"

Mempool_T pool;

void setup(void)
{
	config_get_file();
	config_read(configFile);
	configure_debug(NULL,255,0);
	pool = mempool_open();
}

void teardown(void)
{
	mempool_close(&pool);
}


START_TEST(test_string_new)
{
	String_T S = p_string_new(pool, "");
	fail_unless(S != NULL);
	p_string_free(S, TRUE);
}
END_TEST

START_TEST(test_string_assign)
{
	int i=0;
	String_T S = p_string_new(pool, "");
	p_string_assign(S, "test");
	fail_unless(MATCH("test", p_string_str(S)));
	for (i=1; i<1000; i++) {
		if (i % 2) {
			p_string_assign(S, ABCD);
			fail_unless(MATCH(ABCD, p_string_str(S)), p_string_str(S));
		} else {
			p_string_assign(S, "test");
			fail_unless(MATCH("test", p_string_str(S)));
		}
	}
	p_string_free(S, TRUE);
}
END_TEST


START_TEST(test_string_printf)
{
	int i=0;
	String_T S = p_string_new(pool, "");
	p_string_printf(S, "test %s %s", "A", "B");
	fail_unless(MATCH("test A B", p_string_str(S)));
	for (i=0; i<1000; i++) {
		if (i % 2) {
			p_string_printf(S, "%s", ABCD);
			fail_unless(MATCH(ABCD, p_string_str(S)));
		} else {
			p_string_printf(S, "%s", "test");
			fail_unless(MATCH("test", p_string_str(S)));
		}
	}
	p_string_free(S, TRUE);
}
END_TEST


START_TEST(test_string_append_printf)
{
	int i=0;
	String_T S = p_string_new(pool, "A");
	p_string_append_printf(S, "%s", "B");
	fail_unless(MATCH("AB", p_string_str(S)), p_string_str(S));
	for (i=0; i<10000; i++) {
		p_string_append_printf(S, "%s TEST", ABCD);
	}
	p_string_free(S, TRUE);
	
	S = p_string_new(pool, "");
	p_string_append_printf(S, "%s", "");
	p_string_free(S, TRUE);
}
END_TEST

START_TEST(test_string_append_len)
{
	int i=0;
	String_T S = p_string_new(pool, "ABCDE");
	p_string_append_len(S, "QWERTY", 2);
	fail_unless(MATCH("ABCDEQW", p_string_str(S)));
	for (i=0; i<1000; i++) {
		p_string_append_len(S, ABCD, 26);
	}
	p_string_free(S, TRUE);
	S = p_string_new(pool, "");
	p_string_append_len(S, "$somelabel1)", 11);
	fail_unless(MATCH("$somelabel1", p_string_str(S)));
	p_string_free(S, TRUE);
}
END_TEST

START_TEST(test_string_truncate)
{
	String_T S = p_string_new(pool, "ABCDE");
	p_string_truncate(S, 2);
	fail_unless(MATCH("AB", p_string_str(S)));
	p_string_free(S, TRUE);
}
END_TEST

START_TEST(test_string_erase)
{
	String_T S = p_string_new(pool, ABCD);
	p_string_erase(S, 15, 11);
	fail_unless(MATCH("ABCDEFGHIJKLMNO",p_string_str(S)));
	p_string_erase(S, 0, 5);
	fail_unless(MATCH("FGHIJKLMNO",p_string_str(S)));
	p_string_free(S, TRUE);
}
END_TEST

START_TEST(test_string_unescape)
{
	String_T is = p_string_new(pool, "");
	char *in[] = {
		"",
		"test",
		"test'",
		"test '",
		"test \\",
		"test \\\"",
		"test \\\\",
		"test \\s \\\"",
		NULL
	};
	char *out[] = {
		"",
		"test",
		"test'",
		"test '",
		"test \\",
		"test \"",
		"test \\",
		"test \\s \"",
		NULL
	};

	int i=0;
	while (in[i]) {
		p_string_assign(is, in[i]); 
		p_string_unescape(is);
		fail_unless(MATCH(out[i], p_string_str(is)), "[%s] -> [%s] != [%s]",
			       	in[i], p_string_str(is), out[i]);
		i++;
	}
	p_string_free(is, TRUE);
}
END_TEST

START_TEST(test_string_free)
{
	String_T S = p_string_new(pool, "ABCDE");
	char *s = p_string_free(S, FALSE);
	fail_unless(MATCH("ABCDE", s));
	free(s);
}
END_TEST


Suite *dbmail_string_suite(void)
{
	Suite *s = suite_create("Dbmail String");
	TCase *tc = tcase_create("String");
	
	suite_add_tcase(s, tc);
	
	tcase_add_checked_fixture(tc, setup, teardown);
	tcase_add_test(tc, test_string_new);
	tcase_add_test(tc, test_string_assign);
	tcase_add_test(tc, test_string_printf);
	tcase_add_test(tc, test_string_append_printf);
	tcase_add_test(tc, test_string_append_len);
	tcase_add_test(tc, test_string_unescape);
	tcase_add_test(tc, test_string_erase);
	tcase_add_test(tc, test_string_truncate);
	tcase_add_test(tc, test_string_free);
	return s;
}

int main(void)
{
	int nf;
	Suite *s = dbmail_string_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
	

