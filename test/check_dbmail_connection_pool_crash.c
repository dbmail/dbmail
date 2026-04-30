#include <check.h>
#include <pthread.h>
#include <signal.h>
#include "check_dbmail.h"

extern char configFile[PATH_MAX];

/*
 * Number of threads competing for connections simultaneously.
 * Matches the max_db_connections=20 setting from the production crash scenario.
 */
#define TEST_THREAD_COUNT 20

static pthread_barrier_t all_threads_ready;

/*
 *
 * the test fixtures
 *
 */
void setup(void)
{
	config_get_file();
	config_read(configFile);
	configure_debug(NULL, 255, 0);
	GetDBParams();
	db_connect();
}

void teardown(void)
{
}

/* Thread entry: synchronizes at the barrier, then requests a connection. */
static void *get_connection_thread(void *arg)
{
	(void)arg;
	pthread_barrier_wait(&all_threads_ready);
	db_con_get();
	return NULL;
}

/*
 * Verify that stopping the pool while threads are waiting crashes unpatched
 * DBMail (SIGSEGV in ConnectionPool_getConnection(NULL)).
 *
 * Requires CK_FORK=yes (the default).
 */
START_TEST(test_db_pool_crashes_on_stopped_pool)
{
	pthread_t threads[TEST_THREAD_COUNT];
	int i;

	pthread_barrier_init(&all_threads_ready, NULL, TEST_THREAD_COUNT + 1);

	for (i = 0; i < TEST_THREAD_COUNT; i++)
		pthread_create(&threads[i], NULL, get_connection_thread, NULL);

	/* Stop the pool, then release all threads at once.
	 * Reproduces a race where threads try to get a connection
	 * from a stopped pool.
	 */
	db_disconnect();
	pthread_barrier_wait(&all_threads_ready);

	for (i = 0; i < TEST_THREAD_COUNT; i++)
		pthread_join(threads[i], NULL);
}
END_TEST

Suite *dbmail_connection_pool_crash_suite(void)
{
	Suite *s = suite_create("Dbmail Connection Pool Crash");
	TCase *tc = tcase_create("ConnectionPoolCrash");

	suite_add_tcase(s, tc);

	tcase_add_checked_fixture(tc, setup, teardown);
	tcase_set_timeout(tc, 10);
	tcase_add_test_raise_signal(tc, test_db_pool_crashes_on_stopped_pool, SIGSEGV);

	return s;
}

int main(void)
{
	int nf;
	Suite *s = dbmail_connection_pool_crash_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_ENV);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
