/*
 *   Copyright (c) 2005-2013 NFG Net Facilities Group BV support@nfg.nl
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
#include "check_dbmail.h"
#include "dm_debug.h"

#include <sys/times.h>
#include <time.h>
#include <stdio.h>

extern char configFile[PATH_MAX];

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

static void end_clock(char * msg UNUSED)
{
	en_time = times(&en_cpu);
	/*
	printf("%sReal Time: %LF, User Time %LF, System Time %LF\n",
			msg,
			(long double)(en_time - st_time),
			(long double)(en_cpu.tms_utime - st_cpu.tms_utime),
			(long double)(en_cpu.tms_stime - st_cpu.tms_stime));
			*/
}

Sset_T V, W;


struct item {
	long long int key;
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
	config_get_file();
	config_read(configFile);
	configure_debug(NULL,255,0);
	V = Sset_new(compare, sizeof(struct item), NULL);
	W = Sset_new(compare, sizeof(struct item), NULL);
}

void teardown(void)
{
	Sset_free(&V);
	Sset_free(&W);
}

START_TEST(test_sset_add)
{
	int i, n = 10000;
	struct item *t, *k;
       
	start_clock();
	t = k = malloc(sizeof(struct item) * n);

	start_clock();
	for (i = 0; i<n; i++, t++) {
		t->key = i;
	        t->value = rand();
		Sset_add(V, (void *)t);
	}
	end_clock("Sset_add: ");

	fail_unless(Sset_len(V) == n, "sset holds: [%d]:", Sset_len(V));
	free(k);
}
END_TEST

START_TEST(test_sset_del)
{
	int i, n = 10000;
	struct item p, *t, *k;
       
	t = k = malloc(sizeof(struct item) * n);

	for (i = 0; i<n; i++, t++) {
		t->key = i;
	        t->value = rand();
		Sset_add(V, (void *)t);
	}

	fail_unless(Sset_len(V) == n);

	memset(&p, 0, sizeof(struct item));
	start_clock();
	for (i = 0; i<n; i++) {
		p.key = i;
		Sset_del(V, (void *)&p);
	}
	end_clock("Sset_del: ");

	fail_unless(Sset_len(V) == 0, "sset still holds: [%d]:", Sset_len(V));
	free(k);
}
END_TEST

START_TEST(test_sset_or)
{
	long long int i, n = 10000;
	struct item *t, *k, *l;
       
	t = k = malloc(sizeof(struct item) * n);

	for (i = 0; i<n; i+=2, t++) {
		t->key = i;
	        t->value = i;
		Sset_add(V, (void *)t);
	}

	t = l = malloc(sizeof(struct item) * n);
	for (i = 1; i<=n; i+=2, t++) {
		t->key = i;
	        t->value = i;
		Sset_add(W, (void *)t);
	}
 
	start_clock();
	Sset_T p = Sset_or(V, W);
	end_clock("Sset_or: ");

	fail_unless(Sset_len(p) == Sset_len(V) + Sset_len(W), "Sset_or failed");

	Sset_free(&p);

	free(k);
	free(l);
}
END_TEST

START_TEST(test_sset_and1)
{
	long long int i, n = 10000;
	struct item *t, *k, *l;
       
	k = malloc(sizeof(struct item) * n);
	l = malloc(sizeof(struct item) * n);

	for (t = k, i = 0; i<n; i+=2, t++) {
		t->key = i;
	        t->value = i;
		Sset_add(V, (void *)t);
	}

	for (t = l, i = 1; i<=n; i+=2, t++) {
		t->key = i;
	        t->value = i;
		Sset_add(W, (void *)t);
	}
 
	start_clock();
	Sset_T p = Sset_and(V, W);
	end_clock("Sset_and1: ");

	fail_unless(Sset_len(p) == 0, "Sset_and1 failed");

	Sset_free(&p);

	free(k);
	free(l);
}
END_TEST

START_TEST(test_sset_and2)
{
	long long int i, n = 10000;
	struct item *t, *k, *l;
       
	k = malloc(sizeof(struct item) * n);
	l = malloc(sizeof(struct item) * n);

	for (t = k, i = 0; i<n; i++, t++) {
		t->key = i;
	        t->value = i;
		Sset_add(V, (void *)t);
	}

	for (t = l, i = 0; i<n; i++, t++) {
		t->key = i;
	        t->value = i;
		Sset_add(W, (void *)t);
	}
 
	start_clock();
	Sset_T p = Sset_and(V, W);
	end_clock("Sset_and2: ");

	fail_unless(Sset_len(p) == n, "Sset_and2 failed");

	Sset_free(&p);

	free(k);
	free(l);
}
END_TEST

START_TEST(test_sset_not)
{
	long long int i, n = 10000;
	struct item *t, *k, *l;
       
	k = malloc(sizeof(struct item) * n);
	l = malloc(sizeof(struct item) * n);

	for (t = k, i = 0; i<n; i+=2, t++) {
		t->key = i;
	        t->value = i;
		Sset_add(V, (void *)t);
	}

	for (t = l, i = 1; i<=n; i+=2, t++) {
		t->key = i;
	        t->value = i;
		Sset_add(W, (void *)t);
	}
 
	start_clock();
	Sset_T p = Sset_not(V, W);
	end_clock("Sset_not: ");

	fail_unless(Sset_len(p) == Sset_len(V), "Sset_not failed");

	Sset_free(&p);

	free(k);
	free(l);
}
END_TEST

START_TEST(test_sset_xor)
{
	long long int i, n = 10000;
	struct item *t, *k, *l;
       
	k = malloc(sizeof(struct item) * n);
	l = malloc(sizeof(struct item) * n);

	for (t = k, i = 0; i<n; i+=2, t++) {
		t->key = i;
	        t->value = i;
		Sset_add(V, (void *)t);
	}

	for (t = l, i = 1; i<=n; i+=2, t++) {
		t->key = i;
	        t->value = i;
		Sset_add(W, (void *)t);
	}
 
	start_clock();
	Sset_T p = Sset_xor(V, W);
	end_clock("Sset_xor: ");

	fail_unless(Sset_len(p) == n, "Sset_xor failed");

	Sset_free(&p);

	free(k);
	free(l);
}
END_TEST



Suite *dbmail_sset_suite(void)
{
	Suite *s = suite_create("Dbmail Sset");
	TCase *tc_sset = tcase_create("Sset");
	
	suite_add_tcase(s, tc_sset);
	
	tcase_add_checked_fixture(tc_sset, setup, teardown);
	tcase_add_test(tc_sset, test_sset_add);
	tcase_add_test(tc_sset, test_sset_del);
	tcase_add_test(tc_sset, test_sset_or);
	tcase_add_test(tc_sset, test_sset_and1);
	tcase_add_test(tc_sset, test_sset_and2);
	tcase_add_test(tc_sset, test_sset_not);
	tcase_add_test(tc_sset, test_sset_xor);
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
	

