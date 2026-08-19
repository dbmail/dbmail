#include <check.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <openssl/ssl.h>
#include "check_dbmail.h"

extern char configFile[PATH_MAX];
extern ServerConfig_T *server_conf;

/*
 * ci_close() must close the client descriptor exactly once: for network
 * clients rx and tx hold the same fd, and a second close() can destroy a
 * descriptor the kernel has already handed to another thread. libdbmail
 * is a shared library, so close() is counted by interposition: the
 * definition below shadows libc's close for the whole process (the
 * dynamic linker resolves the library's calls through the executable),
 * and forwards to the kernel directly.
 */
#define MAX_TRACKED_FD 1024
static int close_calls[MAX_TRACKED_FD];

int close(int fd)
{
	if (fd >= 0 && fd < MAX_TRACKED_FD)
		close_calls[fd]++;
	return syscall(SYS_close, fd);
}

void setup(void)
{
	config_get_file();
	config_read(configFile);
	configure_debug(NULL, 0, 0);
	server_conf = g_new0(ServerConfig_T, 1);
	memset(close_calls, 0, sizeof(close_calls));
}

void teardown(void)
{
	g_free(server_conf);
	server_conf = NULL;
}

START_TEST(test_ci_close_socket_once)
{
	int sv[2];
	int fd;
	int rc;
	int fcntl_errno;
	socklen_t len;
	Mempool_T pool;
	client_sock *c;
	ClientBase_T *client;

	ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
	fd = sv[0];
	ck_assert_int_lt(fd, MAX_TRACKED_FD);

	/* wrap the connected fd into a client_sock the way the server's
	 * accept path (_sock_cb) does */
	pool = mempool_open();
	c = mempool_pop(pool, sizeof(client_sock));
	c->pool = pool;
	c->sock = fd;

	len = sizeof(struct sockaddr);
	ck_assert_int_eq(getpeername(fd, &c->caddr, &len), 0);
	c->caddr_len = len;

	len = sizeof(struct sockaddr);
	ck_assert_int_eq(getsockname(fd, &c->saddr, &len), 0);
	c->saddr_len = len;

	client = client_init(c);
	ck_assert_ptr_ne(client, NULL);
	ck_assert_int_eq(client->rx, fd);
	ck_assert_int_eq(client->rx, client->tx);

	ci_close(client);

	ck_assert_int_eq(close_calls[fd], 1);
	/* and the descriptor is really gone */
	rc = fcntl(fd, F_GETFD);
	fcntl_errno = errno;
	ck_assert_int_eq(rc, -1);
	ck_assert_int_eq(fcntl_errno, EBADF);

	close(sv[1]);
	mempool_close(&pool);
}
END_TEST

START_TEST(test_ci_close_stdin_stdout)
{
	int save_in, save_out;
	Mempool_T pool;
	client_sock *c;
	ClientBase_T *client;

	pool = mempool_open();
	c = mempool_pop(pool, sizeof(client_sock));
	c->pool = pool;
	/* c->caddr_len stays 0: client_init() switches to the distinct
	 * stdin/stdout descriptors used by the cli server and dbmail-export,
	 * and ci_close() must close both of them exactly once */

	client = client_init(c);
	ck_assert_ptr_ne(client, NULL);
	ck_assert_int_eq(client->rx, STDIN_FILENO);
	ck_assert_int_eq(client->tx, STDOUT_FILENO);

	save_in = dup(STDIN_FILENO);
	save_out = dup(STDOUT_FILENO);
	ck_assert_int_ge(save_in, 0);
	ck_assert_int_ge(save_out, 0);

	ci_close(client);

	dup2(save_in, STDIN_FILENO);
	dup2(save_out, STDOUT_FILENO);
	close(save_in);
	close(save_out);

	ck_assert_int_eq(close_calls[STDIN_FILENO], 1);
	ck_assert_int_eq(close_calls[STDOUT_FILENO], 1);

	mempool_close(&pool);
}
END_TEST

START_TEST(test_ci_close_tls_socket_once)
{
	int sv[2];
	int fd;
	socklen_t len;
	Mempool_T pool;
	client_sock *c;
	ClientBase_T *client;
	SSL_CTX *ctx;

	ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
	fd = sv[0];
	ck_assert_int_lt(fd, MAX_TRACKED_FD);

	pool = mempool_open();
	c = mempool_pop(pool, sizeof(client_sock));
	c->pool = pool;
	c->sock = fd;

	len = sizeof(struct sockaddr);
	ck_assert_int_eq(getpeername(fd, &c->caddr, &len), 0);
	c->caddr_len = len;

	len = sizeof(struct sockaddr);
	ck_assert_int_eq(getsockname(fd, &c->saddr, &len), 0);
	c->saddr_len = len;

	client = client_init(c);
	ck_assert_ptr_ne(client, NULL);

	/* attach a TLS session the way tls_setup() does: SSL_set_fd() builds
	 * the socket BIO with BIO_NOCLOSE, so SSL_free() must not close the
	 * descriptor - this test fails if that premise ever changes */
	ctx = SSL_CTX_new(TLS_server_method());
	ck_assert_ptr_ne(ctx, NULL);
	client->sock->ssl = SSL_new(ctx);
	ck_assert_ptr_ne(client->sock->ssl, NULL);
	ck_assert_int_eq(SSL_set_fd(client->sock->ssl, fd), 1);

	ci_close(client);

	ck_assert_int_eq(close_calls[fd], 1);

	SSL_CTX_free(ctx);
	close(sv[1]);
	mempool_close(&pool);
}
END_TEST

Suite *dbmail_clientbase_suite(void)
{
	Suite *s = suite_create("Dbmail Clientbase");
	TCase *tc = tcase_create("CiClose");

	suite_add_tcase(s, tc);

	tcase_add_checked_fixture(tc, setup, teardown);
	tcase_set_timeout(tc, 10);
	tcase_add_test(tc, test_ci_close_socket_once);
	tcase_add_test(tc, test_ci_close_stdin_stdout);
	tcase_add_test(tc, test_ci_close_tls_socket_once);

	return s;
}

int main(void)
{
	int nf;
	Suite *s = dbmail_clientbase_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all(sr, CK_ENV);
	nf = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (nf == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
