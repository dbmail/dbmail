/*
  
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2004-2012 NFG Net Facilities Group BV support@nfg.nl

 This program is free software; you can redistribute it and/or 
 modify it under the terms of the GNU General Public License 
 as published by the Free Software Foundation; either 
 version 2 of the License, or (at your option) any later 
 version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/* 
 * server.c
 *
 */

#include "dbmail.h"
#include "dm_request.h"
#define THIS_MODULE "server"

static char *configFile = DEFAULT_CONFIG_FILE;

volatile sig_atomic_t mainRestart = 0;
volatile sig_atomic_t alarm_occurred = 0;

// thread data
int selfpipe[2];
GAsyncQueue *queue;
GThreadPool *tpool = NULL;

ServerConfig_T *server_conf;
extern DBParam_T db_params;

static void server_config_load(ServerConfig_T * conf, const char * const service);
static int server_set_sighandler(void);
void disconnect_all(void);

struct event_base *evbase = NULL;
struct event *sig_int, *sig_hup, *sig_pipe, *sig_term;

struct event *pev = NULL;
SSL_CTX *tls_context;

/* 
 *
 * threaded command primitives 
 *
 * the goal is to make long running tasks (mainly database IO) non-blocking
 *
 *
 */


/*
 * async queue drainage callback for the main thread
 */
void dm_queue_drain(int sock, short event UNUSED, void *arg UNUSED)
{
	char buf[128];
	gpointer data;

	event_del(pev);

	do {
		data = g_async_queue_try_pop(queue);
		if (data) {
			dm_thread_data *D = (gpointer)data;
			if (D->cb_leave) D->cb_leave(data);
			dm_thread_data_free(data);
		}
	} while (data);

	while ((read(sock, buf, 128)) > 0)
		;

	event_add(pev, NULL);
}

/* 
 * push a job to the thread pool
 *
 */

void dm_thread_data_push(gpointer session, gpointer cb_enter, gpointer cb_leave, gpointer data)
{
	GError *err = NULL;
	ImapSession *s;

	assert(session);
	assert(cb_enter);

	s = (ImapSession *)session;

	/* put a cork on the network IO */
	ci_cork(s->ci);

	if (s->state == CLIENTSTATE_QUIT_QUEUED)
		return;

	dm_thread_data *D = g_new0(dm_thread_data,1);
	D->cb_enter	= cb_enter;
	D->cb_leave     = cb_leave;
	D->session	= session;
	D->data         = data;

	// we're not done until we're done
	D->session->command_state = FALSE; 

	TRACE(TRACE_DEBUG,"[%p] [%p]", D, D->session);
	
	g_thread_pool_push(tpool, D, &err);
	TRACE(TRACE_INFO, "threads unused %u/%d limits %u/%d",
			g_thread_pool_get_num_unused_threads(),
			g_thread_pool_get_max_unused_threads(),
			g_thread_pool_get_num_threads(tpool),
			g_thread_pool_get_max_threads(tpool));

	if (err) TRACE(TRACE_EMERG,"g_thread_pool_push failed [%s]", err->message);
}

void dm_thread_data_free(gpointer data)
{
	dm_thread_data *D = (dm_thread_data *)data;
	if (D->data) {
		g_free(D->data); D->data = NULL;
	}
	g_free(D); D = NULL;
}

/* 
 * worker threads can send messages to the client
 * through the main thread async queue. This data
 * is written directly to the output event
 */
void dm_thread_data_sendmessage(gpointer data)
{
	dm_thread_data *D = (dm_thread_data *)data;
	ImapSession *session = (ImapSession *)D->session;
	if (D->data && session && session->ci) {
		ci_write(session->ci, "%s", (char *)D->data);
	}
}

/* 
 * thread-entry callback
 *
 * if a thread in the pool is assigned a job from the queue
 * this call is used as entry point for the job at hand.
 */
static void dm_thread_dispatch(gpointer data, gpointer user_data)
{
	TRACE(TRACE_DEBUG,"data[%p], user_data[%p]", data, user_data);
	dm_thread_data *D = (dm_thread_data *)data;
	ImapSession *session = (ImapSession *)D->session;
	if (session->state == CLIENTSTATE_QUIT_QUEUED)
		return;

	D->cb_enter(D);
}

/*
 *
 * basic server setup
 *
 */
static int server_setup(ServerConfig_T *conf)
{
	GError *err = NULL;
	guint tpool_size = db_params.max_db_connections;

	server_set_sighandler();

	if (! MATCH(conf->service_name,"IMAP")) 
		return 0;

	// Asynchronous message queue for receiving messages
	// from worker threads in the main thread. 
	//
	// Only the main thread is allowed to do network IO.
	// see the libevent docs for the ratio and a work-around.
	// Dbmail only needs and uses a single eventbase for now.
	queue = g_async_queue_new();

	// Create the thread pool
	if (! (tpool = g_thread_pool_new((GFunc)dm_thread_dispatch,NULL,tpool_size,FALSE,&err)))
		TRACE(TRACE_DEBUG,"g_thread_pool creation failed [%s]", err->message);

	// self-pipe used to push the event-loop
	if (pipe(selfpipe))
		TRACE(TRACE_EMERG, "selfpipe setup failed");

	UNBLOCK(selfpipe[0]);
	UNBLOCK(selfpipe[1]);
	
	assert(evbase);
	pev = event_new(evbase, selfpipe[0], EV_READ, dm_queue_drain, NULL);
	event_add(pev, NULL);

	return 0;
}

static void _cb_log_event(int severity, const char *msg)
{
	assert(0);
	TRACE(TRACE_WARNING, "%s", msg);
}

static int server_start_cli(ServerConfig_T *conf)
{
	server_conf = conf;
	if (db_connect() != 0) {
		TRACE(TRACE_ERR, "could not connect to database");
		return -1;
	}

	if (auth_connect() != 0) {
		TRACE(TRACE_ERR, "could not connect to authentication");
		return -1;
	}

	srand((int) ((int) time(NULL) + (int) getpid()));

	/* streams are ready, perform handling */

	if (MATCH(conf->service_name,"HTTP")) {
		TRACE(TRACE_DEBUG,"starting httpd cli server...");
	} else {
		evthread_use_pthreads();
#ifdef DEBUG
		event_enable_debug_mode();
		event_set_log_callback(_cb_log_event);
#endif

		evbase = event_base_new();
		if (server_setup(conf)) return -1;
		conf->ClientHandler(NULL);
		event_base_dispatch(evbase);
	}

	disconnect_all();

	TRACE(TRACE_INFO, "connections closed"); 
	return 0;
}

// PUBLIC
int StartCliServer(ServerConfig_T * conf)
{
	assert(conf);
	server_start_cli(conf);
	return 0;
}

/* Should be called after a HUP to allow for log rotation,
 * as the filesystem may want to give us new inodes and/or
 * the user may have changed the log file configs. */
static void reopen_logs(ServerConfig_T *conf)
{
	int serr;

	if (! (freopen(conf->log, "a", stdout))) {
		serr = errno;
		TRACE(TRACE_ERR, "freopen failed on [%s] [%s]", conf->log, strerror(serr));
	}

	if (! (freopen(conf->error_log, "a", stderr))) {
		serr = errno;
		TRACE(TRACE_ERR, "freopen failed on [%s] [%s]", conf->error_log, strerror(serr));
	}

	if (! (freopen("/dev/null", "r", stdin))) {
		serr = errno;
		TRACE(TRACE_ERR, "freopen failed on stdin [%s]", strerror(serr));
	}
}
	
/* Should be called once to initially close the actual std{in,out,err}
 * and open the redirection files. */
static void reopen_logs_fatal(ServerConfig_T *conf)
{
	int serr;

	if (! (freopen(conf->log, "a", stdout))) {
		serr = errno;
		TRACE(TRACE_EMERG, "freopen failed on [%s] [%s]", conf->log, strerror(serr));
	}
	if (! (freopen(conf->error_log, "a", stderr))) {
		serr = errno;
		TRACE(TRACE_EMERG, "freopen failed on [%s] [%s]", conf->error_log, strerror(serr));
	}
	if (! (freopen("/dev/null", "r", stdin))) {
		serr = errno;
		TRACE(TRACE_EMERG, "freopen failed on stdin [%s]", strerror(serr));
	}
}

pid_t server_daemonize(ServerConfig_T *conf)
{
	assert(conf);
	
	// double-fork
	if (fork()) exit(0);
	setsid();
	if (fork()) exit(0);

	if (chdir("/"))
		TRACE(TRACE_EMERG, "chdir / failed");

	umask(0077);

	reopen_logs_fatal(conf);

	TRACE(TRACE_DEBUG, "sid: [%d]", getsid(0));

	return getsid(0);
}

static int dm_bind_and_listen(int sock, struct sockaddr *saddr, socklen_t len, int backlog, gboolean ssl)
{
	int err, so_reuseaddress = 1;
	char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
	
	memset(hbuf,0, sizeof(hbuf));
	memset(sbuf,0, sizeof(sbuf));

	if (getnameinfo(saddr, len, hbuf, sizeof(hbuf), sbuf, sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV)) {
		TRACE(TRACE_DEBUG, "could not get numeric hostname");
	}

	TRACE(TRACE_DEBUG, "creating %s socket [%d] on [%s:%s]", ssl?"ssl":"plain", sock, hbuf, sbuf);

	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddress, sizeof(so_reuseaddress));
	/* bind the address */
	if ((bind(sock, saddr, len)) == -1) {
		err = errno;
		TRACE(TRACE_EMERG, "bind::error [%s]", strerror(err));
	}

	if ((listen(sock, backlog)) == -1) {
		err = errno;
		TRACE(TRACE_EMERG, "listen::error [%s]", strerror(err));
	}
	
	return 0;
	
}

static int create_unix_socket(ServerConfig_T * conf)
{
	int sock;
	struct sockaddr_un un;

	conf->resolveIP=0;

	if ((sock = socket(PF_UNIX, SOCK_STREAM, 0)) == -1) {
		int err = errno;
		TRACE(TRACE_EMERG, "%s", strerror(err));
	}

	/* setup sockaddr_un */
	memset(&un, 0, sizeof(un));
	un.sun_family = AF_UNIX;
	strncpy(un.sun_path,conf->socket, sizeof(un.sun_path));

	TRACE(TRACE_DEBUG, "create socket [%s] backlog [%d]", conf->socket, conf->backlog);

	// any error in dm_bind_and_listen is fatal
	dm_bind_and_listen(sock, (struct sockaddr *)&un, sizeof(un), conf->backlog, FALSE);
	
	chmod(conf->socket, 02777);

	return sock;
}

static void create_inet_socket(ServerConfig_T *conf, int i, gboolean ssl)
{
	struct addrinfo hints, *res, *res0;
	int s, error = 0;
	const char *port;

	if (ssl) {
		port = conf->ssl_port;
	} else {
		port = conf->port;
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_family    = PF_UNSPEC;
	hints.ai_socktype  = SOCK_STREAM;
	hints.ai_flags     = AI_PASSIVE;
	error = getaddrinfo(conf->iplist[i], port, &hints, &res0);
	if (error) {
		TRACE(TRACE_ERR, "getaddrinfo error [%d] %s", error, gai_strerror(error));
		return;
		/*NOTREACHED*/
        }
	
	for (res = res0; res && conf->ssl_socketcount < MAXSOCKETS && conf->socketcount < MAXSOCKETS; res = res->ai_next) {
		if ((s = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) < 0) {
			TRACE(TRACE_ERR, "could not create a socket of family [%d], socktype[%d], protocol [%d]", res->ai_family, res->ai_socktype, res->ai_protocol);
			continue;
		}
		UNBLOCK(s);

		dm_bind_and_listen(s, res->ai_addr, res->ai_addrlen, conf->backlog, ssl);
		if (ssl)
			conf->ssl_listenSockets[conf->ssl_socketcount++] = s;
		else
			conf->listenSockets[conf->socketcount++] = s;
 	}
	freeaddrinfo(res0);
}

static void server_close_sockets(ServerConfig_T *conf)
{
	if (conf->evh) {
		evhttp_free(conf->evh);
	} else {
		int i;
		for (i = 0; i < conf->socketcount; i++)
			if (conf->listenSockets[i] > 0)
				close(conf->listenSockets[i]);
		conf->socketcount=0;

		for (i = 0; i < conf->ssl_socketcount; i++)
			if (conf->ssl_listenSockets[i] > 0)
				close(conf->ssl_listenSockets[i]);
		conf->ssl_socketcount=0;
		if (conf->socket)
			unlink(conf->socket);
	}
}

static void server_exit(void)
{
	disconnect_all();
	server_close_sockets(server_conf);
}
	
static void server_create_sockets(ServerConfig_T * conf)
{
	int i;

	conf->listenSockets = g_new0(int, MAXSOCKETS);
	conf->ssl_listenSockets = g_new0(int, MAXSOCKETS);

	if (conf->socket && strlen(conf->socket))
		conf->listenSockets[conf->socketcount++] = create_unix_socket(conf);

	tls_load_certs(conf);

	if (conf->ssl)
		tls_load_ciphers(conf);

	if (conf->port && strlen(conf->port)) {
		for (i = 0; i < conf->ipcount; i++) {
			create_inet_socket(conf, i, FALSE);
		}
	}

	if (conf->ssl && strlen(conf->ssl_port)) {
		for (i = 0; i < conf->ipcount; i++) {
			create_inet_socket(conf, i, TRUE);
		}
	}
}

static void _sock_cb(int sock, short event, void *arg, gboolean ssl)
{
	client_sock *c = g_new0(client_sock,1);
	struct sockaddr *caddr;
	struct sockaddr *saddr;
	struct event *ev = (struct event *)arg;

#ifdef DEBUG
	TRACE(TRACE_DEBUG,"%d %s%s%s%s, %p, ssl:%s", sock, 
			(event&EV_TIMEOUT) ? " timeout" : "", 
			(event&EV_READ)    ? " read"    : "", 
			(event&EV_WRITE)   ? " write"   : "", 
			(event&EV_SIGNAL)  ? " signal"  : "", 
			arg, ssl?"Y":"N");
#endif
	/* accept the active fd */
	int len = sizeof(*caddr);

	if ((c->sock = accept(sock, NULL, NULL)) < 0) {
                int serr=errno;
                switch(serr) {
                        case ECONNABORTED:
                        case EPROTO:
                        case EINTR:
                                TRACE(TRACE_DEBUG, "%s", strerror(serr));
                                break;
                        default:
                                TRACE(TRACE_ERR, "%s", strerror(serr));
                                break;
                }
		g_free(c);
                return;
        }
	
        caddr = (struct sockaddr *)g_new0(struct sockaddr_storage, 1);
	if (getpeername(c->sock, caddr, (socklen_t *)&len) < 0) {
		int serr = errno;
		TRACE(TRACE_INFO, "getpeername::error [%s]", strerror(serr));
		g_free(caddr);
		g_free(c);
		return;
	}

	c->caddr = caddr;
	c->caddr_len = len;

        saddr = (struct sockaddr *)g_new0(struct sockaddr_storage, 1);
	if (getsockname(c->sock, saddr, (socklen_t *)&len) < 0) {
		int serr = errno;
		TRACE(TRACE_EMERG, "getsockname::error [%s]", strerror(serr));
		g_free(saddr);
		g_free(caddr);
		g_free(c);
		return; // fatal 
	}

	c->saddr = saddr;
	c->saddr_len = len;
	
	if (ssl) c->ssl_state = -1; // defer tls setup

	TRACE(TRACE_INFO, "connection accepted");

	/* streams are ready, perform handling */
	server_conf->ClientHandler((client_sock *)c);

	/* reschedule */
	event_add(ev, NULL);

}

static void server_sock_cb(int sock, short event, void *arg)
{
	_sock_cb(sock, event, arg, FALSE);
}

static void server_sock_ssl_cb(int sock, short event, void *arg)
{
	_sock_cb(sock, event, arg, TRUE);
}


void server_sig_cb(int fd, short event, void *arg)
{
	struct event *ev = arg;
	
	TRACE(TRACE_DEBUG,"fd [%d], event [%d], signal [%d]", fd, event, EVENT_SIGNAL(ev));

	switch (EVENT_SIGNAL(ev)) {
		case SIGHUP: // TODO: reload config
			mainRestart = 1;
		case SIGPIPE: // ignore
		break;
		default:
			exit(0);
		break;
	}
}

static int server_set_sighandler(void)
{
	sigset_t set;

	assert(evbase);

	sig_int = evsignal_new(evbase, SIGINT, server_sig_cb, NULL);
	evsignal_assign(sig_int, evbase, SIGINT, server_sig_cb, sig_int);

	sig_hup = evsignal_new(evbase, SIGHUP, server_sig_cb, NULL); 
	evsignal_assign(sig_hup, evbase, SIGHUP, server_sig_cb, sig_hup); 

	sig_term = evsignal_new(evbase, SIGTERM, server_sig_cb, NULL); 
	evsignal_assign(sig_term, evbase, SIGTERM, server_sig_cb, sig_term); 
	
	evsignal_add(sig_int, NULL);
	evsignal_add(sig_hup, NULL);
	evsignal_add(sig_term, NULL);

	sigemptyset(&set);
	sigaddset(&set, SIGPIPE);
	pthread_sigmask(SIG_BLOCK, &set, NULL);

	TRACE(TRACE_INFO, "signal handler placed");

	return 0;
}

// 
// Public methods
void disconnect_all(void)
{
	TRACE(TRACE_INFO, "disconnecting all");
	db_disconnect();
	auth_disconnect();
	g_mime_shutdown();
	config_free();

	if (tpool) { 
		g_thread_pool_free(tpool,TRUE,FALSE);
		tpool = NULL;
	}
	if (sig_int) {
		g_free(sig_int);
		sig_int = NULL;
	}
	if (sig_hup) {
		g_free(sig_hup);
		sig_hup = NULL;
	}
	if (sig_term) {
		g_free(sig_term);
		sig_term = NULL;
	}
}

static void server_pidfile(ServerConfig_T *conf)
{
	static gboolean configured = FALSE;
	if (configured) return;

	/* We write the pidFile after daemonize because
	 * we may actually be a child of the original process. */
	if (! conf->pidFile)
		conf->pidFile = config_get_pidfile(conf, conf->process_name);

	pidfile_create(conf->pidFile, getpid());

	configured = TRUE;
}

int server_run(ServerConfig_T *conf)
{
	int i;
	struct event **evsock;

	mainRestart = 0;

	assert(conf);
	reopen_logs(conf);

 	TRACE(TRACE_NOTICE, "starting main service loop for [%s]", conf->service_name);

	server_conf = conf;
	if (db_connect() != 0) {
		TRACE(TRACE_ERR, "could not connect to database");
		return -1;
	}

	if (auth_connect() != 0) {
		TRACE(TRACE_ERR, "could not connect to authentication");
		return -1;
	}
	srand((int) ((int) time(NULL) + (int) getpid()));

 	TRACE(TRACE_NOTICE, "starting main service loop for [%s]", conf->service_name);

	server_conf = conf;

	evthread_use_pthreads();
#ifdef DEBUG
	event_enable_debug_mode();
	event_set_log_callback(_cb_log_event);
#endif
	evbase = event_base_new();

	if (server_setup(conf)) return -1;

	if (conf->port) {

		if (MATCH(conf->service_name, "HTTP")) {
			// FIXME: 
			int port = atoi(conf->port);
			if (! port) {
				TRACE(TRACE_ERR, "Failed to convert port spec [%s]", conf->port);
			} else {
				for (i = 0; i < server_conf->ipcount; i++) {
					TRACE(TRACE_DEBUG, "starting HTTP service [%s:%d]", conf->iplist[i], port);
					if (! (conf->evh = evhttp_start(conf->iplist[i], port))) {
						int serr = errno;
						TRACE(TRACE_EMERG, "[%s]", strerror(serr));
						return -1;
					}
					TRACE(TRACE_DEBUG, "started HTTP service [%p]", conf->evh);
					evhttp_set_gencb(conf->evh, Request_cb, NULL);
				}
			}
		} else {
			int k, total;
			server_create_sockets(conf);
			total = conf->socketcount + conf->ssl_socketcount;
			evsock = g_new0(struct event *, total);
			for (i = 0; i < conf->socketcount; i++) {
				TRACE(TRACE_DEBUG, "Adding event for plain socket [%d] [%d/%d]", conf->listenSockets[i], i+1, total);
				evsock[i] = event_new(evbase, conf->listenSockets[i], EV_READ, server_sock_cb, NULL);
				event_assign(evsock[i], evbase, conf->listenSockets[i], EV_READ, server_sock_cb, evsock[i]);
				event_add(evsock[i], NULL);
			}
			k = i+1;
			for (k = i, i = 0; i < conf->ssl_socketcount; i++, k++) {
				TRACE(TRACE_DEBUG, "Adding event for ssl socket [%d] [%d/%d]", conf->ssl_listenSockets[i], k+1, total);
				evsock[k] = event_new(evbase, conf->ssl_listenSockets[i], EV_READ, server_sock_ssl_cb, NULL);
				event_assign(evsock[k], evbase, conf->listenSockets[i], EV_READ, server_sock_cb, evsock[k]);
				event_add(evsock[k], NULL);
			}
		}
	}	

	atexit(server_exit);

	if (drop_privileges(conf->serverUser, conf->serverGroup) < 0) {
		TRACE(TRACE_ERR,"unable to drop privileges");
		return 0;
	}
	
	server_pidfile(conf);

	TRACE(TRACE_DEBUG,"dispatching event loop...");

	event_base_dispatch(evbase);

	return 0;
}

void server_showhelp(const char *name, const char *greeting) {
	printf("*** %s ***\n", name);

	printf("%s\n", greeting);
	printf("See the man page for more info.\n");

        printf("\nCommon options for all DBMail daemons:\n");
	printf("     -f file   specify an alternative config file\n");
	printf("     -p file   specify an alternative runtime pidfile\n");
	printf("     -n        stdin/stdout mode\n");
	printf("     -D        foreground mode\n");
	printf("     -v        verbose logging to syslog and stderr\n");
	printf("     -V        show the version\n");
	printf("     -h        show this help message\n");
}

/* Return values:
 * -1 just quit
 * 0 all is well, config is updated
 * 1 help must be shown, then quit
 */ 

static void server_config_free(ServerConfig_T * config)
{
	assert(config);

	g_strfreev(config->iplist);
	g_free(config->listenSockets);
	g_free(config->ssl_listenSockets);

	config->listenSockets = NULL;
	config->ssl = FALSE;
	config->ssl_listenSockets = NULL;
	config->iplist = NULL;

	memset(config, 0, sizeof(ServerConfig_T));
}

int server_getopt(ServerConfig_T *config, const char *service, int argc, char *argv[])
{
	int opt;
	configFile = g_strdup(DEFAULT_CONFIG_FILE);

	server_config_free(config);

	TRACE(TRACE_DEBUG, "checking command line options");

	/* get command-line options */
	opterr = 0;		/* suppress error message from getopt() */
	while ((opt = getopt(argc, argv, "vVhqnDf:p:s:")) != -1) {
		switch (opt) {
		case 'v':
			config->log_verbose = 1;
			break;
		case 'V':
			PRINTF_THIS_IS_DBMAIL;
			return -1;
		case 'n':
			config->no_daemonize = 1;
			break;
		case 'D':
			config->no_daemonize = 2;
			break;
		case 'h':
			return 1;
		case 'p':
			if (optarg && strlen(optarg) > 0)
				config->pidFile = g_strdup(optarg);
			else {
				fprintf(stderr, "%s: -p requires a filename argument\n\n", argv[0]);
				return 1;
			}
			break;
		case 'f':
			if (optarg && strlen(optarg) > 0) {
                                g_free(configFile);
				configFile = g_strdup(optarg);
			} else {
				fprintf(stderr, "%s: -f requires a filename argument\n\n", argv[0]);
				return 1;
			}
			break;

		default:
			fprintf(stderr, "%s: unrecognized option: %s\n\n", argv[0], argv[optind]);
			return 1;
		}
	}

	if (optind < argc) {
		fprintf(stderr, "%s: unrecognized options: ", argv[0]);
		while (optind < argc)
			fprintf(stderr, "%s ", argv[optind++]);
		fprintf(stderr, "\n\n");
		return 1;
	}

	server_config_load(config, service);

	return 0;
}

int server_mainloop(ServerConfig_T *config, const char *service, const char *servicename)
{
	strncpy(config->process_name, servicename, FIELDSIZE);

	g_mime_init(0);
	g_mime_parser_get_type();
	g_mime_stream_get_type();
	g_mime_stream_mem_get_type();
	g_mime_stream_file_get_type();
	g_mime_stream_buffer_get_type();
	g_mime_stream_filter_get_type();
	g_mime_filter_crlf_get_type();

	tls_context = tls_init();

	if (config->no_daemonize == 1) {
		StartCliServer(config);
		TRACE(TRACE_INFO, "exiting cli server");
		return 0;
	}
	
	if (! config->no_daemonize)
		server_daemonize(config);


	/* This is the actual main loop. */
	while (server_run(config)) {
		/* Reread the config file and restart the services,
		 * e.g. on SIGHUP or other graceful restart condition. */
		server_config_load(config, service);
		sleep(2);
	}

	server_config_free(config);
	TRACE(TRACE_INFO, "leaving main loop");
	return 0;
}

void server_config_load(ServerConfig_T * config, const char * const service)
{
	Field_T val, val_ssl;

	TRACE(TRACE_DEBUG, "reading config [%s]", configFile);
	config_free();
	config_read(configFile);

	SetTraceLevel(service);
	/* Override SetTraceLevel. */
	if (config->log_verbose) {
		configure_debug(5,5);
	}

	config_get_logfiles(config, service);

	/* read items: TIMEOUT */
	config_get_value("TIMEOUT", service, val);
	if (strlen(val) == 0) {
		TRACE(TRACE_DEBUG, "no value for TIMEOUT in config file");
		config->timeout = 300;
	} else if ((config->timeout = atoi(val)) <= 30)
		TRACE(TRACE_EMERG, "value for TIMEOUT is invalid: [%d]", config->timeout);

	TRACE(TRACE_DEBUG, "timeout [%d] seconds", config->timeout);

	/* read items: LOGIN_TIMEOUT */
	config_get_value("LOGIN_TIMEOUT", service, val);
	if (strlen(val) == 0) {
		TRACE(TRACE_DEBUG, "no value for TIMEOUT in config file");
		config->login_timeout = 60;
	} else if ((config->login_timeout = atoi(val)) <= 10)
		TRACE(TRACE_EMERG, "value for TIMEOUT is invalid: [%d]", config->login_timeout);

	TRACE(TRACE_DEBUG, "login_timeout [%d] seconds",
	      config->login_timeout);

	/* SOCKET */
	config_get_value("SOCKET", service, val);
	if (strlen(val) == 0)
		TRACE(TRACE_DEBUG, "no value for SOCKET in config file");
	strncpy(config->socket, val, FIELDSIZE);
	TRACE(TRACE_DEBUG, "socket [%s]", config->socket);
	
	/* read items: PORT */
	config_get_value("PORT", service, val);
	config_get_value("TLS_PORT", service, val_ssl);

	if ((strlen(val) == 0) && (strlen(val_ssl) == 0))
		TRACE(TRACE_EMERG, "no value for PORT or TLS_PORT in config file");

	strncpy(config->port, val, FIELDSIZE);
	TRACE(TRACE_DEBUG, "binding to PORT [%s]", config->port);

	if (strlen(val_ssl) > 0) {
		strncpy(config->ssl_port, val_ssl, FIELDSIZE);
		TRACE(TRACE_DEBUG, "binding to SSL_PORT [%s]", config->ssl_port);
	}

	/* read items: BINDIP */
	config_get_value("BINDIP", service, val);
	if (strlen(val) == 0)
		TRACE(TRACE_EMERG, "no value for BINDIP in config file");
	// If there was a SIGHUP, then we're resetting an active config.
	g_strfreev(config->iplist);
	g_free(config->listenSockets);
	// Allowed list separators are ' ' and ','.
	config->iplist = g_strsplit_set(val, " ,", 0);
	config->ipcount = g_strv_length(config->iplist);
	if (config->ipcount < 1)
		TRACE(TRACE_EMERG, "no value for BINDIP in config file");

	int ip;
	for (ip = 0; ip < config->ipcount; ip++) {
		// Remove whitespace from each list entry, then log it.
		g_strstrip(config->iplist[ip]);
		if (config->iplist[ip][0] == '*') {
			g_free(config->iplist[ip]);
			config->iplist[ip] = g_strdup("0.0.0.0");
		}
		TRACE(TRACE_DEBUG, "binding to IP [%s]", config->iplist[ip]);
	}

	/* read items: BACKLOG */
	config_get_value("BACKLOG", service, val);
	if (strlen(val) == 0) {
		TRACE(TRACE_DEBUG, "no value for BACKLOG in config file. Using default value [%d]", BACKLOG);
		config->backlog = BACKLOG;
	} else if ((config->backlog = atoi(val)) <= 0)
		TRACE(TRACE_EMERG, "value for BACKLOG is invalid: [%d]", config->backlog);

	/* read items: RESOLVE_IP */
	config_get_value("RESOLVE_IP", service, val);
	if (strlen(val) == 0)
		TRACE(TRACE_DEBUG, "no value for RESOLVE_IP in config file");
	config->resolveIP = (strcasecmp(val, "yes") == 0);
	TRACE(TRACE_DEBUG, "%sresolving client IP", config->resolveIP ? "" : "not ");

	/* read items: service-BEFORE-SMTP */
	char *service_before_smtp = g_strconcat(service, "_BEFORE_SMTP", NULL);
	config_get_value(service_before_smtp, service, val);
	g_free(service_before_smtp);
	if (strlen(val) == 0)
		TRACE(TRACE_DEBUG, "no value for %s_BEFORE_SMTP  in config file", service);
	config->service_before_smtp = (strcasecmp(val, "yes") == 0);
	TRACE(TRACE_DEBUG, "%s %s-before-SMTP", config->service_before_smtp ? "Enabling" : "Disabling", service);

	/* read items: authlog */
	config_get_value("authlog", service, val);
	if (strlen(val) == 0)
		TRACE(TRACE_DEBUG, "no value for AUTHLOG in config file");
	config->authlog = (strcasecmp(val, "yes") == 0);
	TRACE(TRACE_DEBUG, "%s %s Authentication logging", config->authlog ? "Enabling" : "Disabling", service);

	/* read items: EFFECTIVE-USER */
	config_get_value("EFFECTIVE_USER", service, val);
	if (strlen(val) == 0)
		TRACE(TRACE_EMERG, "no value for EFFECTIVE_USER in config file");

	strncpy(config->serverUser, val, FIELDSIZE);
	config->serverUser[FIELDSIZE - 1] = '\0';

	TRACE(TRACE_DEBUG, "effective user shall be [%s]",
	      config->serverUser);

	/* read items: EFFECTIVE-GROUP */
	config_get_value("EFFECTIVE_GROUP", service, val);
	if (strlen(val) == 0)
		TRACE(TRACE_EMERG, "no value for EFFECTIVE_GROUP in config file");

	strncpy(config->serverGroup, val, FIELDSIZE);
	config->serverGroup[FIELDSIZE - 1] = '\0';

	TRACE(TRACE_DEBUG, "effective group shall be [%s]", config->serverGroup);

	/* read items: TLS_CAFILE */
	config_get_value("TLS_CAFILE", service, val);
	if(strlen(val) == 0)
		TRACE(TRACE_WARNING, "no value for TLS_CAFILE in config file");
	strncpy(config->tls_cafile, val, FIELDSIZE);
        config->tls_cafile[FIELDSIZE - 1] = '\0';

        TRACE(TRACE_DEBUG, "CA file is set to [%s]", config->tls_cafile);

	/* read items: TLS_CERT */
	config_get_value("TLS_CERT", service, val);
	if(strlen(val) == 0)
		TRACE(TRACE_WARNING, "no value for TLS_CERT in config file");
	strncpy(config->tls_cert, val, FIELDSIZE);
        config->tls_cert[FIELDSIZE - 1] = '\0';

        TRACE(TRACE_DEBUG, "Certificate file is set to [%s]", config->tls_cert);

	/* read items: TLS_KEY */
	config_get_value("TLS_KEY", service, val);
	if(strlen(val) == 0)
		TRACE(TRACE_WARNING, "no value for TLS_KEY in config file");
	strncpy(config->tls_key, val, FIELDSIZE);
        config->tls_key[FIELDSIZE - 1] = '\0';

        TRACE(TRACE_DEBUG, "Key file is set to [%s]", config->tls_key);

	/* read items: TLS_CIPHERS */
	config_get_value("TLS_CIPHERS", service, val);
	if(strlen(val) == 0)
		TRACE(TRACE_INFO, "no value for TLS_CIPHERS in config file");
	strncpy(config->tls_ciphers, val, FIELDSIZE);
        config->tls_ciphers[FIELDSIZE - 1] = '\0';

        TRACE(TRACE_DEBUG, "Cipher string is set to [%s]", config->tls_ciphers);

	strncpy(config->service_name, service, FIELDSIZE);

	GetDBParams();
}



