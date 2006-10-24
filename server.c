/*
  $Id$
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2004-2006 NFG Net Facilities Group BV support@nfg.nl

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
 * code to implement a network server
 */

#include "dbmail.h"
#define THIS_MODULE "server"


volatile sig_atomic_t GeneralStopRequested = 0;
volatile sig_atomic_t Restart = 0;
volatile sig_atomic_t mainStop = 0;
volatile sig_atomic_t mainRestart = 0;
volatile sig_atomic_t mainStatus = 0;
volatile sig_atomic_t mainSig = 0;
volatile sig_atomic_t get_sigchld = 0;

int isChildProcess = 0;
int isGrandChildProcess = 0;
pid_t ParentPID = 0;
ChildInfo_t childinfo;

/* some extra prototypes (defintions are below) */
static void ParentSigHandler(int sig, siginfo_t * info, void *data);
static int SetParentSigHandler(void);
static int server_setup(serverConfig_t *conf);

int SetParentSigHandler()
{
	struct sigaction act;

	/* init & install signal handlers */
	memset(&act, 0, sizeof(act));

	act.sa_sigaction = ParentSigHandler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_SIGINFO | SA_NOCLDSTOP;

	sigaction(SIGCHLD,	&act, 0);
	sigaction(SIGINT,	&act, 0);
	sigaction(SIGQUIT,	&act, 0);
	sigaction(SIGILL,	&act, 0);
	sigaction(SIGBUS,	&act, 0);
	sigaction(SIGFPE,	&act, 0);
	sigaction(SIGSEGV,	&act, 0); 
	sigaction(SIGTERM,	&act, 0);
	sigaction(SIGHUP, 	&act, 0);
	sigaction(SIGUSR1,	&act, 0);

	return 0;
}

int server_setup(serverConfig_t *conf)
{
	ParentPID = getpid();
	Restart = 0;
	GeneralStopRequested = 0;
	get_sigchld = 0;
	SetParentSigHandler();
	
	childinfo.maxConnect	= conf->childMaxConnect;
	childinfo.listenSockets	= g_memdup(conf->listenSockets, conf->ipcount * sizeof(int));
	childinfo.numSockets   	= conf->ipcount;
	childinfo.timeout 	= conf->timeout;
	childinfo.ClientHandler	= conf->ClientHandler;
	childinfo.timeoutMsg	= conf->timeoutMsg;
	childinfo.resolveIP	= conf->resolveIP;

	return 0;
}
	
int StartCliServer(serverConfig_t * conf)
{
	if (!conf)
		TRACE(TRACE_FATAL, "NULL configuration");
	
	if (server_setup(conf))
		return -1;
	
	manage_start_cli_server(&childinfo);
	
	return 0;
}

int StartServer(serverConfig_t * conf)
{
	int stopped = 0;
	pid_t chpid;

	if (!conf)
		TRACE(TRACE_FATAL, "NULL configuration");

	if (server_setup(conf))
		return -1;
 	
 	scoreboard_new(conf);

	if (db_connect() != DM_SUCCESS) 
		TRACE(TRACE_FATAL, "Unable to connect to database.");
	
	if (db_check_version() != 0) {
		db_disconnect();
		TRACE(TRACE_FATAL, "Unsupported database version.");
	}
	
 	manage_start_children();
 	manage_spare_children();
 	
 	TRACE(TRACE_DEBUG, "starting main service loop");
 	while (!GeneralStopRequested) {
		if(get_sigchld){
			get_sigchld = 0;
			while((chpid = waitpid(-1,(int*)NULL,WNOHANG)) > 0) 
				scoreboard_release(chpid);
		}

		if (mainStatus) {
			mainStatus = 0;
			scoreboard_state();
		}

		if (db_check_connection() != 0) {
			
			if (! stopped) 
				manage_stop_children();
		
			stopped=1;
			sleep(10);
			
		} else {
			if (stopped) {
				manage_start_children();
				stopped=0;
			}
			
			manage_spare_children();
			sleep(1);
		}
	}
   
 	manage_stop_children();

	return Restart;
}

pid_t server_daemonize(serverConfig_t *conf)
{
	int serr;
	assert(conf);
	
	if (fork())
		exit(0);
	setsid();
	if (fork())
		exit(0);

	chdir("/");
	umask(0);
	
	if (! (freopen(conf->log, "a", stdout))) {
		serr = errno;
		TRACE(TRACE_FATAL, "freopen failed on [%s] [%s]", 
				conf->log, strerror(serr));
	}
	if (! (freopen(conf->error_log, "a", stderr))) {
		serr = errno;
		TRACE(TRACE_FATAL, "freopen failed on [%s] [%s]", 
				conf->error_log, strerror(serr));
	}
	if (! (freopen("/dev/null", "r", stdin))) {
		serr = errno;
		TRACE(TRACE_FATAL, "freopen failed on stdin [%s]",
				strerror(serr));
	}

	TRACE(TRACE_DEBUG, "sid: [%d]", getsid(0));

	return getsid(0);
}

static void close_all_sockets(serverConfig_t *conf)
{
	int i;

	for (i = 0; i < conf->ipcount; i++) {
		close(conf->listenSockets[i]);
	}
}

int server_run(serverConfig_t *conf)
{
	mainStop = 0;
	mainRestart = 0;
	mainStatus = 0;
	mainSig = 0;
	int serrno, status, result = 0;
	pid_t pid = -1;

	CreateSocket(conf);

	switch ((pid = fork())) {
	case -1:
		serrno = errno;
		close_all_sockets(conf);
		TRACE(TRACE_FATAL, "fork failed [%s]", strerror(serrno));
		errno = serrno;
		break;

	case 0:
		/* child process */
		isChildProcess = 1;
		drop_privileges(conf->serverUser, conf->serverGroup);
		result = StartServer(conf);
		TRACE(TRACE_INFO, "server done, restart = [%d]",
				result);
		exit(result);		
		break;
	default:
		/* parent process, wait for child to exit */
		while (waitpid(pid, &status, WNOHANG | WUNTRACED) == 0) {
			if (mainStop || mainRestart || mainStatus){
				TRACE(TRACE_DEBUG, "MainSigHandler(): got signal [%d]", mainSig);
				if(mainStop) kill(pid, SIGTERM);
				if(mainRestart) kill(pid, SIGHUP);
				if(mainStatus) {
					mainStatus = 0;
					kill(pid, SIGUSR1);
				}
			}
			sleep(2);
		}

		if (WIFEXITED(status)) {
			/* child process terminated neatly */
			result = WEXITSTATUS(status);
			TRACE(TRACE_DEBUG, "server has exited, exit status [%d]",
			      result);
		} else {
			/* child stopped or signaled so make sure it is dead */
			TRACE(TRACE_DEBUG, "server has not exited normally. Killing...");

			kill(pid, SIGKILL);
			result = 0;
		}

		if (strlen(conf->socket) > 0) {
			if (unlink(conf->socket)) {
				serrno = errno;
				TRACE(TRACE_ERROR, "unlinking unix socket failed [%s]",
						strerror(serrno));
				errno = serrno;
			}
		}
 

		break;
	}
	
	close_all_sockets(conf);
	
	return result;
}

void ParentSigHandler(int sig, siginfo_t * info UNUSED, void *data UNUSED)
{
	int saved_errno = errno;
	Restart = 0;
	
	/* this call is for a child but it's handler is not yet installed */
	/*
	if (ParentPID != getpid())
		active_child_sig_handler(sig, info, data); 

	*/ 
	switch (sig) {

	case SIGCHLD:
		/* ignore, wait for child in main loop */
		/* but we need to catch zombie */
		get_sigchld = 1;
		break;		

	case SIGSEGV:
		sleep(60);
		_exit(1);
		break;

	case SIGHUP:
		Restart = 1;
		GeneralStopRequested = 1;
		break;

	case SIGUSR1:
		mainStatus = 1;
		break;
		
	default:
		GeneralStopRequested = 1;
		break;
	}

	errno = saved_errno;
}

static int dm_socket(int domain)
{
	int sock, err;
	if ((sock = socket(domain, SOCK_STREAM, 0)) == -1) {
		err = errno;
		TRACE(TRACE_FATAL, "%s", strerror(err));
	}
	TRACE(TRACE_DEBUG, "done");
	return sock;
}

static int dm_bind_and_listen(int sock, struct sockaddr *saddr, socklen_t len, int backlog)
{
	int err;
	/* bind the address */
	if ((bind(sock, saddr, len)) == -1) {
		err = errno;
		TRACE(TRACE_DEBUG, "failed");
		return err;
	}

	if ((listen(sock, backlog)) == -1) {
		err = errno;
		TRACE(TRACE_DEBUG, "failed");
		return err;
	}
	
	TRACE(TRACE_DEBUG, "done");
	return 0;
	
}

static int create_unix_socket(serverConfig_t * conf)
{
	int sock, err;
	struct sockaddr_un saServer;

	conf->resolveIP=0;

	sock = dm_socket(PF_UNIX);

	/* setup sockaddr_un */
	memset(&saServer, 0, sizeof(saServer));
	saServer.sun_family = AF_UNIX;
	strncpy(saServer.sun_path,conf->socket, sizeof(saServer.sun_path));

	TRACE(TRACE_DEBUG, "creating socket on [%s] with backlog [%d]",
			conf->socket, conf->backlog);

	err = dm_bind_and_listen(sock, (struct sockaddr *)&saServer, sizeof(saServer), conf->backlog);
	if (err != 0) {
		close(sock);
		TRACE(TRACE_FATAL, "Fatal error, could not bind to [%s] %s",
			conf->socket, strerror(err));
	}
	
	chmod(conf->socket, 02777);

	return sock;
}

static int create_inet_socket(const char * const ip, int port, int backlog)
{
	int sock, err;
	struct sockaddr_in saServer;
	int so_reuseaddress = 1;

	sock = dm_socket(PF_INET);
	
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddress, sizeof(so_reuseaddress));

	/* setup sockaddr_in */
	memset(&saServer, 0, sizeof(saServer));
	saServer.sin_family	= AF_INET;
	saServer.sin_port	= htons(port);

	TRACE(TRACE_DEBUG, "creating socket on [%s:%d] with backlog [%d]",
			ip, port, backlog);
	
	if (ip[0] == '*') {
		
		saServer.sin_addr.s_addr = htonl(INADDR_ANY);
		
	} else if (! (inet_aton(ip, &saServer.sin_addr))) {
		
		close(sock);
		TRACE(TRACE_FATAL, "IP invalid [%s]", ip);
	}

	err = dm_bind_and_listen(sock, (struct sockaddr *)&saServer, sizeof(saServer), backlog);
	if (err != 0) {
		close(sock);
		TRACE(TRACE_FATAL, "Fatal error, could not bind to [%s:%d] %s",
			ip, port, strerror(err));
	}

	return sock;	
}

void CreateSocket(serverConfig_t * conf)
{
	int i;

	conf->listenSockets = g_new0(int, conf->ipcount);

	if (strlen(conf->socket) > 0) {
		conf->listenSockets[0] = create_unix_socket(conf);
	} else {
		for (i = 0; i < conf->ipcount; i++) {
			conf->listenSockets[i] = create_inet_socket(conf->iplist[i], conf->port, conf->backlog);
		}
	}
}

