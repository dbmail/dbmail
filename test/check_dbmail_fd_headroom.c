#include <check.h>
#include <sys/resource.h>
#include "check_dbmail.h"

/*
 * check_fd_headroom() implements the guard that refuses a new client
 * connection when the process is about to run out of file descriptors.
 * The soft RLIMIT_NOFILE limit can be lowered without privileges, so
 * the test drives the guard through its live states with setrlimit().
 */

START_TEST(test_check_fd_headroom)
{
	struct rlimit orig, rl;
	int fd_count = 0;
	unsigned long fd_limit = 0;

	ck_assert_int_eq(getrlimit(RLIMIT_NOFILE, &orig), 0);

	/* the real limit leaves plenty of headroom */
	ck_assert_int_eq(check_fd_headroom(FREE_DF_THRESHOLD, &fd_count, &fd_limit), 1);
	ck_assert_int_gt(fd_count, 0);
	ck_assert_uint_eq(fd_limit, (unsigned long) orig.rlim_cur);

	/* one descriptor short of the threshold: the guard must fire */
	rl = orig;
	rl.rlim_cur = fd_count + FREE_DF_THRESHOLD - 1;
	ck_assert_int_eq(setrlimit(RLIMIT_NOFILE, &rl), 0);
	ck_assert_int_eq(check_fd_headroom(FREE_DF_THRESHOLD, &fd_count, &fd_limit), 0);
	ck_assert_uint_eq(fd_limit, (unsigned long) rl.rlim_cur);

	/* exactly the threshold free: still enough */
	rl.rlim_cur = fd_count + FREE_DF_THRESHOLD;
	ck_assert_int_eq(setrlimit(RLIMIT_NOFILE, &rl), 0);
	ck_assert_int_eq(check_fd_headroom(FREE_DF_THRESHOLD, &fd_count, &fd_limit), 1);

	ck_assert_int_eq(setrlimit(RLIMIT_NOFILE, &orig), 0);
}
END_TEST

Suite *dbmail_fd_headroom_suite(void)
{
	Suite *s = suite_create("Dbmail FdHeadroom");
	TCase *tc = tcase_create("FdHeadroom");

	suite_add_tcase(s, tc);

	tcase_set_timeout(tc, 10);
	tcase_add_test(tc, test_check_fd_headroom);

	return s;
}

int main(void)
{
	int nf;
	Suite *s = dbmail_fd_headroom_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_ENV);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
