/*
 *  Copyright (C) 2006  Aaron Stone  <aaron@serendipity.cx>
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
	
