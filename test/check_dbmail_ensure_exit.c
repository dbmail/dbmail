#include <check.h>
#include <sysexits.h>
#include "dbmail.h"

static GThreadPool *pool = NULL;

/*
 * atexit handler mirroring disconnect_all() in src/server.c: it waits for the
 * jobs still running in the pool.
 */
static void free_pool(void)
{
	if (pool) {
		g_thread_pool_free(pool, TRUE, TRUE);
		pool = NULL;
	}
}

/* Pool job: ends the process from a worker thread. */
static void exit_job(gpointer data, gpointer user_data)
{
	(void)data;
	(void)user_data;
	dm_ensure_exit(EX_TEMPFAIL);
}

/* Exits at once, so reaching the atexit chain is observable as EX_OK. */
static void atexit_marker(void)
{
	_exit(EX_OK);
}

/*
 * Verify that a worker thread can end the process while an atexit handler
 * joins the pool that worker runs in. Calling exit() there deadlocks: the join
 * waits for the calling job to finish, so the test times out instead of
 * exiting, and every other thread blocks on the exit handlers.
 *
 * Requires CK_FORK=yes (the default) because the test exits the process.
 */
START_TEST(test_dm_ensure_exit_from_pool_worker)
{
	ck_assert_int_eq(atexit(free_pool), 0);

	pool = g_thread_pool_new(exit_job, NULL, 1, FALSE, NULL);
	ck_assert_ptr_ne(pool, NULL);
	ck_assert_int_eq(g_thread_pool_push(pool, GINT_TO_POINTER(1), NULL), TRUE);

	/* The job ends the process; if it does not, the tcase timeout fails
	 * the test.
	 */
	pause();
}
END_TEST

/*
 * Verify that the main thread still runs the atexit handlers, so the ordinary
 * shutdown keeps working. Skipping them there would leave the process with
 * EX_TEMPFAIL instead.
 */
START_TEST(test_dm_ensure_exit_runs_atexit_on_main_thread)
{
	ck_assert_int_eq(atexit(atexit_marker), 0);

	dm_ensure_exit(EX_TEMPFAIL);
}
END_TEST

Suite *dbmail_ensure_exit_suite(void)
{
	Suite *s = suite_create("Dbmail Ensure Exit");
	TCase *tc = tcase_create("EnsureExit");

	suite_add_tcase(s, tc);

	tcase_set_timeout(tc, 10);
	tcase_add_exit_test(tc, test_dm_ensure_exit_from_pool_worker, EX_TEMPFAIL);
	tcase_add_exit_test(tc, test_dm_ensure_exit_runs_atexit_on_main_thread, EX_OK);

	return s;
}

int main(void)
{
	int nf;
	Suite *s = dbmail_ensure_exit_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_ENV);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
