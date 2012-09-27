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
#include "dm_cache.h"

extern char *configFile;
extern char *multipart_message;

static Cache_T Cache = NULL;

static DbmailMessage  * message_init(const char *message)
{
	GString *s;
	DbmailMessage *m;

	s = g_string_new(message);
	m = dbmail_message_new();
	m = dbmail_message_init_with_string(m, s);
	dbmail_message_store(m);
	g_string_free(s,TRUE);

	fail_unless(m != NULL, "dbmail_message_init_with_string failed");

	return m;
}

/*
 *
 * the test fixtures
 *
 */
void setup(void)
{
	configure_debug(255,0);
	config_read(configFile);
	GetDBParams();
	db_connect();
	auth_connect();
	Cache = Cache_new();
}

void teardown(void)
{
	db_disconnect();
	auth_disconnect();
	config_free();
	Cache_free(&Cache);
	Cache = NULL;
}

START_TEST(test_cache_new)
{
	fail_unless(Cache != NULL);
}
END_TEST

START_TEST(test_cache_update)
{
	uint64_t id1, id2;
	DbmailMessage *m;
        m = message_init(multipart_message);
	id1 = Cache_update(Cache, m);
	fail_unless(id1 > 0);
	id2 = Cache_update(Cache, m);
	fail_unless(id1 == id2);
	dbmail_message_free(m);
}
END_TEST

START_TEST(test_cache_clear)
{
	uint64_t size;
	DbmailMessage *m;
        m = message_init(multipart_message);
	size = Cache_update(Cache, m);
	fail_unless(size > 0);
	Cache_clear(Cache, m->id);
	dbmail_message_free(m);
}
END_TEST

START_TEST(test_cache_get_size)
{
	uint64_t size1, size2;
	DbmailMessage *m;
        m = message_init(multipart_message);
	size1 = Cache_update(Cache, m);
	fail_unless(size1 > 0);
	size2 = Cache_get_size(Cache, m->id);
	fail_unless(size1 == size2);
	dbmail_message_free(m);
}
END_TEST

START_TEST(test_cache_get_mem)
{
	Stream_T M;
	DbmailMessage *m;
        m = message_init(multipart_message);
	M = Stream_new();
	Cache_update(Cache, m);
	Cache_get_mem(Cache, m->id, M);
	Cache_unref_mem(Cache, m->id, &M);
	dbmail_message_free(m);
}
END_TEST

Suite *dbmail_cache_suite(void)
{
	Suite *s = suite_create("Dbmail Cache");
	TCase *tc_cache = tcase_create("Cache");
	
	suite_add_tcase(s, tc_cache);
	
	tcase_add_checked_fixture(tc_cache, setup, teardown);
	tcase_add_test(tc_cache, test_cache_new);
	tcase_add_test(tc_cache, test_cache_update);
	tcase_add_test(tc_cache, test_cache_clear);
	tcase_add_test(tc_cache, test_cache_get_size);
	tcase_add_test(tc_cache, test_cache_get_mem);
	
	return s;
}

int main(void)
{
	int nf;
	g_mime_init(0);
	Suite *s = dbmail_cache_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	g_mime_shutdown();
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

