/*
  $Id$
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
#include <sys/socket.h>
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

int SetParentSigHandler()
{
	struct sigaction act;

	/* init & install signal handlers */
	memset(&act, 0, sizeof(act));

	act.sa_sigaction = ParentSigHandler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_SIGINFO; 

	sigaction(SIGCHLD, &act, 0);
	sigaction(SIGINT, &act, 0);
	sigaction(SIGQUIT, &act, 0);
	sigaction(SIGILL, &act, 0);
	sigaction(SIGBUS, &act, 0);
	sigaction(SIGFPE, &act, 0);
	sigaction(SIGSEGV, &act, 0);
	sigaction(SIGTERM, &act, 0);
	sigaction(SIGALRM, &act, 0);
	sigaction(SIGHUP, &act, 0);

	return 0;
}

static void server_setup(serverConfig_t *conf)
{
	ParentPID = getpid();
	Restart = 0;
	GeneralStopRequested = 0;
	SetParentSigHandler();
	
	childinfo.maxConnect = conf->childMaxConnect;
	childinfo.listenSocket = conf->listenSocket;
	childinfo.timeout = conf->timeout;
	childinfo.ClientHandler = conf->ClientHandler;
	childinfo.timeoutMsg = conf->timeoutMsg;
	childinfo.resolveIP = conf->resolveIP;
}
	
int StartCliServer(serverConfig_t * conf)
{
	if (!conf)
		trace(TRACE_FATAL, "%s,%s: NULL configuration", __FILE__, __func__);
	
	server_setup(conf);	
	manage_start_cli_server(&childinfo);
	
	return Restart;
}



int StartServer(serverConfig_t * conf)
{
	if (!conf)
		trace(TRACE_FATAL, "%s,%s: NULL configuration", __FILE__, __func__);

	server_setup(conf);
 	
 	scoreboard_new(conf);
	
 	manage_start_children();
 	manage_spare_children();
 	
	alarm(10);
  
 	trace(TRACE_DEBUG, "%s,%s: starting main service loop", __FILE__, __func__);
 	while (!GeneralStopRequested) 
 		manage_restart_children();
   
 	manage_stop_children();
 	scoreboard_delete();
 
	return Restart;
}


void ParentSigHandler(int sig, siginfo_t * info, void *data)
{
	if (ParentPID != getpid()) {
		trace(TRACE_INFO, "%s,%s: no longer parent", __FILE__, __func__);
		/* this call is for a child but it's handler is not yet installed */
		active_child_sig_handler(sig, info, data); 
	}
	
	if (sig != SIGALRM) {
#ifdef _USE_STR_SIGNAL
		trace(TRACE_INFO, "%s,%s: got signal [%s]", __FILE__, __func__, 
				strsignal(sig));
#else
		trace(TRACE_INFO, "%s,%s: got signal [%d]", __FILE__, __func__, 
				sig);
#endif
	}
	
	switch (sig) {
	case SIGALRM:
		manage_spare_children();
		alarm(10);
		break;
 
	case SIGCHLD:	/* ignore, wait for child in main loop */
		break;		

	case SIGHUP:
		Restart = 1;
		trace(TRACE_DEBUG, "%s,%s: SIGHUP, Restart set", __FILE__, __func__);
		/* fall-through */
	default:
		GeneralStopRequested = 1;
	}
}


int CreateSocket(serverConfig_t * conf)
{
	int sock, err;
	struct sockaddr_in saServer;
	/* set reuseaddr, so address will be reused */
	int so_reuseaddress = 1;

	/* make a tcp/ip socket */
	if ((sock = socket(PF_INET, SOCK_STREAM, 0)) == -1) {
		err = errno;
		trace(TRACE_FATAL, "%s,%s: %s", __FILE__, __func__, strerror(err));
	}

	trace(TRACE_DEBUG, "%s,%s: socket created", __FILE__, __func__);

	/* make an (socket)address */
	memset(&saServer, 0, sizeof(saServer));
	
	saServer.sin_family	= AF_INET;
	saServer.sin_port	= htons(conf->port);

	if (conf->ip[0] == '*') {
		saServer.sin_addr.s_addr = htonl(INADDR_ANY);
	} else {
		if (! (inet_aton(conf->ip, &saServer.sin_addr))) {
			close(sock);
			trace(TRACE_FATAL, "%s,%s: IP invalid [%s]", 
					__FILE__, __func__, 
					conf->ip);
		}
	}
	trace(TRACE_DEBUG, "%s,%s: IP ok [%s]", __FILE__, __func__, conf->ip);

	/* set socket option: reuse address */
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, 
			&so_reuseaddress, sizeof(so_reuseaddress));

	/* bind the address */
	if ((bind(sock, (struct sockaddr *) &saServer, sizeof(saServer))) == -1) {
		err = errno;
		close(sock);
		trace(TRACE_FATAL, "%s,%s: %s", __FILE__, __func__, strerror(err));
	}
	trace(TRACE_DEBUG, "%s,%s: bind ok", __FILE__, __func__);

	if ((listen(sock, BACKLOG)) == -1) {
		err = errno;
		close(sock);
		trace(TRACE_FATAL, "%s,%s: %s", __FILE__, __func__, strerror(err));
	}
	
	conf->listenSocket = sock;
	
	trace(TRACE_INFO, "%s,%s: socket complete", __FILE__, __func__);

	return 0;
}

