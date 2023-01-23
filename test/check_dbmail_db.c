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

// Test dsnuser_resolve through all of its pathways.
#include <check.h>
#include "check_dbmail.h"

extern char configFile[PATH_MAX];
extern int quiet;
extern int reallyquiet;

uint64_t useridnr = 0;
uint64_t useridnr_domain = 0;

char *testuser = "testuser1";
uint64_t testidnr = 0;

char *username = "testfaildsn";
char *username_domain = "testfailuser@nonexistantdomain";
char *username_mailbox = "testfailuser+foomailbox@nonexistantdomain";

char *alias = "testfailalias@nonexistantdomain";
char *alias_mailbox = "testfailalias+foomailbox@nonexistantdomain";

char *userpart_catchall = "testfailcatchall@";
char *domain_catchall = "@nonexistantdomain";

/*
 *
 * the test fixtures
 *
 */
void setup(void)
{
	config_get_file();
	config_read(configFile);
	configure_debug(NULL,511,0);
	GetDBParams();
	db_connect();
	auth_connect();

	GList *alias_add = NULL;
	alias_add = g_list_append(alias_add, alias);
	alias_add = g_list_append(alias_add, userpart_catchall);
	alias_add = g_list_append(alias_add, domain_catchall);

	if (! (auth_user_exists(testuser, &testidnr))) {
		do_add(testuser, "test", "md5-hash", 0, 0, NULL, NULL);
		auth_user_exists(testuser, &testidnr);
	}
	if (! (auth_user_exists(username, &useridnr))) {
		do_add(username, "testpass", "md5-hash", 0, 0, alias_add, NULL);
		auth_user_exists(username, &useridnr);
	}
	if (! (auth_user_exists(username_domain, &useridnr_domain))) {
		do_add(username_domain, "testpass", "md5-hash", 0, 0, NULL, NULL);
		auth_user_exists(username_domain, &useridnr_domain);
	}

}

void teardown(void)
{
	uint64_t mailbox_id=0;
	if (db_findmailbox("INBOX/Trash",testidnr,&mailbox_id))
		db_delete_mailbox(mailbox_id,0,0);
			
	if (db_findmailbox("testcreatebox",testidnr,&mailbox_id))
		db_delete_mailbox(mailbox_id,0,0);

	if (db_findmailbox("testdeletebox",testidnr,&mailbox_id))
		db_delete_mailbox(mailbox_id,0,0);

	if (db_findmailbox("testpermissionbox",testidnr,&mailbox_id)) {
		db_mailbox_set_permission(mailbox_id, IMAPPERM_READWRITE);
		db_delete_mailbox(mailbox_id,0,0);
	}

	db_disconnect();
	auth_disconnect();
}

START_TEST(test_db_stmt_prepare)
{
	Connection_T c; PreparedStatement_T s;
	c = db_con_get();
	s = db_stmt_prepare(c, "SELECT 1=1");
	fail_unless(s != NULL, "db_stmt_prepare failed");
	db_con_close(c);

}
END_TEST

START_TEST(test_db_stmt_set_str)
{
	Connection_T c; PreparedStatement_T s; ResultSet_T r;
	c = db_con_get();
	s = db_stmt_prepare(c, "select user_idnr from dbmail_users where userid=?");
	fail_unless(s != NULL, "db_stmt_prepare failed");
	fail_unless(db_stmt_set_str(s, 1, "testuser1"), "db_stmt_set_str failed");
	
	r = db_stmt_query(s);
	fail_unless(r != NULL, "db_stmt_query failed");
	fail_unless(db_result_next(r), "db_result_next failed");
	db_con_close(c);

}
END_TEST

START_TEST(test_Connection_executeQuery)
{
	Connection_T c; ResultSet_T r = NULL;

	c = db_con_get();
	TRY
		r = db_query(c, "SELECT foo FROM bar");
	CATCH(SQLException)
		LOG_SQLERROR;
	END_TRY;
	fail_unless(r==NULL, "Connection_executeQuery should have failed");

	r = db_query(c, "SELECT 1=1");
	fail_unless(r!=NULL, "Connection_executeQuery should have succeeded");
	fail_unless(db_result_next(r), "db_result_next failed");
	fail_unless(strlen(db_result_get(r,0)), "db_result_get failed");
}
END_TEST
START_TEST(test_db_mailbox_set_permission)
{

	int result;
	uint64_t mailbox_id;
	result = db_find_create_mailbox("testpermissionbox", BOX_DEFAULT, testidnr, &mailbox_id);
	fail_unless(mailbox_id, "db_find_create_mailbox [testpermissionbox] owned by [%" PRIu64 "] failed [%" PRIu64 "].", testidnr, mailbox_id);

	result = db_mailbox_set_permission(mailbox_id, IMAPPERM_READ);
	fail_unless(result==TRUE,"db_mailbox_set_permission failed");

	result = db_delete_mailbox(mailbox_id,0,0);
	fail_unless(result != DM_SUCCESS,"db_delete_mailbox should have failed on readonly mailbox");

	result = db_mailbox_set_permission(mailbox_id, IMAPPERM_READWRITE);
	fail_unless(result==TRUE,"db_mailbox_set_permission failed");

	result = db_delete_mailbox(mailbox_id,0,0);
	fail_unless(result == DM_SUCCESS,"db_delete_mailbox should have succeeded on readwrite mailbox");


	return;
}
END_TEST

/**
 * Produces a regexp that will case-insensitively match the mailbox name
 * according to the modified UTF-7 rules given in section 5.1.3 of IMAP.
 * \param column name of the name column.
 * \param mailbox name of the mailbox.
 * \param filter use /% for children or "" for just the box.
 * \return pointer to a newly allocated string.
 */
START_TEST(test_mailbox_match_new)
{
	char *trythese[] = {
		"Inbox",
		"Listen/F% %/Users",
	/*	"xx1_&AOcA4w-o", */
	/*	"INBOX/&BekF3AXVBd0-", */
		NULL };
	char *getthese[] = {
		NULL, "Inbox",
		NULL, "Listen/F% %/Users",
	/*	"___\\_&AOcA4w__", "xx1\\________-o", */
	/*	"______&BekF3AXVBd0_", "INBOX/____________-", */
		NULL, NULL };
	int i;
	struct mailbox_match *result;

	for (i = 0; trythese[i] != NULL; i++) {
		result = mailbox_match_new(trythese[i]);

		fail_unless (MATCH(result->insensitive, getthese[(i*2)+1]), "failed insensitive: [%s] [%s]\n", result->insensitive, getthese[(i*2)+1]);
		if (getthese[i*2])
			fail_unless (MATCH(result->sensitive, getthese[i*2]), "failed sensitive: [%s] [%s]\n", result->sensitive, getthese[i*2]);

		mailbox_match_free(result);
	}
}
END_TEST

/* mailbox functionality */
/** 
 * \brief find mailbox "name" for a user
 * \param name name of mailbox
 * \param user_idnr
 * \param mailbox_idnr will hold mailbox_idnr after return. Must hold a valid
 * pointer on call.
 * \return
 *      - -1 on failure
 *      - 0 if mailbox not found
 *      - 1 if found
 */
//int db_findmailbox(const char *name, uint64_t user_idnr,
//		   /*@out@*/ uint64_t * mailbox_idnr);
/**
 * \brief finds all the mailboxes owned by owner_idnr who match 
 *        the pattern.
 * \param owner_idnr
 * \param pattern pattern
 * \param children pointer to a list of mailboxes conforming to
 *        pattern. This will be filled when the function
 *        returns and needs to be free-d by caller
 * \param nchildren number of mailboxes in children
 * \param only_subscribed only search in subscribed mailboxes.
 * \return 
 *      - -1 on failure
 *      - 0 on success
 */
//int db_findmailbox_by_regex(uint64_t owner_idnr, const char *pattern,
//			    uint64_t ** children, unsigned *nchildren,
//			    int only_subscribed);
START_TEST(test_db_findmailbox_by_regex)
{
	GList *children = NULL;
	uint64_t mailbox_id = 0;

	db_createmailbox("INBOX/Trash", testidnr, &mailbox_id);
	db_findmailbox_by_regex(testidnr, "INBOX/Trash", &children, 0);
}
END_TEST


START_TEST(test_db_createmailbox)
{
	uint64_t owner_id=99999999;
	uint64_t mailbox_id=0;
	int result;

	result = db_createmailbox("INBOX", owner_id, &mailbox_id);
	fail_unless(result == -1, "db_createmailbox should have failed");

	result = db_createmailbox("testcreatebox", testidnr, &mailbox_id);
	fail_unless(result==DM_SUCCESS,"db_createmailbox failed");

	
}
END_TEST

/** Create a mailbox, recursively creating its parents.
 * \param mailbox Name of the mailbox to create
 * \param owner_idnr Owner of the mailbox
 * \param mailbox_idnr Fills the pointer with the mailbox id
 * \param message Returns a static pointer to the return message
 * \return
 *    0 Everything's good
 *    1 Cannot create mailbox
 *   -1 Database error
 */
START_TEST(test_db_mailbox_create_with_parents)
{
	uint64_t mailbox_idnr = 0, mb1 = 0, mb2 = 0, mb3 = 0;
	const char *message;
	int result;
	int only_empty = 0, update_curmail_size = 0 ;

	result = db_mailbox_create_with_parents("INBOX/Foo/Bar/Baz", BOX_COMMANDLINE,
			useridnr, &mailbox_idnr, &message);
	fail_unless(result == 0 && mailbox_idnr != 0, "Failed at db_mailbox_create_with_parents: [%s]", message);

	/* At this point, useridnr should have and be subscribed to several boxes... */
	result = db_findmailbox("inbox/foo/BAR/baz", useridnr, &mailbox_idnr);
	fail_unless(result == 1 && mailbox_idnr != 0, "Failed at db_findmailbox(\"inbox/foo/BAR/baz\")");
	result = db_delete_mailbox(mailbox_idnr, only_empty, update_curmail_size);
	fail_unless(result == 0, "Failed at db_findmailbox or db_delete_mailbox");

	result = db_findmailbox("InBox/Foo/bar", useridnr, &mailbox_idnr);
	fail_unless(result == 1 && mailbox_idnr != 0, "Failed at db_findmailbox(\"InBox/Foo/bar\")");
	result = db_delete_mailbox(mailbox_idnr, only_empty, update_curmail_size);
	fail_unless(result == 0, "Failed at db_findmailbox or db_delete_mailbox");

	result = db_mailbox_create_with_parents("INBOX/Foo/Bar-Baz", BOX_COMMANDLINE, useridnr, &mb1, &message);
	fail_unless(result == 0 && mb1 != 0, "Failed at db_mailbox_create_with_parents: [%s]", message);

	result = db_mailbox_create_with_parents("INBOX/Foo/Bar_Baz", BOX_COMMANDLINE, useridnr, &mb2, &message);
	fail_unless(result == 0 && mb2 != 0, "Failed at db_mailbox_create_with_parents: [%s]", message);

	result = db_mailbox_create_with_parents("INBOX/Foo/Bar=Baz", BOX_COMMANDLINE, useridnr, &mb3, &message);
	fail_unless(result == 0 && mb3 != 0, "Failed at db_mailbox_create_with_parents: [%s]", message);

	result = db_delete_mailbox(mb1, only_empty, update_curmail_size);
	fail_unless(result == 0, "Failed at db_findmailbox or db_delete_mailbox");
	result = db_delete_mailbox(mb2, only_empty, update_curmail_size);
	fail_unless(result == 0, "Failed at db_findmailbox or db_delete_mailbox");
	result = db_delete_mailbox(mb3, only_empty, update_curmail_size);
	fail_unless(result == 0, "Failed at db_findmailbox or db_delete_mailbox");

	result = db_findmailbox("INBOX/FOO", useridnr, &mailbox_idnr);
	fail_unless(result == 1 && mailbox_idnr != 0, "Failed at db_findmailbox(\"INBOX/FOO\")");
	result = db_delete_mailbox(mailbox_idnr, only_empty, update_curmail_size);
	fail_unless(result == 0, "Failed at db_findmailbox or db_delete_mailbox");

	result = db_findmailbox("INBox", useridnr, &mailbox_idnr);
	fail_unless(result == 1 && mailbox_idnr != 0, "Failed at db_findmailbox(\"INBox\")");
	result = db_delete_mailbox(mailbox_idnr, only_empty, update_curmail_size);
	fail_unless(result == 0, "We just deleted inbox. Something silly.");
	/* Cool, we've cleaned up after ourselves. */

}
END_TEST

/**
 * \brief delete a mailbox. 
 * \param mailbox_idnr
 * \param only_empty if non-zero the mailbox will only be emptied,
 *        i.e. all messages in it will be deleted.
 * \param update_curmail_size if non-zero the curmail_size of the
 *        user will be updated.
* \return 
*    - -1 on database failure
*    - 0 on success
* \attention this function is unable to delete shared mailboxes
*/
//int db_delete_mailbox(uint64_t mailbox_idnr, int only_empty,
//		      int update_curmail_size);
START_TEST(test_db_delete_mailbox)
{
	uint64_t mailbox_id = 999999999;
	int result;

	result = db_delete_mailbox(mailbox_id, 0, 0);
	fail_unless(result != DM_SUCCESS, "db_delete_mailbox should have failed");

	result = db_createmailbox("testdeletebox",testidnr, &mailbox_id);
	fail_unless(result == DM_SUCCESS,"db_createmailbox failed");
	result = db_delete_mailbox(mailbox_id,0,1);
	fail_unless(result == DM_SUCCESS,"db_delete_mailbox failed");

}
END_TEST

/* Insert or update a replycache entry.
 * int db_replycache_register(const char *to, const char *from, const char *handle);

 * Returns DM_SUCCESS if the (to, from) pair hasn't been seen in days.
 * int db_replycache_validate(const char *to, const char *from, const char *handle, int days);

 * Remove a replycache entry.
 * int db_replycache_unregister(const char *to, const char *from, const char *handle);
 */
START_TEST(test_db_replycache)
{
	int result;

	result = db_replycache_register("test_to", "test_from", "test_handle");
	fail_unless(result == TRUE, "failed to register");

	/* Should always be DM_SUCCESS */
	result = db_replycache_validate("test_to", "test_from", "test_handle", 0);
	//fail_unless(result == DM_SUCCESS, "failed with days = 0");

	/* Should not be DM_SUCCESS, since we just inserted it. */
	result = db_replycache_validate("test_to", "test_from", "test_handle", 1);
	fail_unless(result != DM_SUCCESS, "failed with days = 1");

	/* Should not be DM_SUCCESS, since we just inserted it. */
	result = db_replycache_validate("test_to", "test_from", "test_handle", 2);
	fail_unless(result != DM_SUCCESS, "failed with days = 2");

	/* Should not be DM_SUCCESS, since we just inserted it. */
	result = db_replycache_validate("test_to", "test_from", "test_handle", 1100);
	fail_unless(result != DM_SUCCESS, "failed with days = 1100");

	result = db_replycache_unregister("test_to", "test_from", "test_handle");
	fail_unless(result == TRUE, "failed to unregister");
}
END_TEST



START_TEST(test_db_get_sql)
{
	const char *s = db_get_sql(SQL_CURRENT_TIMESTAMP);
	fail_unless(s != NULL);
}
END_TEST

START_TEST(test_diff_time)
{
	struct timeval before, after;
	int diff;

	before.tv_sec = 1; before.tv_usec = 0;
	after.tv_sec = 2; after.tv_usec = 0;
	diff = diff_time(before, after);
	fail_unless(diff == 1);

	before.tv_sec = 1; before.tv_usec = 1000000 - 1;
	after.tv_sec = 2; after.tv_usec = 0;
	diff = diff_time(before, after);
	fail_unless(diff == 0);
	
	before.tv_sec = 1; before.tv_usec = 500001;
	after.tv_sec = 2; after.tv_usec = 0;
	diff = diff_time(before, after);
	fail_unless(diff == 0);

	before.tv_sec = 1; before.tv_usec = 499999;
	after.tv_sec = 2; after.tv_usec = 0;
	diff = diff_time(before, after);
	fail_unless(diff == 1);
}
END_TEST

Suite *dbmail_db_suite(void)
{
	Suite *s = suite_create("Dbmail Basic Database Functions");

	TCase *tc_db = tcase_create("DB");
	suite_add_tcase(s, tc_db);
	tcase_add_checked_fixture(tc_db, setup, teardown);

	tcase_add_test(tc_db, test_db_stmt_prepare);
	tcase_add_test(tc_db, test_db_stmt_set_str);

	tcase_add_test(tc_db, test_Connection_executeQuery);
	tcase_add_test(tc_db, test_db_createmailbox);
	tcase_add_test(tc_db, test_db_delete_mailbox);
	tcase_add_test(tc_db, test_db_replycache);
	tcase_add_test(tc_db, test_db_mailbox_set_permission);
	tcase_add_test(tc_db, test_db_mailbox_create_with_parents);
	tcase_add_test(tc_db, test_mailbox_match_new);
	tcase_add_test(tc_db, test_db_findmailbox_by_regex);
	tcase_add_test(tc_db, test_db_get_sql);
	tcase_add_test(tc_db, test_diff_time);

	return s;
}

int main(void)
{
	int nf;
	Suite *s = dbmail_db_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

