#include <check.h>
#include <sysexits.h>
#include "dbmail.h"

extern char configFile[PATH_MAX];

/*
 * tpool and disconnect_all() are non-static globals in src/server.c; they are
 * simply not declared in server.h.
 */
extern GThreadPool *tpool;
extern void disconnect_all(void);

/* Number of queries the job runs while holding its connection. */
#define JOB_QUERIES 10

static volatile int holding = 0;

void setup(void)
{
	config_get_file();
	config_read(configFile);
	configure_debug(NULL, 0, 0);
	GetDBParams();
	db_connect();
}

void teardown(void)
{
}

/*
 * Pool job: takes a connection and keeps using it, the way an IMAP command
 * holds one for the whole duration of a query.
 */
static void job(gpointer data, gpointer user_data)
{
	Connection_T c;
	int i;

	(void)data;
	(void)user_data;

	c = db_con_get();
	holding = 1;

	for (i = 0; i < JOB_QUERIES; i++) {
		usleep(100000);
		db_query(c, "SELECT %d", i);
	}

	db_con_close(c);
}

/*
 * Verify that disconnect_all() waits for the running pool jobs before it
 * releases what those jobs are using.
 *
 * The job holds a connection for about a second. If disconnect_all() calls
 * db_disconnect() first, the connection pool is freed underneath the job and
 * it dies in libzdb with SIGSEGV, failing this test.
 */
START_TEST(test_disconnect_all_waits_for_pool_jobs)
{
	tpool = g_thread_pool_new(job, NULL, 1, FALSE, NULL);
	ck_assert_ptr_ne(tpool, NULL);
	ck_assert_int_eq(g_thread_pool_push(tpool, GINT_TO_POINTER(1), NULL), TRUE);

	while (! holding)
		usleep(10000);
	usleep(300000);		/* let the job get into its query loop */

	disconnect_all();
}
END_TEST

Suite *dbmail_disconnect_all_suite(void)
{
	Suite *s = suite_create("Dbmail Disconnect All");
	TCase *tc = tcase_create("DisconnectAll");

	suite_add_tcase(s, tc);

	tcase_add_checked_fixture(tc, setup, teardown);
	tcase_set_timeout(tc, 20);
	tcase_add_test(tc, test_disconnect_all_waits_for_pool_jobs);

	return s;
}

int main(void)
{
	int nf;
	Suite *s = dbmail_disconnect_all_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_ENV);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
