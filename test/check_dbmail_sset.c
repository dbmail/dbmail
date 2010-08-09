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

#include <sys/times.h>
#include <time.h>
#include <stdio.h>

extern char *configFile;

/*
 *
 * the test fixtures
 *
 */
static clock_t st_time;
static clock_t en_time;
static struct tms st_cpu;
static struct tms en_cpu;

static void start_clock(void)
{
	st_time = times(&st_cpu);
}

static void end_clock(char *msg)
{
	en_time = times(&en_cpu);
	printf("%sReal Time: %LF, User Time %LF, System Time %LF\n",
			msg,
			(long double)(en_time - st_time),
			(long double)(en_cpu.tms_utime - st_cpu.tms_utime),
			(long double)(en_cpu.tms_stime - st_cpu.tms_stime));
}

Sset_T V;


struct item {
	int key;
	long long int value;
};

static int compare(const void *a, const void *b)
{
	struct item *x, *y;
	x = (struct item *)a;
	y = (struct item *)b;

	if (x->key>y->key)
		return 1;
	if (x->key==y->key)
		return 0;
	return -1;
}

void setup(void)
{
	configure_debug(255,0);
	config_read(configFile);
	V = Sset_new(compare);
}

void teardown(void)
{
	Sset_free(&V);
}

START_TEST(test_sset_add)
{
	int i, n = 1000000;
	struct item p, *t, *k;
       
	start_clock();
	k = malloc(sizeof(struct item) * n);
	end_clock("malloc: ");

	t = k;
	start_clock();
	for (i = 0; i<n; i++, t++) {
		t->key = i;
	        t->value = rand();
		Sset_add(V, (void *)t);
	}
	end_clock("Sset_add: ");

	memset(&p, 0, sizeof(struct item));
	start_clock();
	for (i = 0; i<n; i++, t++) {
		p.key = i;
		Sset_del(V, (void *)&t);
	}
	end_clock("Sset_del: ");


	free(k);
}
END_TEST

Suite *dbmail_sset_suite(void)
{
	Suite *s = suite_create("Dbmail Sset");
	TCase *tc_sset = tcase_create("Sset");
	
	suite_add_tcase(s, tc_sset);
	
	tcase_add_checked_fixture(tc_sset, setup, teardown);
	tcase_add_test(tc_sset, test_sset_add);

	return s;
}

int main(void)
{
	int nf;
	Suite *s = dbmail_sset_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_NORMAL);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);
	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
	

