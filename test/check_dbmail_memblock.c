/*
 *  Copyright (c) 2008-2012 NFG Net Facilities Group BV support@nfg.nl
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
 *   Basic unit-test framework for dbmail (www.dbmail.org)
 *
 *   See http://check.sf.net for details and docs.
 *
 *   Run 'make check' to see some action.
 *
 */ 

#include <check.h>
#include "check_dbmail.h"
#include "dm_memblock.h"

/*
 *
 * the test fixtures
 *
 */
void setup(void)
{
	configure_debug(255,0);
	config_read(configFile);
}

void teardown(void)
{
	config_free();
}

START_TEST(test_mem_open) 
{
	Mem_T M = Mem_open();
	fail_unless(M != NULL);
	Mem_close(&M);
	fail_unless(M == NULL);
}
END_TEST

#define DATASIZE 64*1024
START_TEST(test_mem_write)
{
	char indata[100];
	char *instr = "abcdefghijklmnopqrstuvwxyz";
	char out[4096];
	Mem_T M;

	memset(indata, 'a', sizeof(indata));

	M = Mem_open();
	Mem_write(M,indata,100);
	Mem_write(M,indata,100);
	Mem_close(&M);

	M = Mem_open();
	Mem_write(M, instr, 20);

	memset(out, 0, sizeof(out));
	Mem_read(M, out, 10);
	fail_unless(MATCH(out,"abcdefghij"), "Mem_read failed");

	memset(out, 0, sizeof(out));
	Mem_read(M, out, 2);
	fail_unless(MATCH(out,"kl"), "Mem_read failed");

	memset(out, 0, sizeof(out));
	Mem_read(M, out, 2);
	fail_unless(MATCH(out,"mn"), "Mem_read failed");
	
	Mem_close(&M);
}
END_TEST

START_TEST(test_mem_read)
{
	char indata[DATASIZE+1];
	char outdata[DATASIZE+1];
	int l;

	memset(indata, '\0', sizeof(indata));
	memset(outdata, '\0', sizeof(outdata));
	memset(indata, 'a', DATASIZE);

	Mem_T M = Mem_open();
	Mem_write(M,indata,DATASIZE);
	Mem_rewind(M);
	l = Mem_read(M,outdata,DATASIZE);

	fail_unless(l==DATASIZE, "Mem_read failed: %d != %d", l, DATASIZE);
	fail_unless(strcmp(indata,outdata)==0,"Mem_read failed\n[%s]\n[%s]\n", indata, outdata);

	Mem_close(&M);
}
END_TEST

Suite *dbmail_memblock_suite(void)
{
	Suite *s = suite_create("Dbmail Memblock");
	TCase *tc_memblock = tcase_create("Memblock");
	
	suite_add_tcase(s, tc_memblock);
	
	tcase_add_checked_fixture(tc_memblock, setup, teardown);
	tcase_add_test(tc_memblock, test_mem_open);
	tcase_add_test(tc_memblock, test_mem_write);
	tcase_add_test(tc_memblock, test_mem_read);
	
	return s;
}

int main(void)
{
	int nf;
	Suite *s = dbmail_memblock_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

