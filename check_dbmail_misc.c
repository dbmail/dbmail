/*
 *   Copyright (C) 2006 Aaron Stone  <aaron@serendipity.cx>
 *   Copyright (c) 2006 NFG Net Facilities Group BV support@nfg.nl
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
 *  $Id: check_dbmail_dsn.c 1598 2005-02-23 08:41:02Z paul $ 
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

// Test find_bounded and zap_between.
#include <check.h>
#include "check_dbmail.h"

START_TEST(test_g_list_join)
{
	GString *result;
	GList *l = NULL;
	l = g_list_append(l, "NIL");
	l = g_list_append(l, "NIL");
	l = g_list_append(l, "(NIL NIL)");
	l = g_list_append(l, "(NIL NIL)");
	result = g_list_join(l," ");
	fail_unless(strcmp(result->str,"NIL NIL (NIL NIL) (NIL NIL)")==0,"g_list_join failed");
	g_string_free(result,TRUE);

	l = NULL;
	l = g_list_append(l, "NIL");
	result = g_list_join(l," ");
	fail_unless(strcmp(result->str,"NIL")==0,"g_list_join failed");
	g_string_free(result,TRUE);

}
END_TEST

START_TEST(test_g_string_split)
{
	GString *s = g_string_new("");
	GList *l = NULL;

	g_string_printf(s,"a b");
	l = g_string_split(s," ");

	fail_unless(g_list_length(l)==2,"g_string_split failed");
	g_list_destroy(l);

	g_string_printf(s,"a,b,c");
	l = g_string_split(s,",");

	fail_unless(g_list_length(l)==3,"g_string_split failed");
	g_list_destroy(l);

	l = g_string_split(s," ");

	fail_unless(g_list_length(l)==1,"g_string_split failed");
	g_list_destroy(l);

	g_string_free(s,TRUE);
}
END_TEST

START_TEST(test_g_tree_keys)
{
	GTree *a;
	GList *akeys;
	u64_t *k, *v;
	int i=0;
	
	a = g_tree_new_full((GCompareDataFunc)ucmp,NULL,(GDestroyNotify)g_free,(GDestroyNotify)g_free);
	akeys = g_tree_keys(a);

	fail_unless(g_tree_nnodes(a)==0,"g_tree_keys failed");
	fail_unless(g_list_length(akeys)==0,"g_tree_keys failed");

	g_list_free(akeys);
	
	for (i=0; i<4; i++) {
		k = g_new0(u64_t,1);
		v = g_new0(u64_t,1);
		*k = i;
		*v = i;
		g_tree_insert(a,k,v);
	}
	
	akeys = g_tree_keys(a);
	fail_unless(g_tree_nnodes(a)==4,"g_tree_keys failed");
	fail_unless(g_list_length(akeys)==4,"g_tree_keys failed");
	
	g_list_free(akeys);
	g_tree_destroy(a);
}
END_TEST


/*
 * boolean merge of two GTrees. The result is stored in GTree *a.
 * the state of GTree *b is undefined: it may or may not have been changed, 
 * depending on whether or not key/value pairs were moved from b to a.
 * Both trees are safe to destroy afterwards, assuming g_tree_new_full was used
 * for their construction.
 */
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
	
	for (r=1; r<40; r+=10) {
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


START_TEST(test_find_bounded)
{
	char *newaddress;
	size_t newaddress_len, last_pos;

	find_bounded("fail+success@failure", '+', '@',
			&newaddress, &newaddress_len, &last_pos);

	fail_unless(strcmp("success", newaddress)==0,
			"find_bounded is broken. "
			"Should be success: %s", newaddress);

	dm_free(newaddress);
}
END_TEST

START_TEST(test_zap_between_both)
{
	char *newaddress;
	size_t newaddress_len, zapped_len;

	zap_between("suc+failure@cess", -'+', -'@',
			&newaddress, &newaddress_len, &zapped_len);

	fail_unless(strcmp("success", newaddress)==0,
			"zap_between is both broken. "
			"Should be success: %s", newaddress);

	dm_free(newaddress);
}
END_TEST

START_TEST(test_zap_between_left)
{
	char *newaddress;
	size_t newaddress_len, zapped_len;

	zap_between("suc+failure@cess", -'+', '@',
			&newaddress, &newaddress_len, &zapped_len);

	fail_unless(strcmp("suc@cess", newaddress)==0,
			"zap_between is left broken. "
			"Should be suc@cess: %s", newaddress);

	dm_free(newaddress);
}
END_TEST

START_TEST(test_zap_between_right)
{
	char *newaddress;
	size_t newaddress_len, zapped_len;

	zap_between("suc+failure@cess", '+', -'@',
			&newaddress, &newaddress_len, &zapped_len);

	fail_unless(strcmp("suc+cess", newaddress)==0,
			"zap_between is right broken. "
			"Should be suc+cess: %s", newaddress);

	dm_free(newaddress);
}
END_TEST

START_TEST(test_zap_between_center)
{
	char *newaddress;
	size_t newaddress_len, zapped_len;

	zap_between("suc+failure@cess", '+', '@',
			&newaddress, &newaddress_len, &zapped_len);

	fail_unless(strcmp("suc+@cess", newaddress)==0,
			"zap_between is center broken. "
			"Should be suc+@cess: %s", newaddress);

	dm_free(newaddress);
}
END_TEST

void setup(void)
{
	configure_debug(5,0);
}

void teardown(void)
{
}

Suite *dbmail_misc_suite(void)
{
	Suite *s = suite_create("Dbmail Misc Functions");

	TCase *tc_misc = tcase_create("Misc");
	suite_add_tcase(s, tc_misc);
	tcase_add_checked_fixture(tc_misc, setup, teardown);
	tcase_add_test(tc_misc, test_g_list_join);
	tcase_add_test(tc_misc, test_g_string_split);
	tcase_add_test(tc_misc, test_g_tree_keys);
	tcase_add_test(tc_misc, test_g_tree_merge_or);
	tcase_add_test(tc_misc, test_g_tree_merge_and);
	tcase_add_test(tc_misc, test_g_tree_merge_not);
	tcase_add_test(tc_misc, test_zap_between_both);
	tcase_add_test(tc_misc, test_zap_between_left);
	tcase_add_test(tc_misc, test_zap_between_right);
	tcase_add_test(tc_misc, test_zap_between_center);
	tcase_add_test(tc_misc, test_find_bounded);
	return s;
}

int main(void)
{
	int nf;
	Suite *s = dbmail_misc_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
	
