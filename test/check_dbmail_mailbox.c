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
#include <assert.h>
#include "check_dbmail.h"

extern char *multipart_message;
extern char configFile[PATH_MAX];
extern DBParam_T db_params;
extern Mempool_T small_pool;
#define DBPFX db_params.pfx

uint64_t empty_box = 0;

/* we need this one because we can't directly link imapd.o */
int imap_before_smtp = 0;

static void add_message(void)
{
	int result;
	DbmailMessage *message;
	Mempool_T pool = mempool_open();
	List_T dsnusers = p_list_new(pool);
	Delivery_T *dsnuser = g_new0(Delivery_T,1);
	GList *userids = NULL;
	uint64_t *uid;
	

        uint64_t user_idnr;
	if (! (auth_user_exists("testuser1",&user_idnr)))
		auth_adduser("testuser1","test", "md5", 101, 1024000, &user_idnr);

	uid = g_new0(uint64_t,1);
	*uid = user_idnr;
	userids = g_list_prepend(userids, uid);

	message = dbmail_message_new(NULL);
	message = dbmail_message_init_with_string(message,multipart_message);

	dsnuser_init(dsnuser);
	dsnuser->address = g_strdup("testuser1");
	dsnuser->userids = userids;

	dsnusers = p_list_prepend(dsnusers, dsnuser);
	
	result = insert_messages(message, dsnusers);

	assert(result==0);

	dsnuser_free_list(dsnusers);
	dbmail_message_free(message);
	mempool_close(&pool);
}

static gboolean tree_print(gpointer key, gpointer value, gpointer data UNUSED)
{
	if (! (key && value))
		return TRUE;

	uint64_t *k = (uint64_t *)key;
	uint64_t *v = (uint64_t *)value;
	printf("[%" PRIu64 ": %" PRIu64 "]\n", *k, *v);
	return FALSE;
}

void tree_dump(GTree *t)
{
	TRACE(TRACE_DEBUG,"start");
	g_tree_foreach(t,(GTraverseFunc)tree_print,NULL);
	TRACE(TRACE_DEBUG,"done");
}

static gboolean _node_cat(uint64_t *key, uint64_t *value, GString **s)
{
	if (! (key && value))
		return TRUE;
	
	g_string_append_printf(*(GString **)s, "[%" PRIu64 ": %" PRIu64 "]\n", *key,*value);

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

static uint64_t get_mailbox_id(const char *name)
{
	uint64_t id, owner;
	auth_user_exists("testuser1",&owner);
	db_find_create_mailbox(name, BOX_COMMANDLINE, owner, &id);
	assert(id);
	return id;
}

void setup(void)
{
	config_get_file();
	config_read(configFile);
	configure_debug(NULL,511,0);
	GetDBParams();
	db_connect();
	auth_connect();
	small_pool = mempool_open();
	empty_box = get_mailbox_id("empty");
	add_message();
	add_message();
}

void teardown(void)
{
	db_delete_mailbox(empty_box, 0, 0);
	auth_disconnect();
	db_disconnect();
	config_free();
	mempool_close(&small_pool);
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
	DbmailMailbox *mb = dbmail_mailbox_new(NULL, get_mailbox_id("INBOX"));
	fail_unless(mb!=NULL, "dbmail_mailbox_new failed");
	dbmail_mailbox_free(mb);
}
END_TEST

START_TEST(test_dbmail_mailbox_free)
{
	DbmailMailbox *mb = dbmail_mailbox_new(NULL, get_mailbox_id("INBOX"));
	dbmail_mailbox_free(mb);
}
END_TEST

START_TEST(test_dbmail_mailbox_dump)
{
	int c = 0;
	FILE *o = fopen("/dev/null","w");
	DbmailMailbox *mb = dbmail_mailbox_new(NULL, get_mailbox_id("INBOX"));
	c = dbmail_mailbox_dump(mb,o);
	fail_unless(c>=0,"dbmail_mailbox_dump failed");
	dbmail_mailbox_free(mb);
//	fprintf(stderr,"dumped [%d] messages\n", c);
}
END_TEST

static String_T * _build_search_keys(Mempool_T pool, const char *args, size_t *size)
{
	int idx;
	String_T *search_keys;
	int arglen = 0;
	char **array;
	array = g_strsplit(args," ",0);

	while (array[arglen++]);
	*size = sizeof(String_T) * arglen;
	search_keys = (String_T *)mempool_pop(pool, *size);
	for (idx=0; idx<arglen && array[idx]; idx++) {
		search_keys[idx] = p_string_new(pool, array[idx]);
	}
	g_strfreev(array);
	return search_keys;
}

	
START_TEST(test_dbmail_mailbox_build_imap_search)
{
	String_T *search_keys;
	gboolean sorted = 1;
	size_t size;
	uint64_t idx;

	Mempool_T pool = mempool_open();
	DbmailMailbox *mb, *mc, *md;
	
	// first case
	search_keys = _build_search_keys(pool, "( arrival cc date reverse from size subject to ) us-ascii "
			"HEADER FROM paul@nfg.nl SINCE 1-Feb-1994", &size);

	mb = dbmail_mailbox_new(pool, get_mailbox_id("INBOX"));

	idx = 0;
	dbmail_mailbox_build_imap_search(mb, search_keys, &idx, sorted);
	mempool_push(pool, search_keys, size);
	dbmail_mailbox_free(mb);
	
	// second case
	idx = 0;
	mc = dbmail_mailbox_new(pool, get_mailbox_id("INBOX"));
	search_keys = _build_search_keys(pool, "( arrival ) ( cc ) us-ascii HEADER FROM paul@nfg.nl "
			"SINCE 1-Feb-1990", &size);
	
	idx = 0;
	dbmail_mailbox_build_imap_search(mc, search_keys, &idx, sorted);
	mempool_push(pool, search_keys, size);
	
	dbmail_mailbox_free(mc);
	
	// third case
	idx = 0;
	md = dbmail_mailbox_new(pool, get_mailbox_id("INBOX"));
	search_keys = _build_search_keys(pool, "( arrival cc date reverse from size subject to ) us-ascii "
			"HEADER FROM test ( SINCE 1-Feb-1995 OR HEADER SUBJECT test "
			"HEADER SUBJECT foo )", &size);
	dbmail_mailbox_build_imap_search(md, search_keys, &idx, sorted);
	fail_unless(g_node_max_height(g_node_get_root(md->search))==4, 
			"build_search: tree too shallow");
	
	mempool_push(pool, search_keys, size);
	dbmail_mailbox_free(md);

	// fourth case
	idx = 0;
	md = dbmail_mailbox_new(pool, get_mailbox_id("INBOX"));
	search_keys = _build_search_keys(pool, "1,* ( arrival cc date reverse from size subject to ) us-ascii "
			"HEADER FROM test ( SINCE 1-Feb-1995 OR HEADER SUBJECT test "
			"HEADER SUBJECT foo )", &size);

	dbmail_mailbox_build_imap_search(md, search_keys, &idx, 1);
	fail_unless(g_node_max_height(g_node_get_root(md->search))==4, 
			"build_search: tree too shallow");
	
	dbmail_mailbox_free(md);
	mempool_push(pool, search_keys, size);
	mempool_close(&pool);
}
END_TEST

START_TEST(test_dbmail_mailbox_sort)
{
	String_T *search_keys;
	size_t size;
	uint64_t idx = 0;
	gboolean sorted = 1;

	DbmailMailbox *mb;
	Mempool_T pool = mempool_open();
	
	// first case
	mb = dbmail_mailbox_new(pool, get_mailbox_id("INBOX"));
	search_keys = _build_search_keys(pool,
			"( arrival cc date reverse from size subject to )"
		       " us-ascii HEADER FROM test SINCE 1-Feb-1994", &size);
	
	dbmail_mailbox_build_imap_search(mb, search_keys, &idx, sorted);
	dbmail_mailbox_sort(mb);
	
	dbmail_mailbox_free(mb);
	mempool_push(pool, search_keys, size);
	mempool_close(&pool);
}
END_TEST

START_TEST(test_dbmail_mailbox_search)
{
	String_T *search_keys;
	size_t size;
	uint64_t idx = 0;
	gboolean sorted = 1;
	int all, found, notfound;
	DbmailMailbox *mb;
	Mempool_T pool = mempool_open();
	
	// first case
	mb = dbmail_mailbox_new(pool, get_mailbox_id("INBOX"));
	search_keys = _build_search_keys(pool, "( arrival cc date reverse from size subject to ) us-ascii "
			"HEADER FROM foo SINCE 1-Feb-1994 ( SENTSINCE 1-Feb-1995 OR BEFORE 1-Jan-2006 SINCE 1-Jan-2005 )",
			&size);
	
	dbmail_mailbox_build_imap_search(mb, search_keys, &idx, sorted);
	dbmail_mailbox_sort(mb);
	dbmail_mailbox_search(mb);
	
	dbmail_mailbox_free(mb);
	mempool_push(pool, search_keys, size);

	// second case
	//
	idx=0;
	sorted = 0;
	mb = dbmail_mailbox_new(pool, get_mailbox_id("INBOX"));
	search_keys = _build_search_keys(pool, "1:*", &size);
	
	dbmail_mailbox_build_imap_search(mb, search_keys, &idx, sorted);
	dbmail_mailbox_search(mb);
	all = g_tree_nnodes(mb->found);
	
	dbmail_mailbox_free(mb);
	mempool_push(pool, search_keys, size);
	
	//
	idx=0;
	sorted = 0;
	mb = dbmail_mailbox_new(pool, get_mailbox_id("INBOX"));
	search_keys = _build_search_keys(pool, "1:* TEXT @", &size);
	
	dbmail_mailbox_build_imap_search(mb, search_keys, &idx, sorted);
	dbmail_mailbox_search(mb);
	found = g_tree_nnodes(mb->found);
	
	dbmail_mailbox_free(mb);
	mempool_push(pool, search_keys, size);
	
	//
	idx=0;
	sorted = 0;
	mb = dbmail_mailbox_new(pool, get_mailbox_id("INBOX"));
	search_keys = _build_search_keys(pool, "1:* NOT TEXT @", &size);
	
	dbmail_mailbox_build_imap_search(mb, search_keys, &idx, sorted);
	dbmail_mailbox_search(mb);
	notfound = g_tree_nnodes(mb->found);
	
	dbmail_mailbox_free(mb);
	mempool_push(pool, search_keys, size);

	fail_unless((all - found) == notfound, "dbmail_mailbox_search failed: SEARCH NOT (all: %d, found: %d, notfound: %d)", all, found, notfound);
	
	// third case
	idx=0;
	sorted = 0;
	mb = dbmail_mailbox_new(pool, get_mailbox_id("INBOX"));
	search_keys = _build_search_keys(pool, "UID 1,* BODY paul@nfg.nl", &size);
	
	dbmail_mailbox_build_imap_search(mb, search_keys, &idx, sorted);
	dbmail_mailbox_search(mb);
	
	dbmail_mailbox_free(mb);
	mempool_push(pool, search_keys, size);

	// 
	idx=0;
	sorted = 0;
	mb = dbmail_mailbox_new(pool, get_mailbox_id("INBOX"));
	search_keys = _build_search_keys(pool, "1", &size);
	
	dbmail_mailbox_build_imap_search(mb, search_keys, &idx, sorted);
	dbmail_mailbox_search(mb);
	found = g_tree_nnodes(mb->found);
	fail_unless(found==1,"dbmail_mailbox_search failed: SEARCH UID 1");
	
	dbmail_mailbox_free(mb);
	mempool_push(pool, search_keys, size);

	// 
	idx=0;
	sorted = 0;
	mb = dbmail_mailbox_new(pool, get_mailbox_id("INBOX"));
	search_keys = _build_search_keys(pool, "OR FROM myclient SUBJECT myclient", &size);
	
	dbmail_mailbox_build_imap_search(mb, search_keys, &idx, sorted);
	dbmail_mailbox_search(mb);
//	found = g_tree_nnodes(mb->ids);
//	fail_unless(found==1,"dbmail_mailbox_search failed: SEARCH UID 1");
	
	dbmail_mailbox_free(mb);
	mempool_push(pool, search_keys, size);
	mempool_close(&pool);
}
END_TEST

START_TEST(test_dbmail_mailbox_search_parsed_1)
{
	uint64_t idx=0;
	size_t size;
	gboolean sorted = 0;
	Mempool_T pool = mempool_open();
	DbmailMailbox *mb = dbmail_mailbox_new(pool, get_mailbox_id("INBOX"));
	String_T *search_keys = _build_search_keys(pool, "UID 1 BODY unlikelyaddress@nfg.nl", &size);
	dbmail_mailbox_build_imap_search(mb, search_keys, &idx, sorted);
	dbmail_mailbox_search(mb);
	dbmail_mailbox_free(mb);
	mempool_push(pool, search_keys, size);
	mempool_close(&pool);
}
END_TEST

START_TEST(test_dbmail_mailbox_search_parsed_2)
{
	size_t size;
	uint64_t idx=0;
	gboolean sorted = 0;
	Mempool_T pool = mempool_open();
	DbmailMailbox *mb = dbmail_mailbox_new(pool, get_mailbox_id("INBOX"));
	String_T *search_keys = _build_search_keys(pool, "UID 1,* BODY the", &size);
	dbmail_mailbox_build_imap_search(mb, search_keys, &idx, sorted);
	dbmail_mailbox_search(mb);
	dbmail_mailbox_free(mb);
	mempool_push(pool, search_keys, size);
	mempool_close(&pool);
}
END_TEST

START_TEST(test_dbmail_mailbox_orderedsubject)
{
	char *res;
	uint64_t idx = 0;
	size_t size;
	String_T *search_keys;
	Mempool_T pool = mempool_open();
	DbmailMailbox *mb = dbmail_mailbox_new(pool, get_mailbox_id("INBOX"));
	
	search_keys = _build_search_keys(pool, "HEADER FROM foo.org ( SINCE 1-Jan-2005 )", &size);

	dbmail_mailbox_build_imap_search(mb, search_keys, &idx, 0);
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
	mempool_push(pool, search_keys, size);
	mempool_close(&pool);

}
END_TEST
START_TEST(test_dbmail_mailbox_get_set)
{
	guint c, d, r;
	GTree *set;
	DbmailMailbox *mb = dbmail_mailbox_new(NULL, get_mailbox_id("INBOX"));
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
	
	set = dbmail_mailbox_get_set(mb,"*,1",0);
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
	g_tree_destroy(set);

	set = dbmail_mailbox_get_set(mb, "999999998:999999999", 0);
	fail_unless(set != NULL,"dbmail_mailbox_get_set failed");
	c = g_tree_nnodes(set);
	fail_unless(c==0, "dbmail_mailbox_get_set failed [%d]", c);
	g_tree_destroy(set);
	
	// UID sets
	char *s, *t;

	set = dbmail_mailbox_get_set(mb, "0", 1);
	fail_unless(set == NULL);
	g_tree_destroy(set);

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
	
	set = dbmail_mailbox_get_set(mb, "999999998:999999999", 1);
	fail_unless(set != NULL,"dbmail_mailbox_get_set failed");
	c = g_tree_nnodes(set);
	fail_unless(c==0, "dbmail_mailbox_get_set failed [%d]", c);
	g_tree_destroy(set);
	
	dbmail_mailbox_free(mb);

	// empty box
	mb = dbmail_mailbox_new(NULL, empty_box);
	dbmail_mailbox_open(mb);

	set = dbmail_mailbox_get_set(mb, "1:*", 0);
	fail_unless(set == NULL,"dbmail_mailbox_get_set failed");
	g_tree_destroy(set);
	
	set = dbmail_mailbox_get_set(mb, "*", 0);
	fail_unless(set == NULL,"dbmail_mailbox_get_set failed");
	g_tree_destroy(set);

	set = dbmail_mailbox_get_set(mb, "1", 0);
	fail_unless(set == NULL,"dbmail_mailbox_get_set failed");
	g_tree_destroy(set);

	set = dbmail_mailbox_get_set(mb, "1:*", 1);
	fail_unless(set != NULL);
	d = g_tree_nnodes(set);
	fail_unless(d==1, "expected 1, got %d", d);
	g_tree_destroy(set);

	set = dbmail_mailbox_get_set(mb, "*:1", 1);
	fail_unless(set != NULL);
	d = g_tree_nnodes(set);
	fail_unless(d==1, "expected 1, got %d", d);
	g_tree_destroy(set);

	set = dbmail_mailbox_get_set(mb, "1234567", 1);
	fail_unless(set != NULL,"dbmail_mailbox_get_set failed");
	g_tree_destroy(set);

	set = dbmail_mailbox_get_set(mb, "1:1", 1);
	fail_unless(set != NULL,"dbmail_mailbox_get_set failed");
	g_tree_destroy(set);

	set = dbmail_mailbox_get_set(mb, "1:a*", 1);
	fail_unless(set == NULL,"dbmail_mailbox_get_set failed");
	g_tree_destroy(set);
	
	set = dbmail_mailbox_get_set(mb, "a:*", 1);
	fail_unless(set == NULL,"dbmail_mailbox_get_set failed");
	g_tree_destroy(set);

	set = dbmail_mailbox_get_set(mb, "*:a", 1);
	fail_unless(set == NULL,"dbmail_mailbox_get_set failed");
	g_tree_destroy(set);

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
	g_mime_init();
	srunner_run_all(sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	g_mime_shutdown();
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
	

