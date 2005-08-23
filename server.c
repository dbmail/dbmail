/*
  $Id: server.c 1823 2005-07-23 03:16:25Z aaron $
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl

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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "db.h"
#include "debug.h"
#include "server.h"
#include "pool.h"
#include "serverchild.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>


int GeneralStopRequested = 0;
int Restart = 0;
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
	sigaction(SIGALRM,	&act, 0);
	sigaction(SIGHUP, 	&act, 0);

	return 0;
}

int server_setup(serverConfig_t *conf)
{
	if (db_connect() != 0) 
		return -1;
	
	if (db_check_version() != 0) {
		db_disconnect();
		return -1;
	}
	
	db_disconnect();

	ParentPID = getpid();
	Restart = 0;
	GeneralStopRequested = 0;
	SetParentSigHandler();
	
	childinfo.maxConnect	= conf->childMaxConnect;
	childinfo.listenSocket	= conf->listenSocket;
	childinfo.timeout 	= conf->timeout;
	childinfo.ClientHandler	= conf->ClientHandler;
	childinfo.timeoutMsg	= conf->timeoutMsg;
	childinfo.resolveIP	= conf->resolveIP;

	return 0;
}
	
int StartCliServer(serverConfig_t * conf)
{
	if (!conf)
		trace(TRACE_FATAL, "%s,%s: NULL configuration", __FILE__, __func__);
	
	if (server_setup(conf))
		return -1;
	
	manage_start_cli_server(&childinfo);
	
	return 0;
}

int StartServer(serverConfig_t * conf)
{
	int stopped = 0;
	if (!conf)
		trace(TRACE_FATAL, "%s,%s: NULL configuration", __FILE__, __func__);

	if (server_setup(conf))
		return -1;
 	
 	scoreboard_new(conf);
	
 	manage_start_children();
 	manage_spare_children();
 	
	alarm(10);
  
 	trace(TRACE_DEBUG, "%s,%s: starting main service loop", __FILE__, __func__);
 	while (!GeneralStopRequested) {
		if (db_check_connection() != 0) {
			if (! stopped) {
				alarm(0);
				trace(TRACE_MESSAGE,"%s,%s: entering sleep-mode until database is back again",
					__FILE__, __func__);
				manage_stop_children();
			}
			stopped=1;
			sleep(10);
		} else {
			if (stopped) {
				trace(TRACE_MESSAGE,"%s,%s: resume operation now the database is back again",
					__FILE__, __func__);
				manage_spare_children();
				alarm(10);
			}

			stopped=0;
 			manage_restart_children();
		}
	}
   
 	manage_stop_children();
 	scoreboard_delete();

	if (strlen(conf->socket) > 0)
		unlink(conf->socket);
 
	return Restart;
}


void ParentSigHandler(int sig, siginfo_t * info, void *data)
{
	pid_t chpid;
	int saved_errno = errno;
	
	if (ParentPID != getpid()) {
		trace(TRACE_INFO, "%s,%s: no longer parent", __FILE__, __func__);
		/* this call is for a child but it's handler is not yet installed */
		active_child_sig_handler(sig, info, data); 
	}
	
	if (sig != SIGALRM) 
		trace(TRACE_INFO, "%s,%s: %s", __FILE__, __func__, strsignal(sig));
	
	switch (sig) {
	case SIGALRM:
		manage_spare_children();
		alarm(10);
		break;
 
	case SIGCHLD:
		/* ignore, wait for child in main loop */
		/* but we need to catch zombie */
		if ((chpid = waitpid(-1,&sig,WNOHANG)) > 0)
			scoreboard_release(chpid);
		break;		

	case SIGSEGV:
		sleep(60);
		exit(1);
		break;

	case SIGHUP:
		Restart = 1;
		trace(TRACE_DEBUG, "%s,%s: SIGHUP, Restart set", __FILE__, __func__);
		/* fall-through */
	default:
		GeneralStopRequested = 1;
	}

	errno = saved_errno;
}

static int dm_socket(int domain)
{
	int sock, err;
	if ((sock = socket(domain, SOCK_STREAM, 0)) == -1) {
		err = errno;
		trace(TRACE_FATAL, "%s,%s: %s", __FILE__, __func__, strerror(err));
	}
	trace(TRACE_DEBUG, "%s,%s: done", __FILE__, __func__);
	return sock;
}

static int dm_bind_and_listen(int sock, struct sockaddr *saddr, socklen_t len)
{
	int err;
	/* bind the address */
	if ((bind(sock, saddr, len)) == -1) {
		err = errno;
		close(sock);
		trace(TRACE_FATAL, "%s,%s: %s", __FILE__, __func__, strerror(err));
	}

	if ((listen(sock, BACKLOG)) == -1) {
		err = errno;
		close(sock);
		trace(TRACE_FATAL, "%s,%s: %s", __FILE__, __func__, strerror(err));
	}
	
	trace(TRACE_DEBUG, "%s,%s: done", __FILE__, __func__);
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

	dm_bind_and_listen(sock, (struct sockaddr *)&saServer, sizeof(saServer));
	
	chmod (conf->socket,02777);

	return sock;
}

static int create_inet_socket(serverConfig_t * conf)
{
	int sock;
	struct sockaddr_in saServer;
	int so_reuseaddress = 1;

	sock = dm_socket(PF_INET);
	
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddress, sizeof(so_reuseaddress));

	/* setup sockaddr_in */
	memset(&saServer, 0, sizeof(saServer));
	saServer.sin_family	= AF_INET;
	saServer.sin_port	= htons(conf->port);
	
	if (conf->ip[0] == '*') {
		
		saServer.sin_addr.s_addr = htonl(INADDR_ANY);
		
	} else if (! (inet_aton(conf->ip, &saServer.sin_addr))) {
		
		close(sock);
		trace(TRACE_FATAL, "%s,%s: IP invalid [%s]",
				__FILE__, __func__, conf->ip);
	}

	dm_bind_and_listen(sock, (struct sockaddr *)&saServer, sizeof(saServer));

	return sock;	
}

int CreateSocket(serverConfig_t * conf)
{
	if (strlen(conf->socket) > 0) 
		conf->listenSocket = create_unix_socket(conf);
	else
		conf->listenSocket = create_inet_socket(conf);
	
	return conf->listenSocket;
}

void ClearConfig(serverConfig_t * conf)
{
	memset(conf, 0, sizeof(serverConfig_t));
}

