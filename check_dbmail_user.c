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
 *  $Id: check_dbmail_common.c 1598 2005-02-23 08:41:02Z paul $ 
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

#include <stdlib.h>
#include <check.h>
#include <gmime/gmime.h>
#include <stdio.h>
#include <string.h>

#include "dbmail-imapsession.h"
#include "dbmail-message.h"
#include "mime.h"
#include "rfcmsg.h"
#include "dbmsgbuf.h"
#include "imaputil.h"
#include "config.h"
#include "pipe.h"

#include "check_dbmail.h"

extern char *configFile;
extern db_param_t _db_params;


/*
 *
 * the test fixtures
 *
 */
void setup(void)
{
	configure_debug(4,0,1);
	config_read(configFile);
	GetDBParams(&_db_params);
	db_connect();
}

void teardown(void)
{
	db_disconnect();
}

//int do_add(const char * const user,
//           const char * const password,
//           const char * const enctype,
//           const u64_t maxmail, const u64_t clientid,
//	   GList * alias_add,
//	   GList * alias_del);

START_TEST(test_do_add)
{

}
END_TEST

//int do_delete(const u64_t useridnr, const char * const user);
//int do_show(const char * const user);
//int do_empty(const u64_t useridnr);
/* Change operations */
//int do_username(const u64_t useridnr, const char *newuser);
//int do_maxmail(const u64_t useridnr, const u64_t maxmail);
//int do_clientid(const u64_t useridnr, const u64_t clientid);
//int do_password(const u64_t useridnr,
//                const char * const password,
//                const char * const enctype);
//int do_aliases(const u64_t useridnr,
//               GList * alias_add,
//               GList * alias_del);
/* External forwards */
//int do_forwards(const char *alias, const u64_t clientid,
//                GList * fwds_add,
//                GList * fwds_del);

/* Helper functions */
int is_valid(const char * const str);
u64_t strtomaxmail(const char * const str);


Suite *dbmail_common_suite(void)
{
	Suite *s = suite_create("Dbmail User");
	TCase *tc_user = tcase_create("User");
	
	suite_add_tcase(s, tc_user);
	
	tcase_add_checked_fixture(tc_user, setup, teardown);
	tcase_add_test(tc_user, test_do_add);
	
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
	suite_free(s);
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
	

