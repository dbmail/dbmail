/*
  
 Copyright (C) 1999-2004 IC & S  dbmail@D-s.nl
 Copyright (c) 2004-2008 NFG Net Facilities Group BV support@nfg.nl

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
#define THIS_MODULE "server"

static char *configFile = DEFAULT_CONFIG_FILE;

volatile sig_atomic_t mainRestart = 0;
volatile sig_atomic_t alarm_occurred = 0;

// thread data
int selfpipe[2];
GAsyncQueue *queue;
GThreadPool *tpool = NULL;


serverConfig_t *server_conf;

static void server_config_load(serverConfig_t * conf, const char * const service);
static int server_set_sighandler(void);

struct event *sig_int, *sig_hup, *sig_pipe;


/* 
 *
 * threaded command primitives 
 *
 * the goal is to make long running tasks (mainly database IO) non-blocking
 *
 *
 */

static void dm_thread_data_free(gpointer data);

/*
 * async queue drainage callback for the main thread
 */
void dm_queue_drain(clientbase_t *client UNUSED)
{
	gpointer data;
	do {
		data = g_async_queue_try_pop(queue);
		if (data) {
			dm_thread_data *D = (gpointer)data;
			if (D->cb_leave) D->cb_leave(data);
			dm_thread_data_free(data);
		}
	} while (data);
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
	dm_thread_data *D = g_new0(dm_thread_data,1);
	D->cb_enter	= cb_enter;
	D->cb_leave     = cb_leave;
	D->session	= session;
	D->data         = data;

	// we're not done until we're done
	D->session->command_state = FALSE; 

	TRACE(TRACE_DEBUG,"[%p] [%p]", D, D->session);

	g_thread_pool_push(tpool, D, &err);

	if (err) TRACE(TRACE_FATAL,"g_thread_pool_push failed [%s]", err->message);
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
	if (D->data && D->session)
		ci_write(session->ci, "%s", (char *)D->data);
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
	D->cb_enter(D);
}

/*
 *
 * basic server setup
 *
 */
static int server_setup(void)
{
	GError *err = NULL;

	if (! g_thread_supported () ) g_thread_init (NULL);
	server_set_sighandler();

	// Asynchronous message queue for receiving messages
	// from worker threads in the main thread. 
	//
	// Only the main thread is allowed to do network IO.
	// see the libevent docs for the ratio and a work-around.
	// Dbmail only needs and uses a single eventbase for now.
	queue = g_async_queue_new();

	// Create the thread pool
	if (! (tpool = g_thread_pool_new((GFunc)dm_thread_dispatch,NULL,10,TRUE,&err)))
		TRACE(TRACE_DEBUG,"g_thread_pool creation failed [%s]", err->message);

	// self-pipe used to push the event-loop
	pipe(selfpipe);
	UNBLOCK(selfpipe[0]);
	UNBLOCK(selfpipe[1]);
	
	return 0;
}
	
static int server_start_cli(serverConfig_t *conf)
{
	server_conf = conf;
	if (db_connect() != 0) {
		TRACE(TRACE_ERROR, "could not connect to database");
		return -1;
	}

	if (auth_connect() != 0) {
		TRACE(TRACE_ERROR, "could not connect to authentication");
		return -1;
	}

	srand((int) ((int) time(NULL) + (int) getpid()));

	/* streams are ready, perform handling */
	event_init();

	if (server_setup()) return -1;

	conf->ClientHandler(NULL);
	event_dispatch();

	disconnect_all();

	TRACE(TRACE_INFO, "connections closed"); 
	return 0;
}

// PUBLIC
int StartCliServer(serverConfig_t * conf)
{
	assert(conf);
	server_start_cli(conf);
	return 0;
}

/* Should be called after a HUP to allow for log rotation,
 * as the filesystem may want to give us new inodes and/or
 * the user may have changed the log file configs. */
static void reopen_logs(serverConfig_t *conf)
{
	int serr;

	if (! (freopen(conf->log, "a", stdout))) {
		serr = errno;
		TRACE(TRACE_ERROR, "freopen failed on [%s] [%s]", conf->log, strerror(serr));
	}

	if (! (freopen(conf->error_log, "a", stderr))) {
		serr = errno;
		TRACE(TRACE_ERROR, "freopen failed on [%s] [%s]", conf->error_log, strerror(serr));
	}

	if (! (freopen("/dev/null", "r", stdin))) {
		serr = errno;
		TRACE(TRACE_ERROR, "freopen failed on stdin [%s]", strerror(serr));
	}
}
	
/* Should be called once to initially close the actual std{in,out,err}
 * and open the redirection files. */
static void reopen_logs_fatal(serverConfig_t *conf)
{
	int serr;

	if (! (freopen(conf->log, "a", stdout))) {
		serr = errno;
		TRACE(TRACE_FATAL, "freopen failed on [%s] [%s]", conf->log, strerror(serr));
	}
	if (! (freopen(conf->error_log, "a", stderr))) {
		serr = errno;
		TRACE(TRACE_FATAL, "freopen failed on [%s] [%s]", conf->error_log, strerror(serr));
	}
	if (! (freopen("/dev/null", "r", stdin))) {
		serr = errno;
		TRACE(TRACE_FATAL, "freopen failed on stdin [%s]", strerror(serr));
	}
}

pid_t server_daemonize(serverConfig_t *conf)
{
	assert(conf);
	
	// double-fork
	if (fork()) exit(0);
	setsid();
	if (fork()) exit(0);

	chdir("/");
	umask(0077);

	reopen_logs_fatal(conf);

	TRACE(TRACE_DEBUG, "sid: [%d]", getsid(0));

	return getsid(0);
}

static int dm_socket(int domain)
{
	int sock, err;
	if ((sock = socket(domain, SOCK_STREAM, 0)) == -1) {
		err = errno;
		TRACE(TRACE_FATAL, "%s", strerror(err));
	}
	return sock;
}

static int dm_bind_and_listen(int sock, struct sockaddr *saddr, socklen_t len, int backlog)
{
	int err;
	/* bind the address */
	if ((bind(sock, saddr, len)) == -1) {
		err = errno;
		TRACE(TRACE_FATAL, "%s", strerror(err));
	}

	if ((listen(sock, backlog)) == -1) {
		err = errno;
		TRACE(TRACE_FATAL, "%s", strerror(err));
	}
	
	TRACE(TRACE_DEBUG, "done");
	return 0;
	
}

static int create_unix_socket(serverConfig_t * conf)
{
	int sock;
	struct sockaddr_un saServer;

	conf->resolveIP=0;

	sock = dm_socket(PF_UNIX);

	/* setup sockaddr_un */
	memset(&saServer, 0, sizeof(saServer));
	saServer.sun_family = AF_UNIX;
	strncpy(saServer.sun_path,conf->socket, sizeof(saServer.sun_path));

	TRACE(TRACE_DEBUG, "create socket [%s] backlog [%d]", conf->socket, conf->backlog);

	// any error in dm_bind_and_listen is fatal
	dm_bind_and_listen(sock, (struct sockaddr *)&saServer, sizeof(saServer), conf->backlog);
	
	chmod(conf->socket, 02777);

	return sock;
}

static int create_inet_socket(const char * const ip, int port, int backlog)
{
	int sock;
	struct sockaddr_in saServer;
	int so_reuseaddress = 1;

	sock = dm_socket(PF_INET);
	
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddress, sizeof(so_reuseaddress));

	/* setup sockaddr_in */
	memset(&saServer, 0, sizeof(saServer));
	saServer.sin_family	= AF_INET;
	saServer.sin_port	= htons(port);

	TRACE(TRACE_DEBUG, "create socket [%s:%d] backlog [%d]", ip, port, backlog);
	
	if (ip[0] == '*') {
		saServer.sin_addr.s_addr = htonl(INADDR_ANY);
	} else if (! (inet_aton(ip, &saServer.sin_addr))) {
		if (sock > 0) close(sock);
		TRACE(TRACE_FATAL, "IP invalid [%s]", ip);
	}

	// any error in dm_bind_and_listen is fatal
	dm_bind_and_listen(sock, (struct sockaddr *)&saServer, sizeof(saServer), backlog);

	UNBLOCK(sock);

	return sock;	
}

static void server_close_sockets(serverConfig_t *conf)
{
	int i;
	for (i = 0; i < conf->ipcount; i++)
		if (conf->listenSockets[i] > 0)
			close(conf->listenSockets[i]);
}

static void server_create_sockets(serverConfig_t * conf)
{
	int i;

	conf->listenSockets = g_new0(int, conf->ipcount);

	if (strlen(conf->socket) > 0) {
		conf->listenSockets[0] = create_unix_socket(conf);
	} else {
		for (i = 0; i < conf->ipcount; i++)
			conf->listenSockets[i] = create_inet_socket(conf->iplist[i], conf->port, conf->backlog);
	}
}

static void client_pipe_cb(int sock, short event, void *arg)
{
	clientbase_t *client;

	TRACE(TRACE_DEBUG,"%d %d, %p", sock, event, arg);
	char buf[1];
	while (read(sock, buf, 1) > 0)
		;
	client = (clientbase_t *)arg;
	if (client->cb_pipe) client->cb_pipe(client);
	if (client->pev) event_add(client->pev, NULL);
}

static int client_error_cb(int sock, short event, void *arg)
{
	int r = 0;
	clientbase_t *client = (clientbase_t *)arg;
	switch (event) {
		case EAGAIN:
			break;
		default:
			TRACE(TRACE_DEBUG,"%d %s, %p", sock, strerror((int)event), arg);
			r = -1;
			break;
	}
	return r;
}


clientbase_t * client_init(int socket, struct sockaddr_in *caddr)
{
	int err;
	clientbase_t *client	= g_new0(clientbase_t, 1);

	client->timeout		= server_conf->timeout;
	client->login_timeout	= server_conf->login_timeout;
	client->line_buffer	= g_string_new("");
	client->queue           = g_async_queue_new();
	client->cb_error        = client_error_cb;

	/* make streams */
	if (socket == 0 && caddr == NULL) {
		client->rx		= STDIN_FILENO;
		client->tx		= STDOUT_FILENO;
	} else {
		strncpy((char *)client->ip_src, inet_ntoa(caddr->sin_addr), sizeof(client->ip_src));
		client->ip_src_port = ntohs(caddr->sin_port);

		if (server_conf->resolveIP) {
			struct hostent *clientHost;
			clientHost = gethostbyaddr((gpointer) &(caddr->sin_addr), sizeof(caddr->sin_addr), caddr->sin_family);

			if (clientHost && clientHost->h_name)
				strncpy((char *)client->clientname, clientHost->h_name, FIELDSIZE);

			TRACE(TRACE_MESSAGE, "incoming connection from [%s:%d (%s)] by pid [%d]",
					client->ip_src, client->ip_src_port,
					client->clientname[0] ? client->clientname : "Lookup failed", getpid());
		} else {
			TRACE(TRACE_MESSAGE, "incoming connection from [%s:%d] by pid [%d]",
					client->ip_src, client->ip_src_port, getpid());
		}

		/* make streams */
		if (!(client->rx = dup(socket))) {
			err = errno;
			TRACE(TRACE_ERROR, "%s", strerror(err));
			if (socket > 0) close(socket);
			g_free(client);
			return NULL;
		}

		if (!(client->tx = socket)) {
			err = errno;
			TRACE(TRACE_ERROR, "%s", strerror(err));
			if (socket > 0) close(socket);
			g_free(client);
			return NULL;
		}
	}

	client->rev = g_new0(struct event, 1);
	client->wev = g_new0(struct event, 1);
	client->pev = g_new0(struct event, 1);
	event_set(client->pev, selfpipe[0], EV_READ, client_pipe_cb, client);
	event_add(client->pev, NULL);

	return client;
}

static void server_sock_cb(int sock, short event, void *arg)
{
	client_sock *c = g_new0(client_sock,1);
	struct sockaddr_in *caddr = g_new0(struct sockaddr_in, 1);
	struct event *ev = (struct event *)arg;

	TRACE(TRACE_DEBUG,"%d %d, %p", sock, event, arg);

	/* accept the active fd */
	int len = sizeof(struct sockaddr_in);

	if ((c->sock = accept(sock, caddr, (socklen_t *)&len)) < 0) {
                int serr=errno;
                switch(serr) {
                        case ECONNABORTED:
                        case EPROTO:
                        case EINTR:
                                TRACE(TRACE_DEBUG, "%s", strerror(serr));
                                break;
                        default:
                                TRACE(TRACE_ERROR, "%s", strerror(serr));
                                break;
                }
                return;
        }
	
	c->caddr = caddr;
	TRACE(TRACE_INFO, "connection accepted");

	/* streams are ready, perform handling */
	server_conf->ClientHandler((client_sock *)c);

	/* reschedule */
	event_add(ev, NULL);
}

void server_sig_cb(int fd, short event, void *arg)
{
	struct event *ev = arg;
	
	TRACE(TRACE_DEBUG,"fd [%d], event [%d], signal [%d]", fd, event, EVENT_SIGNAL(ev));

	switch (EVENT_SIGNAL(ev)) {
		case SIGHUP:
			mainRestart = 1;
		break;
		default:
			exit(0);
		break;
	}
}

// FIXME: signals have been in a bad shape since
// the libevent rewrite
static int server_set_sighandler(void)
{

	struct sigaction sa;
	
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_IGN;
	sa.sa_flags   = 0;

	sigemptyset(&sa.sa_mask);
	if (sigaction(SIGPIPE, &sa, 0) != 0)
		perror("sigaction");

	sig_int = g_new0(struct event, 1);
	sig_hup = g_new0(struct event, 1);

	signal_set(sig_int, SIGINT, server_sig_cb, sig_int); signal_add(sig_int, NULL);
	signal_set(sig_hup, SIGHUP, server_sig_cb, sig_hup); signal_add(sig_hup, NULL);

	TRACE(TRACE_INFO, "signal handler placed");

	return 0;
}

// 
// Public methods
void disconnect_all(void)
{
	db_disconnect();
	auth_disconnect();
	if (tpool) g_thread_pool_free(tpool,TRUE,FALSE);
	g_free(sig_int);
	g_free(sig_hup);
}

static void server_exit(void)
{
	disconnect_all();
	server_close_sockets(server_conf);
}
	
int server_run(serverConfig_t *conf)
{
	int ip;
	struct event *evsock;

	mainRestart = 0;

	assert(conf);
	reopen_logs(conf);
	server_create_sockets(conf);

 	TRACE(TRACE_MESSAGE, "starting main service loop");

	server_conf = conf;
	if (db_connect() != 0) {
		TRACE(TRACE_ERROR, "could not connect to database");
		return -1;
	}

	if (auth_connect() != 0) {
		TRACE(TRACE_ERROR, "could not connect to authentication");
		return -1;
	}

	srand((int) ((int) time(NULL) + (int) getpid()));

	TRACE(TRACE_DEBUG,"setup event loop");
	event_init();

	if (server_setup()) return -1;

	evsock = g_new0(struct event, server_conf->ipcount+1);
	for (ip = 0; ip < server_conf->ipcount; ip++) {
		event_set(&evsock[ip], server_conf->listenSockets[ip], EV_READ, server_sock_cb, &evsock[ip]);
		event_add(&evsock[ip], NULL);
	}

	TRACE(TRACE_DEBUG,"dispatching event loop...");

	atexit(server_exit);

	event_dispatch();

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

static void server_config_free(serverConfig_t * config)
{
	assert(config);

	g_strfreev(config->iplist);
	g_free(config->listenSockets);

	config->listenSockets = NULL;
	config->iplist = NULL;

	memset(config, 0, sizeof(serverConfig_t));
}

int server_getopt(serverConfig_t *config, const char *service, int argc, char *argv[])
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

int server_mainloop(serverConfig_t *config, const char *service, const char *servicename)
{
	if (config->no_daemonize == 1) {
		StartCliServer(config);
		TRACE(TRACE_INFO, "exiting cli server");
		return 0;
	}
	
	if (! config->no_daemonize)
		server_daemonize(config);

	/* We write the pidFile after daemonize because
	 * we may actually be a child of the original process. */
	if (! config->pidFile)
		config->pidFile = config_get_pidfile(config, servicename);

	pidfile_create(config->pidFile, getpid());

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

void server_config_load(serverConfig_t * config, const char * const service)
{
	field_t val;

	TRACE(TRACE_DEBUG, "reading config [%s]", configFile);
	config_free();
	config_read(configFile);

	SetTraceLevel(service);
	/* Override SetTraceLevel. */
	if (config->log_verbose) {
		configure_debug(5,5);
	}

	config_get_logfiles(config);

	/* read items: TIMEOUT */
	config_get_value("TIMEOUT", service, val);
	if (strlen(val) == 0) {
		TRACE(TRACE_DEBUG, "no value for TIMEOUT in config file");
		config->timeout = 0;
	} else if ((config->timeout = atoi(val)) <= 30)
		TRACE(TRACE_FATAL, "value for TIMEOUT is invalid: [%d]",
		      config->timeout);

	TRACE(TRACE_DEBUG, "timeout [%d] seconds",
	      config->timeout);

	/* read items: LOGIN_TIMEOUT */
	config_get_value("LOGIN_TIMEOUT", service, val);
	if (strlen(val) == 0) {
		TRACE(TRACE_DEBUG, "no value for TIMEOUT in config file");
		config->login_timeout = 60;
	} else if ((config->login_timeout = atoi(val)) <= 10)
		TRACE(TRACE_FATAL, "value for TIMEOUT is invalid: [%d]",
		      config->login_timeout);

	TRACE(TRACE_DEBUG, "login_timeout [%d] seconds",
	      config->login_timeout);

	/* SOCKET */
	config_get_value("SOCKET", service, val);
	if (strlen(val) == 0)
		TRACE(TRACE_DEBUG, "no value for SOCKET in config file");
	strncpy(config->socket, val, FIELDSIZE);
	TRACE(TRACE_DEBUG, "socket [%s]", 
		config->socket);
	
	/* read items: PORT */
	config_get_value("PORT", service, val);
	if (strlen(val) == 0)
		TRACE(TRACE_FATAL, "no value for PORT in config file");

	if ((config->port = atoi(val)) <= 0)
		TRACE(TRACE_FATAL, "value for PORT is invalid: [%d]",
		      config->port);

	TRACE(TRACE_DEBUG, "binding to PORT [%d]",
	      config->port);


	/* read items: BINDIP */
	config_get_value("BINDIP", service, val);
	if (strlen(val) == 0)
		TRACE(TRACE_FATAL, "no value for BINDIP in config file");
	// If there was a SIGHUP, then we're resetting an active config.
	g_strfreev(config->iplist);
	g_free(config->listenSockets);
	// Allowed list separators are ' ' and ','.
	config->iplist = g_strsplit_set(val, " ,", 0);
	config->ipcount = g_strv_length(config->iplist);
	if (config->ipcount < 1) {
		TRACE(TRACE_FATAL, "no value for BINDIP in config file");
	}

	int ip;
	for (ip = 0; ip < config->ipcount; ip++) {
		// Remove whitespace from each list entry, then log it.
		g_strstrip(config->iplist[ip]);
		TRACE(TRACE_DEBUG, "binding to IP [%s]", config->iplist[ip]);
	}

	/* read items: BACKLOG */
	config_get_value("BACKLOG", service, val);
	if (strlen(val) == 0) {
		TRACE(TRACE_DEBUG, "no value for BACKLOG in config file. Using default value [%d]",
			BACKLOG);
		config->backlog = BACKLOG;
	} else if ((config->backlog = atoi(val)) <= 0)
		TRACE(TRACE_FATAL, "value for BACKLOG is invalid: [%d]",
			config->backlog);

	/* read items: RESOLVE_IP */
	config_get_value("RESOLVE_IP", service, val);
	if (strlen(val) == 0)
		TRACE(TRACE_DEBUG, "no value for RESOLVE_IP in config file");

	config->resolveIP = (strcasecmp(val, "yes") == 0);

	TRACE(TRACE_DEBUG, "%sresolving client IP",
	      config->resolveIP ? "" : "not ");

	/* read items: service-BEFORE-SMTP */
	char *service_before_smtp = g_strconcat(service, "_BEFORE_SMTP", NULL);
	config_get_value(service_before_smtp, service, val);
	g_free(service_before_smtp);

	if (strlen(val) == 0)
		TRACE(TRACE_DEBUG, "no value for %s_BEFORE_SMTP  in config file",
		      service);

	config->service_before_smtp = (strcasecmp(val, "yes") == 0);

	TRACE(TRACE_DEBUG, "%s %s-before-SMTP",
	      config->service_before_smtp ? "Enabling" : "Disabling", service);


	/* read items: EFFECTIVE-USER */
	config_get_value("EFFECTIVE_USER", service, val);
	if (strlen(val) == 0)
		TRACE(TRACE_FATAL, "no value for EFFECTIVE_USER in config file");

	strncpy(config->serverUser, val, FIELDSIZE);
	config->serverUser[FIELDSIZE - 1] = '\0';

	TRACE(TRACE_DEBUG, "effective user shall be [%s]",
	      config->serverUser);


	/* read items: EFFECTIVE-GROUP */
	config_get_value("EFFECTIVE_GROUP", service, val);
	if (strlen(val) == 0)
		TRACE(TRACE_FATAL, "no value for EFFECTIVE_GROUP in config file");

	strncpy(config->serverGroup, val, FIELDSIZE);
	config->serverGroup[FIELDSIZE - 1] = '\0';

	TRACE(TRACE_DEBUG, "effective group shall be [%s]",
	      config->serverGroup);

	GetDBParams();
}



