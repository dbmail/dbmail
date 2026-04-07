#include <check.h>
#include <pthread.h>
#include <sysexits.h>
#include "check_dbmail.h"

extern char configFile[PATH_MAX];
extern DBParam_T db_params;

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
	configure_debug(NULL, 511, 0);
	GetDBParams();
	db_connect();
	db_params.connection_pool_timeout = 2;
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
 * Verify db_con_get() behaviour when the pool is stopped while multiple
 * threads are competing for connections:
 *
 * Requires CK_FORK=yes (the default) because the test exits the process.
 */
START_TEST(test_db_pool_exits_after_timeout)
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

Suite *dbmail_connection_pool_suite(void)
{
	Suite *s = suite_create("Dbmail Connection Pool");
	TCase *tc = tcase_create("ConnectionPool");

	suite_add_tcase(s, tc);

	tcase_add_checked_fixture(tc, setup, teardown);
	tcase_set_timeout(tc, 10);
	tcase_add_exit_test(tc, test_db_pool_exits_after_timeout, EX_TEMPFAIL);

	return s;
}

int main(void)
{
	int nf;
	Suite *s = dbmail_connection_pool_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_ENV);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
