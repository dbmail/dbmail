/*
 *  Copyright (C) 2004  Paul Stevens <paul@nfg.nl>
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
 *  $Id: check_dbmail_deliver.c 1829 2005-08-01 14:53:53Z paul $ 
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

static u64_t get_first_user_idnr(void)
{
	u64_t user_idnr;
	GList *users = auth_get_known_users();
	users = g_list_first(users);
	auth_user_exists((char *)users->data,&user_idnr);
	return user_idnr;
}

static u64_t get_mailbox_id(void)
{
	u64_t id, owner;
	auth_user_exists("testuser1",&owner);
	db_find_create_mailbox("INBOX", BOX_COMMANDLINE, owner, &id);
	return id;
}

void setup(void)
{
	configure_debug(5,1,0);
	config_read(configFile);
	GetDBParams(&_db_params);
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
	struct DbmailMailbox *mb = dbmail_mailbox_new(get_mailbox_id());
	fail_unless(mb!=NULL, "dbmail_mailbox_new failed");
}
END_TEST

START_TEST(test_dbmail_mailbox_free)
{
	struct DbmailMailbox *mb = dbmail_mailbox_new(get_mailbox_id());
	dbmail_mailbox_free(mb);
}
END_TEST

START_TEST(test_dbmail_mailbox_open)
{
	struct DbmailMailbox *mb = dbmail_mailbox_new(get_mailbox_id());
	mb = dbmail_mailbox_open(mb);
	fail_unless(mb!=NULL, "dbmail_mailbox_open failed");
}
END_TEST

START_TEST(test_dbmail_mailbox_dump)
{
	int c = 0;
	FILE *o = fopen("/dev/null","w");
	struct DbmailMailbox *mb = dbmail_mailbox_new(get_mailbox_id());
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

	search_key_t *sk = NULL, *save;
	struct DbmailMailbox *mb, *mc, *md;
	
	// first case
	save = g_new0(search_key_t,1);
	mb = dbmail_mailbox_new(get_mailbox_id());
	
	args = g_strdup("( arrival cc date reverse from size subject to ) us-ascii HEADER FROM paul@nfg.nl SINCE 1-Feb-1994");
	array = g_strsplit(args," ",0);
	g_free(args);
	
	sk = save;
	dbmail_mailbox_build_imap_search(mb, array, &idx, sorted);

	dbmail_mailbox_free(mb);
	g_free(save);
	g_strfreev(array);
	
	// second case
	sk = NULL;
	idx = 0;
	save = g_new0(search_key_t,1);
	mc = dbmail_mailbox_new(get_mailbox_id());
	args = g_strdup("( arrival ) ( cc ) us-ascii HEADER FROM paul@nfg.nl SINCE 1-Feb-1990");
	array = g_strsplit(args," ",0);
	g_free(args);

	sk = save;
	dbmail_mailbox_build_imap_search(mc, array, &idx, sorted);

	
	dbmail_mailbox_free(mc);
	g_free(save);
	g_strfreev(array);
	
	// third case
	sk = NULL;
	idx = 0;
	save = g_new0(search_key_t,1);
	md = dbmail_mailbox_new(get_mailbox_id());
	args = g_strdup("( arrival cc date reverse from size subject to ) us-ascii "
			"HEADER FROM test ( SINCE 1-Feb-1995 OR HEADER SUBJECT test HEADER SUBJECT foo )");
	
	array = g_strsplit(args," ",0);
	g_free(args);

	sk = save;
	dbmail_mailbox_build_imap_search(md, array, &idx, sorted);

	fail_unless(g_node_max_height(g_node_get_root(md->search))==4, "build_search: tree too shallow");
	
	dbmail_mailbox_free(md);
	g_free(save);
	g_strfreev(array);

	// fourth case
	sk = NULL;
	idx = 0;
	save = g_new0(search_key_t,1);
	md = dbmail_mailbox_new(get_mailbox_id());
	args = g_strdup("1,* ( arrival cc date reverse from size subject to ) us-ascii "
			"HEADER FROM test ( SINCE 1-Feb-1995 OR HEADER SUBJECT test HEADER SUBJECT foo )");
	
	array = g_strsplit(args," ",0);
	g_free(args);

	sk = save;
	dbmail_mailbox_build_imap_search(md, array, &idx, 1);

	fail_unless(g_node_max_height(g_node_get_root(md->search))==4, "build_search: tree too shallow");
	
	dbmail_mailbox_free(md);
	g_free(save);
	g_strfreev(array);

}
END_TEST

START_TEST(test_dbmail_mailbox_sort)
{
	char *args;
	char **array;
	u64_t idx = 0;
	gboolean sorted = 1;

	struct DbmailMailbox *mb;
	
	// first case
	mb = dbmail_mailbox_new(get_mailbox_id());
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
	struct DbmailMailbox *mb;
	
	// first case
	mb = dbmail_mailbox_new(get_mailbox_id());
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
	idx=0;
	sorted = 0;
	mb = dbmail_mailbox_new(get_mailbox_id());
	args = g_strdup("1:* TEXT paul@nfg.nl");
	array = g_strsplit(args," ",0);
	g_free(args);
	
	dbmail_mailbox_build_imap_search(mb, array, &idx, sorted);
	dbmail_mailbox_search(mb);
	
	dbmail_mailbox_free(mb);
	g_strfreev(array);
	
	// third case
	idx=0;
	sorted = 0;
	mb = dbmail_mailbox_new(get_mailbox_id());
	args = g_strdup("1 BODY paul@nfg.nl");
	array = g_strsplit(args," ",0);
	g_free(args);
	
	dbmail_mailbox_build_imap_search(mb, array, &idx, sorted);
	dbmail_mailbox_search(mb);
	
	dbmail_mailbox_free(mb);
	g_strfreev(array);


}
END_TEST
START_TEST(test_dbmail_mailbox_search_parsed)
{
	char *args;
	char **array;
	u64_t idx = 0;
	gboolean sorted = 1;
	struct DbmailMailbox *mb;
	
	idx=0;
	sorted = 0;
	mb = dbmail_mailbox_new(get_mailbox_id());
	args = g_strdup("1 BODY paul@nfg.nl");
	array = g_strsplit(args," ",0);
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
	struct DbmailMailbox *mb = dbmail_mailbox_new(get_mailbox_id());
	
	dbmail_mailbox_open(mb);

	args = g_strdup("HEADER FROM test ( SINCE 1-Jan-2005 )");
	array = g_strsplit(args," ",0);
	g_free(args);

	dbmail_mailbox_build_imap_search(mb, array, &idx, 0);
	dbmail_mailbox_search(mb);
	
	dbmail_mailbox_set_uid(mb,TRUE);
	res = dbmail_mailbox_orderedsubject(mb);
	//printf("threads [%s]\n", res);
	
	dbmail_mailbox_set_uid(mb,FALSE);
	res = dbmail_mailbox_orderedsubject(mb);
	//printf("threads [%s]\n", res);
	
	g_free(res);
	dbmail_mailbox_free(mb);
	g_strfreev(array);

}
END_TEST

static gboolean tree_print(gpointer key, gpointer value, gpointer data UNUSED)
{
	if (! (key && value))
		return TRUE;

	u64_t *k = (u64_t *)key;
	u64_t *v = (u64_t *)value;
	printf("%llu: %llu\n", *k, *v);
	return FALSE;
}

void tree_dump(GTree *t)
{
	g_tree_foreach(t,(GTraverseFunc)tree_print,NULL);
}

START_TEST(test_g_tree_merge_not)
{
	u64_t r = 0;
	u64_t *k, *v;
	GTree *a, *b;
	
	a = g_tree_new_full((GCompareDataFunc)ucmp,NULL,(GDestroyNotify)g_free,(GDestroyNotify)g_free);
	b = g_tree_new_full((GCompareDataFunc)ucmp,NULL,(GDestroyNotify)g_free,(GDestroyNotify)g_free);
	
	for (r=1; r<=10; r+=2) {
		k = g_new0(u64_t,1);
		v = g_new0(u64_t,1);
		*k = r;
		*v = r;
		g_tree_insert(b,k,v);
	}
	g_tree_merge(a,b,IST_SUBSEARCH_NOT);
	fail_unless(g_tree_nnodes(a)==5,"g_tree_merge failed. Too few nodes in a.");
	
	g_tree_destroy(a);
	g_tree_destroy(b);
}
END_TEST

START_TEST(test_g_tree_merge_or)
{
	u64_t r = 0;
	u64_t *k, *v;
	GTree *a, *b;
	
	a = g_tree_new_full((GCompareDataFunc)ucmp,NULL,(GDestroyNotify)g_free,(GDestroyNotify)g_free);
	b = g_tree_new_full((GCompareDataFunc)ucmp,NULL,(GDestroyNotify)g_free,(GDestroyNotify)g_free);
	
	for (r=2; r<=10; r+=2) {
		k = g_new0(u64_t,1);
		v = g_new0(u64_t,1);
		*k = r;
		*v = r;
		g_tree_insert(b,k,v);
	}

	g_tree_merge(a,b,IST_SUBSEARCH_OR);
	fail_unless(g_tree_nnodes(a)==5,"g_tree_merge failed. Too many nodes in a.");
	
	g_tree_destroy(a);
	g_tree_destroy(b);

}
END_TEST

START_TEST(test_g_tree_merge_and)
{
	u64_t r = 0;
	u64_t *k, *v;
	GTree *a, *b;
	
	a = g_tree_new_full((GCompareDataFunc)ucmp,NULL,(GDestroyNotify)g_free,(GDestroyNotify)g_free);
	b = g_tree_new_full((GCompareDataFunc)ucmp,NULL,(GDestroyNotify)g_free,(GDestroyNotify)g_free);
	
	k = g_new0(u64_t,1);
	v = g_new0(u64_t,1);
	*k = 1;
	*v = 1;
	g_tree_insert(a,k,v);

	for (r=1; r<=10; r++) {
		k = g_new0(u64_t,1);
		v = g_new0(u64_t,1);
		*k = r;
		*v = r;
		g_tree_insert(b,k,v);
	}

	g_tree_merge(a,b,IST_SUBSEARCH_AND);
	fail_unless(g_tree_nnodes(a)==1,"g_tree_merge failed. Too few nodes in a.");
	fail_unless(g_tree_nnodes(b)==10,"g_tree_merge failed. Too few nodes in b.");
	
	g_tree_destroy(a);
	g_tree_destroy(b);

	a = g_tree_new_full((GCompareDataFunc)ucmp,NULL,(GDestroyNotify)g_free,(GDestroyNotify)g_free);
	b = g_tree_new_full((GCompareDataFunc)ucmp,NULL,(GDestroyNotify)g_free,(GDestroyNotify)g_free);
	
	for (r=2; r<=10; r+=2) {
		k = g_new0(u64_t,1);
		v = g_new0(u64_t,1);
		*k = r;
		*v = r;
		g_tree_insert(a,k,v);
	}

	for (r=1; r<=10; r++) {
		k = g_new0(u64_t,1);
		v = g_new0(u64_t,1);
		*k = r;
		*v = r;
		g_tree_insert(b,k,v);
	}

	g_tree_merge(a,b,IST_SUBSEARCH_AND);
	fail_unless(g_tree_nnodes(a)==5,"g_tree_merge failed. Too few nodes in a.");
	fail_unless(g_tree_nnodes(b)==10,"g_tree_merge failed. Too few nodes in b.");
	
	g_tree_destroy(a);
	g_tree_destroy(b);

}
END_TEST

START_TEST(test_dbmail_mailbox_get_set)
{
	guint c, d;
	GTree *set;
	struct DbmailMailbox *mb = dbmail_mailbox_new(get_mailbox_id());
	dbmail_mailbox_set_uid(mb,TRUE);
	search_key_t *s = g_new0(search_key_t, 1);
	s->type = IST_SET;

	strncpy(s->search,"1:*",MAX_SEARCH_LEN);
	set = dbmail_mailbox_get_set(mb, s);
	c = g_tree_nnodes(set);
	fail_unless(c>1,"dbmail_mailbox_get_set failed");
	g_tree_destroy(set);

	strncpy(s->search,"*:1",MAX_SEARCH_LEN);
	set = dbmail_mailbox_get_set(mb,s);
	d = g_tree_nnodes(set);
	fail_unless(c==d,"dbmail_mailbox_get_set failed");
	g_tree_destroy(set);

	strncpy(s->search,"1,*",MAX_SEARCH_LEN);
	set = dbmail_mailbox_get_set(mb,s);
	d = g_tree_nnodes(set);
	fail_unless(d==2,"mailbox_get_set failed");
	g_tree_destroy(set);

	s->type = IST_SET;
	
	strncpy(s->search,"1,*",MAX_SEARCH_LEN);
	set = dbmail_mailbox_get_set(mb,s);
	d = g_tree_nnodes(set);
	fail_unless(d==2,"mailbox_get_set failed");
	g_tree_destroy(set);
	
	strncpy(s->search,"1",MAX_SEARCH_LEN);
	set = dbmail_mailbox_get_set(mb,s);
	d = g_tree_nnodes(set);
	fail_unless(d==1,"mailbox_get_set failed");
	g_tree_destroy(set);

	g_free(s);
	dbmail_mailbox_free(mb);
}
END_TEST

Suite *dbmail_mailbox_suite(void)
{
	Suite *s = suite_create("Dbmail Mailbox");

	TCase *tc_mailbox = tcase_create("Mailbox");
	suite_add_tcase(s, tc_mailbox);
	tcase_add_checked_fixture(tc_mailbox, setup, teardown);
	/*
	tcase_add_test(tc_mailbox, test_g_tree_merge_or);
	tcase_add_test(tc_mailbox, test_g_tree_merge_and);
	tcase_add_test(tc_mailbox, test_g_tree_merge_not);
	tcase_add_test(tc_mailbox, test_dbmail_mailbox_get_set);
	tcase_add_test(tc_mailbox, test_dbmail_mailbox_new);
	tcase_add_test(tc_mailbox, test_dbmail_mailbox_free);
	tcase_add_test(tc_mailbox, test_dbmail_mailbox_open);
	tcase_add_test(tc_mailbox, test_dbmail_mailbox_dump);
	tcase_add_test(tc_mailbox, test_dbmail_mailbox_build_imap_search);
	tcase_add_test(tc_mailbox, test_dbmail_mailbox_sort);
	tcase_add_test(tc_mailbox, test_dbmail_mailbox_search);
	*/
	
	tcase_add_test(tc_mailbox, test_dbmail_mailbox_search_parsed);
	/*
	tcase_add_test(tc_mailbox, test_dbmail_mailbox_orderedsubject);
	*/
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
	

