/*
 *   Copyright (c) 2005-2012 NFG Net Facilities Group BV support@nfg.nl
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

/*
 *
 * the test fixtures
 *
 */
static uint64_t get_mailbox_id(const char *name)
{
	uint64_t id, owner;
	auth_user_exists("testuser1",&owner);
	db_find_create_mailbox(name, BOX_COMMANDLINE, owner, &id);
	return id;
}

void setup(void)
{
	configure_debug(255,0);
	config_read(configFile);
	GetDBParams();
	db_connect();
	auth_connect();
}

void teardown(void)
{
	db_disconnect();
}

START_TEST(test_createdestroy)
{
	uint64_t id = get_mailbox_id("INBOX");
	MailboxState_T M = MailboxState_new(NULL, id);
	MailboxState_free(&M);
}
END_TEST

static void mailboxstate_destroy(MailboxState_T M)
{
	MailboxState_free(&M);
}


START_TEST(test_mbxinfo)
{
	MailboxState_T N, M;
	GTree *mbxinfo = g_tree_new_full((GCompareDataFunc)ucmpdata,NULL,(GDestroyNotify)g_free,(GDestroyNotify)mailboxstate_destroy);
	uint64_t *k1, *k2;
	uint64_t id = get_mailbox_id("INBOX");

	k1 = g_new0(uint64_t,1);
	k2 = g_new0(uint64_t,1);

	*k1 = id;
	*k2 = id;

	N = MailboxState_new(NULL, id);
	g_tree_replace(mbxinfo, k1, N);

	M = MailboxState_new(NULL, id);
	g_tree_replace(mbxinfo, k2, M);

	g_tree_destroy(mbxinfo);
}
END_TEST


Suite *dbmail_common_suite(void)
{
	Suite *s = suite_create("Dbmail MailboxState");
	TCase *tc_state = tcase_create("MailboxState");
	
	suite_add_tcase(s, tc_state);
	
	tcase_add_checked_fixture(tc_state, setup, teardown);
	tcase_add_test(tc_state, test_createdestroy);
	tcase_add_test(tc_state, test_mbxinfo);

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
	

