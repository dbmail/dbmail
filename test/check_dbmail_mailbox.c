/*
 *  Copyright (c) 2004-2011 NFG Net Facilities Group BV support@nfg.nl
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

extern char * multipart_message;
extern char * configFile;
extern db_param_t _db_params;
#define DBPFX _db_params.pfx



/* we need this one because we can't directly link imapd.o */
int imap_before_smtp = 0;
	
static void init_testuser1(void) 
{
        u64_t user_idnr;
	if (! (auth_user_exists("testuser1",&user_idnr)))
		auth_adduser("testuser1","test", "md5", 101, 1024000, &user_idnr);
}

static gboolean tree_print(gpointer key, gpointer value, gpointer data UNUSED)
{
	if (! (key && value))
		return TRUE;

	u64_t *k = (u64_t *)key;
	u64_t *v = (u64_t *)value;
	printf("[%llu: %llu]\n", *k, *v);
	return FALSE;
}

void tree_dump(GTree *t)
{
	TRACE(TRACE_DEBUG,"start");
	g_tree_foreach(t,(GTraverseFunc)tree_print,NULL);
	TRACE(TRACE_DEBUG,"done");
}

static gboolean _node_cat(u64_t *key, u64_t *value, GString **s)
{
	if (! (key && value))
		return TRUE;
	
	g_string_append_printf(*(GString **)s, "[%llu: %llu]\n", *key,*value);

	return FALSE;
}

char * tree_as_string(GTree *t)
{
	char *result;
	GString *s = g_string_new("");
	
	TRACE(TRACE_DEBUG,"start");
	g_tree_foreach(t,(GTraverseFunc)_node_cat,&s);
	TRACE(TRACE_DEBUG,"done");

	result = s->str;
	g_string_free(s,FALSE);
	return result;
}

static u64_t get_mailbox_id(const char *name)
{
	u64_t id, owner;
	auth_user_exists("testuser1",&owner);
	db_find_create_mailbox(name, BOX_COMMANDLINE, owner, &id);
	return id;
}

void setup(void)
{
	configure_debug(511,0);
	config_read(configFile);
	GetDBParams();
	db_connect();
	auth_connect();
	g_mime_init(0);
	init_testuser1();
}

void teardown(void)
{
	auth_disconnect();
	db_disconnect();
	config_free();
	g_mime_shutdown();
}


/****************************************************************************************
 *
 *
 * TestCases
 *
 *
 ***************************************************************************************/

START_TEST(test_dbmail_mailbox_new)
{
	DbmailMailbox *mb = dbmail_mailbox_new(get_mailbox_id("INBOX"));
	fail_unless(mb!=NULL, "dbmail_mailbox_new failed");
	dbmail_mailbox_free(mb);
}
END_TEST

START_TEST(test_dbmail_mailbox_free)
{
	DbmailMailbox *mb = dbmail_mailbox_new(get_mailbox_id("INBOX"));
	dbmail_mailbox_free(mb);
}
END_TEST

START_TEST(test_dbmail_mailbox_dump)
{
	int c = 0;
	FILE *o = fopen("/dev/null","w");
	DbmailMailbox *mb = dbmail_mailbox_new(get_mailbox_id("INBOX"));
	c = dbmail_mailbox_dump(mb,o);
	fail_unless(c>=0,"dbmail_mailbox_dump failed");
	dbmail_mailbox_free(mb);
//	fprintf(stderr,"dumped [%d] messages\n", c);
}
END_TEST

START_TEST(test_dbmail_mailbox_build_imap_search)
{
	char *args;
	char **array;
	u64_t idx = 0;
	gboolean sorted = 1;

	DbmailMailbox *mb, *mc, *md;
	
	// first case
	mb = dbmail_mailbox_new(get_mailbox_id("INBOX"));
	args = g_strdup("( arrival cc date reverse from size subject to ) us-ascii "
			"HEADER FROM paul@nfg.nl SINCE 1-Feb-1994");
	array = g_strsplit(args," ",0);
	g_free(args);
	
	dbmail_mailbox_build_imap_search(mb, array, &idx, sorted);

	dbmail_mailbox_free(mb);
	g_strfreev(array);
	
	// second case
	idx = 0;
	mc = dbmail_mailbox_new(get_mailbox_id("INBOX"));
	args = g_strdup("( arrival ) ( cc ) us-ascii HEADER FROM paul@nfg.nl "
			"SINCE 1-Feb-1990");
	array = g_strsplit(args," ",0);
	g_free(args);

	dbmail_mailbox_build_imap_search(mc, array, &idx, sorted);
	
	dbmail_mailbox_free(mc);
	g_strfreev(array);
	
	// third case
	idx = 0;
	md = dbmail_mailbox_new(get_mailbox_id("INBOX"));
	args = g_strdup("( arrival cc date reverse from size subject to ) us-ascii "
			"HEADER FROM test ( SINCE 1-Feb-1995 OR HEADER SUBJECT test "
			"HEADER SUBJECT foo )");
	array = g_strsplit(args," ",0);
	g_free(args);

	dbmail_mailbox_build_imap_search(md, array, &idx, sorted);
	fail_unless(g_node_max_height(g_node_get_root(md->search))==4, 
			"build_search: tree too shallow");
	
	dbmail_mailbox_free(md);
	g_strfreev(array);

	// fourth case
	idx = 0;
	md = dbmail_mailbox_new(get_mailbox_id("INBOX"));
	args = g_strdup("1,* ( arrival cc date reverse from size subject to ) us-ascii "
			"HEADER FROM test ( SINCE 1-Feb-1995 OR HEADER SUBJECT test "
			"HEADER SUBJECT foo )");
	array = g_strsplit(args," ",0);
	g_free(args);

	dbmail_mailbox_build_imap_search(md, array, &idx, 1);
	fail_unless(g_node_max_height(g_node_get_root(md->search))==4, 
			"build_search: tree too shallow");
	
	dbmail_mailbox_free(md);
	g_strfreev(array);
}
END_TEST

START_TEST(test_dbmail_mailbox_sort)
{
	char *args;
	char **array;
	u64_t idx = 0;
	gboolean sorted = 1;

	DbmailMailbox *mb;
	
	// first case
	mb = dbmail_mailbox_new(get_mailbox_id("INBOX"));
	args = g_strdup("( arrival cc date reverse from size subject to ) us-ascii HEADER FROM test SINCE 1-Feb-1994");
	array = g_strsplit(args," ",0);
	g_free(args);
	
	dbmail_mailbox_build_imap_search(mb, array, &idx, sorted);
	dbmail_mailbox_sort(mb);
	
	dbmail_mailbox_free(mb);
	g_strfreev(array);
}
END_TEST

START_TEST(test_dbmail_mailbox_search)
{
	char *args;
	char **array;
	u64_t idx = 0;
	gboolean sorted = 1;
	int all, found, notfound;
	DbmailMailbox *mb;
	
	// first case
	mb = dbmail_mailbox_new(get_mailbox_id("INBOX"));
	args = g_strdup("( arrival cc date reverse from size subject to ) us-ascii "
			"HEADER FROM foo SINCE 1-Feb-1994 ( SENTSINCE 1-Feb-1995 OR BEFORE 1-Jan-2006 SINCE 1-Jan-2005 )");
	array = g_strsplit(args," ",0);
	g_free(args);
	
	dbmail_mailbox_build_imap_search(mb, array, &idx, sorted);
	dbmail_mailbox_sort(mb);
	dbmail_mailbox_search(mb);
	
	dbmail_mailbox_free(mb);
	g_strfreev(array);

	// second case
	//
	idx=0;
	sorted = 0;
	mb = dbmail_mailbox_new(get_mailbox_id("INBOX"));
	args = g_strdup("1:*");
	array = g_strsplit(args," ",0);
	g_free(args);
	
	dbmail_mailbox_build_imap_search(mb, array, &idx, sorted);
	dbmail_mailbox_search(mb);
	all = g_tree_nnodes(mb->found);
	
	dbmail_mailbox_free(mb);
	g_strfreev(array);
	
	idx=0;
	sorted = 0;
	mb = dbmail_mailbox_new(get_mailbox_id("INBOX"));
	args = g_strdup("1:* TEXT @");
	array = g_strsplit(args," ",0);
	g_free(args);
	
	dbmail_mailbox_build_imap_search(mb, array, &idx, sorted);
	dbmail_mailbox_search(mb);
	found = g_tree_nnodes(mb->found);
	
	dbmail_mailbox_free(mb);
	g_strfreev(array);
	
	idx=0;
	sorted = 0;
	mb = dbmail_mailbox_new(get_mailbox_id("INBOX"));
	args = g_strdup("1:* NOT TEXT @");
	array = g_strsplit(args," ",0);
	g_free(args);
	
	dbmail_mailbox_build_imap_search(mb, array, &idx, sorted);
	dbmail_mailbox_search(mb);
	notfound = g_tree_nnodes(mb->found);
	
	dbmail_mailbox_free(mb);
	g_strfreev(array);

	fail_unless((all - found) == notfound, "dbmail_mailbox_search failed: SEARCH NOT (all: %d, found: %d, notfound: %d)", all, found, notfound);
	
	// third case
	idx=0;
	sorted = 0;
	mb = dbmail_mailbox_new(get_mailbox_id("INBOX"));
	args = g_strdup("UID 1,* BODY paul@nfg.nl");
	array = g_strsplit(args," ",0);
	g_free(args);
	
	dbmail_mailbox_build_imap_search(mb, array, &idx, sorted);
	dbmail_mailbox_search(mb);
	
	dbmail_mailbox_free(mb);
	g_strfreev(array);

	// 
	idx=0;
	sorted = 0;
	mb = dbmail_mailbox_new(get_mailbox_id("INBOX"));
	args = g_strdup("1");
	array = g_strsplit(args," ",0);
	g_free(args);
	
	dbmail_mailbox_build_imap_search(mb, array, &idx, sorted);
	dbmail_mailbox_search(mb);
	found = g_tree_nnodes(mb->found);
	fail_unless(found==1,"dbmail_mailbox_search failed: SEARCH UID 1");
	
	dbmail_mailbox_free(mb);
	g_strfreev(array);

	// 
	idx=0;
	sorted = 0;
	mb = dbmail_mailbox_new(get_mailbox_id("INBOX"));
	args = g_strdup("OR FROM myclient SUBJECT myclient");
	array = g_strsplit(args," ",0);
	g_free(args);
	
	dbmail_mailbox_build_imap_search(mb, array, &idx, sorted);
	dbmail_mailbox_search(mb);
//	found = g_tree_nnodes(mb->ids);
//	fail_unless(found==1,"dbmail_mailbox_search failed: SEARCH UID 1");
	
	dbmail_mailbox_free(mb);
	g_strfreev(array);


}
END_TEST

START_TEST(test_dbmail_mailbox_search_parsed_1)
{
	u64_t idx=0;
	gboolean sorted = 0;
	DbmailMailbox *mb = dbmail_mailbox_new(get_mailbox_id("INBOX"));
	char *args = g_strdup("UID 1 BODY unlikelyaddress@nfg.nl");
	char **array = g_strsplit(args," ",0);
	g_free(args);
	dbmail_mailbox_build_imap_search(mb, array, &idx, sorted);
	dbmail_mailbox_search(mb);
	dbmail_mailbox_free(mb);
	g_strfreev(array);
}
END_TEST

START_TEST(test_dbmail_mailbox_search_parsed_2)
{
	u64_t idx=0;
	gboolean sorted = 0;
	DbmailMailbox *mb = dbmail_mailbox_new(get_mailbox_id("INBOX"));
	char *args = g_strdup("UID 1,* BODY the");
	char **array = g_strsplit(args," ",0);
	g_free(args);
	dbmail_mailbox_build_imap_search(mb, array, &idx, sorted);
	dbmail_mailbox_search(mb);
	dbmail_mailbox_free(mb);
	g_strfreev(array);
}
END_TEST

START_TEST(test_dbmail_mailbox_orderedsubject)
{
	char *res;
	char *args;
	char **array;
	u64_t idx = 0;
	DbmailMailbox *mb = dbmail_mailbox_new(get_mailbox_id("INBOX"));
	
	args = g_strdup("HEADER FROM foo.org ( SINCE 1-Jan-2005 )");
	array = g_strsplit(args," ",0);
	g_free(args);

	dbmail_mailbox_build_imap_search(mb, array, &idx, 0);
	dbmail_mailbox_search(mb);
	
	dbmail_mailbox_set_uid(mb,TRUE);
	res = dbmail_mailbox_orderedsubject(mb);
	fail_unless(res!= NULL, "dbmail_mailbox_orderedsubject failed");
	g_free(res);
	
	dbmail_mailbox_set_uid(mb,FALSE);
	res = dbmail_mailbox_orderedsubject(mb);
	fail_unless(res!= NULL, "dbmail_mailbox_orderedsubject failed");
	g_free(res);
	
	dbmail_mailbox_free(mb);
	g_strfreev(array);

}
END_TEST
START_TEST(test_dbmail_mailbox_get_set)
{
	guint c, d, r;
	GTree *set;
	DbmailMailbox *mb = dbmail_mailbox_new(get_mailbox_id("INBOX"));
	dbmail_mailbox_set_uid(mb,TRUE);
	r = dbmail_mailbox_open(mb);

	fail_unless(r == DM_SUCCESS, "dbmail_mailbox_open failed");

	// basic tests;
	set = dbmail_mailbox_get_set(mb, "1:*", 0);
	fail_unless(set != NULL,"dbmail_mailbox_get_set failed");
	c = g_tree_nnodes(set);
	fail_unless(c>1,"dbmail_mailbox_get_set failed [%d]", c);
	g_tree_destroy(set);

	set = dbmail_mailbox_get_set(mb,"*:1",0);
	fail_unless(set != NULL,"dbmail_mailbox_get_set failed");
	d = g_tree_nnodes(set);
	fail_unless(c==d,"dbmail_mailbox_get_set failed [%d != %d]", c, d);
	g_tree_destroy(set);

	set = dbmail_mailbox_get_set(mb,"1,*",0);
	fail_unless(set != NULL,"dbmail_mailbox_get_set failed");
	d = g_tree_nnodes(set);
	fail_unless(d==2,"mailbox_get_set failed [%d != 2]", d);
	g_tree_destroy(set);

	set = dbmail_mailbox_get_set(mb,"1,*",0);
	fail_unless(set != NULL,"dbmail_mailbox_get_set failed");
	d = g_tree_nnodes(set);
	fail_unless(d==2,"mailbox_get_set failed");
	g_tree_destroy(set);
	
	set = dbmail_mailbox_get_set(mb,"1",0);
	fail_unless(set != NULL,"dbmail_mailbox_get_set failed");
	d = g_tree_nnodes(set);
	fail_unless(d==1,"mailbox_get_set failed");
	g_tree_destroy(set);

	set = dbmail_mailbox_get_set(mb,"-1:1",0);
	fail_unless(set == NULL);


	// UID sets
	char *s, *t;

	set = dbmail_mailbox_get_set(mb, "1:*", 1);
	fail_unless(set != NULL,"dbmail_mailbox_get_set failed");
	s = tree_as_string(set);
	c = g_tree_nnodes(set);
	fail_unless(c>1,"dbmail_mailbox_get_set failed");
	g_tree_destroy(set);

	set = dbmail_mailbox_get_set(mb, "1:*", 0);
	fail_unless(set != NULL,"dbmail_mailbox_get_set failed");
	t = tree_as_string(set);
	fail_unless(strncmp(s,t,1024)==0,"mismatch between <1:*> and <UID 1:*>\n%s\n%s", s,t);
	g_tree_destroy(set);
	g_free(s);
	g_free(t);
	
	dbmail_mailbox_free(mb);


	// empty box
	mb = dbmail_mailbox_new(get_mailbox_id("empty"));
	dbmail_mailbox_open(mb);

	set = dbmail_mailbox_get_set(mb, "1:*", 0);
	fail_unless(set == NULL,"dbmail_mailbox_get_set failed");
	
	set = dbmail_mailbox_get_set(mb, "*", 0);
	fail_unless(set == NULL,"dbmail_mailbox_get_set failed");

	set = dbmail_mailbox_get_set(mb, "1", 0);
	fail_unless(set == NULL,"dbmail_mailbox_get_set failed");

	dbmail_mailbox_free(mb);
}
END_TEST

Suite *dbmail_mailbox_suite(void)
{
	Suite *s = suite_create("Dbmail Mailbox");

	TCase *tc_mailbox = tcase_create("Mailbox");
	suite_add_tcase(s, tc_mailbox);
	tcase_add_checked_fixture(tc_mailbox, setup, teardown);
	tcase_add_test(tc_mailbox, test_dbmail_mailbox_get_set);
	tcase_add_test(tc_mailbox, test_dbmail_mailbox_new);
	tcase_add_test(tc_mailbox, test_dbmail_mailbox_free);
	tcase_add_test(tc_mailbox, test_dbmail_mailbox_dump);
	tcase_add_test(tc_mailbox, test_dbmail_mailbox_build_imap_search);
	tcase_add_test(tc_mailbox, test_dbmail_mailbox_sort);
	tcase_add_test(tc_mailbox, test_dbmail_mailbox_search);
	tcase_add_test(tc_mailbox, test_dbmail_mailbox_search_parsed_1);
	tcase_add_test(tc_mailbox, test_dbmail_mailbox_search_parsed_2);
	tcase_add_test(tc_mailbox, test_dbmail_mailbox_orderedsubject);
	
	return s;
}

int main(void)
{
	int nf;
	Suite *s = dbmail_mailbox_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
	

