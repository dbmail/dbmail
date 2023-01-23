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

/*
 *
 * the test fixtures
 *
 */
Capa_T A;
Mempool_T pool;

void setup(void)
{
	config_get_file();
	config_read(configFile);
	configure_debug(NULL,255,31);
	pool = mempool_open();
	A = Capa_new(pool);
}

void teardown(void)
{
	Capa_free(&A);
	mempool_close(&pool);
}

START_TEST(test_capa_new)
{
	fail_unless(MATCH(Capa_as_string(A), IMAP_CAPABILITY_STRING), "mismatch:\n[%s] !=\n[%s]", Capa_as_string(A), IMAP_CAPABILITY_STRING);
}
END_TEST

START_TEST(test_capa_match)
{
	fail_unless(Capa_match(A, "AUTH=CRAM-MD5"), "match failed");
	fail_unless(! Capa_match(A, "AUTH=MD5"), "match failed");
}
END_TEST

START_TEST(test_capa_add)
{
	char *ex1 = "IMAP4rev1 AUTH=LOGIN AUTH=CRAM-MD5 ACL RIGHTS=texk NAMESPACE CHILDREN SORT QUOTA THREAD=ORDEREDSUBJECT UNSELECT IDLE STARTTLS UIDPLUS WITHIN LOGINDISABLED CONDSTORE LITERAL+ ENABLE QRESYNC";
	char *ex2 = "IMAP4rev1 AUTH=LOGIN AUTH=CRAM-MD5 ACL RIGHTS=texk NAMESPACE CHILDREN SORT QUOTA THREAD=ORDEREDSUBJECT UNSELECT IDLE STARTTLS UIDPLUS WITHIN LOGINDISABLED CONDSTORE LITERAL+ ENABLE QRESYNC ID";
	Capa_remove(A, "ID");
	fail_unless(! Capa_match(A, "ID"), "remove failed\n[%s] !=\n[%s]\n", ex1, Capa_as_string(A));
	fail_unless(MATCH(Capa_as_string(A), ex1), "remove failed\n[%s] !=\n[%s]\n", ex1, Capa_as_string(A));
	Capa_add(A, "ID");
	fail_unless(Capa_match(A, "ID"), "add failed\n[%s] !=\n[%s]\n", ex2, Capa_as_string(A));
	fail_unless(MATCH(Capa_as_string(A), ex2), "add failed\n[%s] !=\n[%s]\n", ex2, Capa_as_string(A));
}
END_TEST

START_TEST(test_capa_remove)
{
	char *ex1 = "IMAP4rev1 AUTH=LOGIN AUTH=CRAM-MD5 ACL RIGHTS=texk SORT THREAD=ORDEREDSUBJECT UNSELECT IDLE ID UIDPLUS WITHIN LOGINDISABLED CONDSTORE LITERAL+ ENABLE QRESYNC";
	Capa_remove(A, "STARTTLS");
	fail_unless(! Capa_match(A, "STARTTLS"), "remove failed");
	Capa_remove(A, "NAMESPACE");
	Capa_remove(A, "QUOTA");
	Capa_remove(A, "CHILDREN");
	fail_unless(MATCH(Capa_as_string(A), ex1), "remove failed\n[%s] !=\n[%s]\n", ex1, Capa_as_string(A));
}
END_TEST

Suite *dbmail_capa_suite(void)
{
	Suite *s = suite_create("Dbmail Capa");
	TCase *tc_capa = tcase_create("Capa");
	
	suite_add_tcase(s, tc_capa);
	
	tcase_add_checked_fixture(tc_capa, setup, teardown);
	tcase_add_test(tc_capa, test_capa_new);
	tcase_add_test(tc_capa, test_capa_match);
	tcase_add_test(tc_capa, test_capa_add);
	tcase_add_test(tc_capa, test_capa_remove);

	return s;
}

int main(void)
{
	int nf;
	Suite *s = dbmail_capa_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
	

