/*
 *  Copyright (C) 2006  Aaron Stone  <aaron@serendipity.cx>
 *  Copyright (c) 2005-2012 NFG Net Facilities Group BV support@nfg.nl
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

extern char *configFile;
extern int quiet;
extern int reallyquiet;

/*
 *
 * the test fixtures
 *
 */
void setup(void)
{
	reallyquiet = 1;
	configure_debug(255,0);
	config_read(configFile);
	GetDBParams();
//	db_connect();
//	auth_connect();
//	g_mime_init(0);
}

void teardown(void)
{
//	db_disconnect();
//	auth_disconnect();
//	g_mime_shutdown();
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

Suite *dbmail_list_suite(void)
{
	Suite *s = suite_create("Dbmail List");
	TCase *tc_list = tcase_create("List");
	
	suite_add_tcase(s, tc_list);
	
	tcase_add_checked_fixture(tc_list, setup, teardown);
	tcase_add_test(tc_list, test_dbmail_list_dedup);
	
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
