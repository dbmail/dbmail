/*
 *   Copyright (C) 2006  Aaron Stone  <aaron@serendipity.cx>
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
extern int quiet;
extern int reallyquiet;

/*
 *
 * the test fixtures
 *
 */
Mempool_T pool;

void setup(void)
{
	//reallyquiet = 1;
	config_get_file();
	config_read(configFile);
	configure_debug(NULL,255,0);
	pool = mempool_open();
}

void teardown(void)
{
	mempool_close(&pool);
}


START_TEST(test_dbmail_list_dedup) 
{
	GList *list = NULL;
	GList *concat = NULL;

	/* Have to strdup these because dedup frees them. */
	list = g_list_prepend(list, strdup("foo"));
	list = g_list_prepend(list, strdup("foo"));
	list = g_list_prepend(list, strdup("foo"));
	list = g_list_prepend(list, strdup("foo"));
	list = g_list_prepend(list, strdup("bar"));
	list = g_list_prepend(list, strdup("bar"));
	list = g_list_prepend(list, strdup("baz"));
	list = g_list_prepend(list, strdup("qux"));
	list = g_list_prepend(list, strdup("qux"));

	concat = g_list_slices(list, 100);

	fail_unless(strcmp("qux,qux,baz,bar,bar,foo,foo,foo,foo", concat->data) == 0,
		"Failed to concatenate list for pre-condition");

	g_list_destroy(concat);

	list = g_list_dedup(list, (GCompareFunc)strcmp, TRUE);
	concat = g_list_slices(list, 100);

	fail_unless(strcmp("qux,baz,bar,foo", concat->data) == 0,
		"Failed to concatenate list for pre-condition");

	g_list_destroy(concat);
	g_list_destroy(list);
}
END_TEST

START_TEST(test_p_list_length)
{
	const char *data = "data";
	size_t length;
	List_T L = p_list_new(pool);
	length = p_list_length(L);

	fail_unless(length == 0);
	L = p_list_append(L, (void *)data);
	L = p_list_first(L);

	length = p_list_length(L);
	fail_unless(length == 1);

	L = p_list_prepend(L, (void *)data);
	L = p_list_first(L);

	length = p_list_length(L);
	fail_unless(length == 2);
	
	p_list_free(&L);
}
END_TEST


START_TEST(test_p_list_newfree)
{
	int i;
	const char *data = "data";
	List_T L = p_list_new(pool);
	fail_unless(L != NULL);
	p_list_free(&L);
	L = p_list_new(pool);
	for (i=0; i<100; i++)
		L = p_list_prepend(L, (void *)data);
	L = p_list_last(L);
	for (i=0; i<100; i++)
		L = p_list_append(L, (void *)data);
	L = p_list_first(L);
	p_list_free(&L);
}
END_TEST

START_TEST(test_p_list_append)
{
	List_T N, L = p_list_new(pool);
	String_T S = p_string_new(pool, "A");

	N = p_list_append(L, S);
	fail_unless(N != NULL);
	fail_unless(N == L);
	S = p_list_data(N);
	fail_unless(MATCH(p_string_str(S),"A"));

	N = p_list_append(L, S);
	fail_unless(N != NULL);
	fail_unless(N != L);
	S = p_list_data(N);
	fail_unless(MATCH(p_string_str(S),"A"));
}
END_TEST

START_TEST(test_p_list_prepend)
{
	List_T N, L = p_list_new(pool);
	String_T S = p_string_new(pool, "B");

	N = p_list_prepend(L, S);
	fail_unless(N != NULL);
	fail_unless(N == L);
	S = p_list_data(N);
	fail_unless(MATCH(p_string_str(S),"B"));

	N = p_list_prepend(L, S);
	fail_unless(N != NULL);
	fail_unless(N != L);
	S = p_list_data(N);
	fail_unless(MATCH(p_string_str(S),"B"));
}
END_TEST

START_TEST(test_p_list_last)
{
	List_T N, L = p_list_new(pool);
	String_T S;
	int i=0;
	N = L;
	for(i=0; i<100; i++) {
		S = p_string_new(pool, "");
		N = p_list_append(N, S);
	}
	fail_unless(N != L);
	L = p_list_last(L);
	fail_unless(N == L);
}
END_TEST

START_TEST(test_p_list_first)
{
	List_T N, L = p_list_new(pool);
	String_T S;
	int i=0;
	N = L;
	for(i=0; i<100; i++) {
		S = p_string_new(pool, "");
		N = p_list_prepend(N, S);
	}
	fail_unless(N != L);
	L = p_list_first(L);
	fail_unless(N == L);
}
END_TEST

START_TEST(test_p_list_previous)
{
	const char *data = "data";

	List_T N, L = p_list_new(pool);
	N = p_list_previous(L);
	fail_unless(N == NULL);

	N = p_list_prepend(L, (void *)data);
	fail_unless(N != NULL);
	fail_unless(N == L);

	L = p_list_previous(L);
	fail_unless(L == NULL);
}
END_TEST

START_TEST(test_p_list_next)
{
	const char *data = "data";
	List_T N, L = p_list_new(pool);
	N = p_list_next(L);
	fail_unless(N == NULL);

	N = p_list_append(L, (void *)data);
	fail_unless(N != NULL);
	fail_unless(L == N);

	L = p_list_next(L);
	fail_unless(L == NULL);
}
END_TEST

START_TEST(test_p_list_data)
{
	List_T L = p_list_new(pool);
	String_T N, S = p_string_new(pool, "");
	L = p_list_append(L, S);
	N = p_list_data(L);
	fail_unless(N == S);
}
END_TEST

START_TEST(test_p_list_remove)
{
	List_T E, N, L = p_list_new(pool);
	String_T S;
	int i=0;
	N = L;
	for(i=0; i<100; i++) {
		S = p_string_new(pool, "");
		N = p_list_prepend(N, S);
	}
	E = p_list_first(L);
	N = p_list_remove(L, E);
	fail_unless(E != N);
	E = p_list_last(L);
	N = p_list_remove(L, E);
	fail_unless(E != N);
}
END_TEST

Suite *dbmail_list_suite(void)
{
	Suite *s = suite_create("Dbmail List");
	TCase *tc_list = tcase_create("List");
	
	suite_add_tcase(s, tc_list);
	
	tcase_add_checked_fixture(tc_list, setup, teardown);
	tcase_add_test(tc_list, test_dbmail_list_dedup);
	tcase_add_test(tc_list, test_p_list_newfree);
	tcase_add_test(tc_list, test_p_list_append);
	tcase_add_test(tc_list, test_p_list_prepend);
	tcase_add_test(tc_list, test_p_list_last);
	tcase_add_test(tc_list, test_p_list_first);
	tcase_add_test(tc_list, test_p_list_previous);
	tcase_add_test(tc_list, test_p_list_next);
	tcase_add_test(tc_list, test_p_list_remove);
	tcase_add_test(tc_list, test_p_list_length);
	tcase_add_test(tc_list, test_p_list_data);

	
	return s;
}

int main(void)
{
	int nf;
	Suite *s = dbmail_list_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
