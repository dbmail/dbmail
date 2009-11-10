/*
 *   Copyright (c) 2005-2006 NFG Net Facilities Group BV support@nfg.nl
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
void setup(void)
{
	configure_debug(5,0);
}

void teardown(void)
{
	;
}

#define M(a,b) fail_unless(MATCH(dm_md5((a)),(b)), "dm_md5 failed [%s] != [%s]", dm_md5(a), b)
START_TEST(test_md5)
{
        M("","d41d8cd98f00b204e9800998ecf8427e");
        M("a","0cc175b9c0f1b6a831c399e269772661");
        M("abc", "900150983cd24fb0d6963f7d28e17f72");
}
END_TEST

#define B(a,b) fail_unless(MATCH(dm_md5_base64((a)),(b)), "dm_md5_base64 failed [%s] != [%s]", dm_md5(a), b)
START_TEST(test_md5_base64)
{
	B("","1B2M2Y8AsgTpgAmY7PhCfg==");
	B("a","DMF1ucDxtqgxw5niaXcmYQ==");
	B("abc","kAFQmDzST7DWlj99KOF/cg==");
}
END_TEST

Suite *dbmail_common_suite(void)
{
	Suite *s = suite_create("Dbmail md5");
	TCase *tc_util = tcase_create("Md5");
	
	suite_add_tcase(s, tc_util);
	
	tcase_add_checked_fixture(tc_util, setup, teardown);
	tcase_add_test(tc_util, test_md5);
	tcase_add_test(tc_util, test_md5_base64);

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
	

