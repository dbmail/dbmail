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


int StartServer(serverConfig_t * conf)
{
	if (!conf)
		trace(TRACE_FATAL, "StartServer(): NULL configuration");

	trace(TRACE_DEBUG, "StartServer(): init");

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
 	
 	trace(TRACE_DEBUG, "StartServer(): init ok. Creating children..");
 	scoreboard_new(conf);
 	manage_start_children();
 	manage_spare_children();
 	alarm(10);
  
 	trace(TRACE_DEBUG, "StartServer(): children created, starting main service loop");
 	while (!GeneralStopRequested) 
 		manage_restart_children();
   
 	manage_stop_children();
 	scoreboard_delete();
 
	return Restart;
}


void ParentSigHandler(int sig, siginfo_t * info, void *data)
{
	if (ParentPID != getpid()) {
		trace(TRACE_INFO,
		      "ParentSigHandler(): i'm no longer father");
		active_child_sig_handler(sig, info, data); /* this call is for a child but it's handler is not yet installed */
	}
	
	if (sig != SIGALRM) {
#ifdef _USE_STR_SIGNAL
		trace(TRACE_INFO, "ParentSigHandler(): got signal [%s]",
		      strsignal(sig));
#else
		trace(TRACE_INFO, "ParentSigHandler(): got signal [%d]", sig);
#endif
	}
	
	switch (sig) {
	case SIGALRM:
		manage_spare_children();
		alarm(10);
		break;
 
	case SIGCHLD:
		break;		/* ignore, wait for child in main loop */

	case SIGHUP:
		trace(TRACE_DEBUG,
		      "ParentSigHandler(): SIGHUP, setting Restart");
		Restart = 1;

	default:
		GeneralStopRequested = 1;
	}
}


int CreateSocket(serverConfig_t * conf)
{
	int sock, r, len;
	struct sockaddr_in saServer;
	int so_reuseaddress = 1;
			   /**< reuseaddr to 1, so address will be reused */

	/* make a tcp/ip socket */
	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock == -1)
		trace(TRACE_FATAL,
		      "CreateSocket(): socket creation failed [%s]",
		      strerror(errno));

	trace(TRACE_DEBUG, "CreateSocket(): socket created");

	/* make an (socket)address */
	memset(&saServer, 0, sizeof(saServer));

	saServer.sin_family = AF_INET;
	saServer.sin_port = htons(conf->port);

	if (conf->ip[0] == '*')
		saServer.sin_addr.s_addr = htonl(INADDR_ANY);
	else {
		r = inet_aton(conf->ip, &saServer.sin_addr);
		if (!r) {
			close(sock);
			trace(TRACE_FATAL,
			      "CreateSocket(): invalid IP [%s]", conf->ip);
		}
	}

	trace(TRACE_DEBUG, "CreateSocket(): socket IP requested [%s] OK",
	      conf->ip);

	/* set socket option: reuse address */
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddress,
		   sizeof(so_reuseaddress));

	/* bind the address */
	len = sizeof(saServer);
	r = bind(sock, (struct sockaddr *) &saServer, len);

	if (r == -1) {
		close(sock);
		trace(TRACE_FATAL,
		      "CreateSocket(): could not bind address to socket");
	}

	trace(TRACE_DEBUG, "CreateSocket(): IP bound to socket");

	r = listen(sock, BACKLOG);
	if (r == -1) {
		close(sock);
		trace(TRACE_FATAL,
		      "CreateSocket(): error making socket listen [%s]",
		      strerror(errno));
	}

	trace(TRACE_INFO, "CreateSocket(): socket creation complete");
	conf->listenSocket = sock;

	return 0;
}

