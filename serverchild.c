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
 * serverchild.c
 *
 * function implementations of server children code (connection handling)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "debug.h"
#include "serverchild.h"
#include "db.h"
#include "auth.h"
#include "clientinfo.h"
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
#ifdef PROC_TITLES
#include "proctitleutils.h"
#endif

int ChildStopRequested = 0;
int connected = 0;
clientinfo_t client;

int PerformChildTask(ChildInfo_t * info);


void ChildSigHandler(int sig, siginfo_t * info UNUSED, void *data UNUSED)
{
	static int triedDisconnect = 0;

#ifdef _USE_STR_SIGNAL
	trace(TRACE_ERROR, "ChildSighandler(): got signal [%s]",
	      strsignal(sig));
#else
	trace(TRACE_ERROR, "ChildSighandler(): got signal [%d]", sig);
#endif

	/* perform reinit at SIGHUP otherwise exit, but do nothing on
	 *  SIGCHLD*/
	switch (sig) {
	case SIGCHLD:
		trace(TRACE_DEBUG, "ChildSighandler(): SIGCHLD received..."
		      "ignoring");
		break;
	case SIGALRM:
		trace(TRACE_DEBUG, "ChildSighandler(): timeout received");

		if (client.tx) {
			trace(TRACE_DEBUG,
			      "ChildSighandler(): write stream open, closing");
			fprintf(client.tx, "%s", client.timeoutMsg);
			fflush(client.tx);
			fclose(client.tx);	/* closes clientSocket as well */
			client.tx = NULL;
		}

		if (client.rx) {
			trace(TRACE_DEBUG,
			      "ChildSighandler(): read stream open, closing");
			shutdown(fileno(client.rx), SHUT_RDWR);
			fclose(client.rx);
			client.rx = NULL;
		}

		break;

	case SIGHUP:
	case SIGTERM:
	case SIGQUIT:
	case SIGSTOP:
		if (ChildStopRequested) {
			trace(TRACE_DEBUG,
			      "ChildSighandler(): already caught a stop request. Closing right now");

			/* already caught this signal, exit the hard way now */
			if (client.tx) {
				trace(TRACE_DEBUG,
				      "ChildSighandler(): write stream still open, closing");
				fflush(client.tx);
				fclose(client.tx);	/* closes clientSocket as well */
				client.tx = NULL;
			}

			if (client.rx) {
				trace(TRACE_DEBUG,
				      "ChildSighandler(): read stream still open, closing");
				shutdown(fileno(client.rx), SHUT_RDWR);
				fclose(client.rx);
				client.rx = NULL;
			}

			if (connected) {
				trace(TRACE_DEBUG,
				      "ChildSighandler(): database connection still open, closing");
				db_disconnect();
				auth_disconnect();
				connected = 0;
			}

			trace(TRACE_DEBUG, "ChildSighandler(): exit");
			exit(1);
		}
		trace(TRACE_DEBUG,
		      "ChildSighandler(): setting stop request");
		ChildStopRequested = 1;
		break;
	default:
		/* bad shtuff, exit */
		trace(TRACE_DEBUG,
		      "ChildSighandler(): cannot ignore this. Terminating");

		/*
		 * For some reason i have not yet determined the process starts eating up
		 * all CPU time when trying to disconnect.
		 * For now: just bail out :-)
		 */
		_exit(1);

		if (!triedDisconnect) {
			triedDisconnect = 1;

			if (client.tx) {
				trace(TRACE_DEBUG,
				      "ChildSighandler(): write stream still open, closing");
				fflush(client.tx);
				fclose(client.tx);	/* closes clientSocket as well */
				client.tx = NULL;
			}

			if (client.rx) {
				trace(TRACE_DEBUG,
				      "ChildSighandler(): read stream still open, closing");
				shutdown(fileno(client.rx), SHUT_RDWR);
				fclose(client.rx);
				client.rx = NULL;
			}

			if (connected) {
				trace(TRACE_DEBUG,
				      "ChildSighandler(): database connection still open, closing");
				db_disconnect();
				auth_disconnect();
			}

			connected = 0;
		}

		trace(TRACE_DEBUG, "ChildSighandler(): exit");
		exit(1);
	}
}



/*
 * SetChildSigHandler()
 * 
 * sets the signal handler for a child proces
 */
int SetChildSigHandler()
{
	struct sigaction act;

	/* init & install signal handlers */
	memset(&act, 0, sizeof(act));

	act.sa_sigaction = ChildSigHandler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_SIGINFO;

	sigaction(SIGINT, &act, 0);
	sigaction(SIGQUIT, &act, 0);
	sigaction(SIGILL, &act, 0);
	sigaction(SIGBUS, &act, 0);
	sigaction(SIGPIPE, &act, 0);
	sigaction(SIGFPE, &act, 0);
	sigaction(SIGSEGV, &act, 0);
	sigaction(SIGTERM, &act, 0);
	sigaction(SIGHUP, &act, 0);
	sigaction(SIGALRM, &act, 0);
	sigaction(SIGCHLD, &act, 0);

	return 0;
}


/*
 * CreateChild()
 *
 * creates a new child, returning only to the parent process
 */
pid_t CreateChild(ChildInfo_t * info)
{
	pid_t pid = fork();

	if (pid != 0)
		return pid;

	ChildStopRequested = 0;
	SetChildSigHandler();
	trace(TRACE_INFO,
	      "CreateChild(): signal handler placed, going to perform task now");

	PerformChildTask(info);

	usleep(10000);
	exit(0);
}


/*
 * checks if a child is still alive (i.e. exists & not a zombie)
 * FIXME seems to be not functional (kill succeeds on zombies)
 */
int CheckChildAlive(pid_t pid)
{
	return (kill(pid, 0) == -1) ? 0 : 1;
}


int PerformChildTask(ChildInfo_t * info)
{
	int i, len, clientSocket;
	struct sockaddr_in saClient;
	struct hostent *clientHost;

	if (!info) {
		trace(TRACE_ERROR,
		      "PerformChildTask(): NULL info supplied");
		return -1;
	}

	if (db_connect() != 0) {
		trace(TRACE_ERROR,
		      "PerformChildTask(): could not connect to database");
		return -1;
	}

	if (auth_connect() != 0) {
		trace(TRACE_ERROR,
		      "PerformChildTask(): could not connect to authentication");
		return -1;
	}

	srand((int) ((int) time(NULL) + (int) getpid()));
	connected = 1;

	for (i = 0; i < info->maxConnect && !ChildStopRequested; i++) {
		trace(TRACE_INFO,
		      "PerformChildTask(): waiting for connection");

		/* wait for connect */
		len = sizeof(saClient);
		clientSocket =
		    accept(info->listenSocket,
			   (struct sockaddr *) &saClient, &len);

		if (clientSocket == -1) {
			i--;	/* don't count this as a connect */
			trace(TRACE_INFO,
			      "PerformChildTask(): accept failed");
			continue;	/* accept failed, refuse connection & continue */
		}

		memset(&client, 0, sizeof(client));	/* zero-init */

		client.timeoutMsg = info->timeoutMsg;
		client.timeout = info->timeout;
		strncpy(client.ip, inet_ntoa(saClient.sin_addr),
			IPNUM_LEN);
		client.clientname[0] = '\0';

		if (info->resolveIP) {
			clientHost =
			    gethostbyaddr((char *) &saClient.sin_addr,
					  sizeof(saClient.sin_addr),
					  saClient.sin_family);

			if (clientHost && clientHost->h_name)
				strncpy(client.clientname,
					clientHost->h_name, FIELDSIZE);

			trace(TRACE_MESSAGE,
			      "PerformChildTask(): incoming connection from [%s (%s)]",
			      client.ip,
			      client.clientname[0] ? client.
			      clientname : "Lookup failed");
		} else {
			trace(TRACE_MESSAGE,
			      "PerformChildTask(): incoming connection from [%s]",
			      client.ip);
		}

		/* make streams */
		if (!(client.rx = fdopen(dup(clientSocket), "r"))) {
			/* read-FILE opening failure */
			trace(TRACE_ERROR,
			      "PerformChildTask(): error opening read file stream");
			close(clientSocket);
			continue;
		}

		if (!(client.tx = fdopen(clientSocket, "w"))) {
			/* write-FILE opening failure */
			trace(TRACE_ERROR,
			      "PerformChildTask(): error opening write file stream");
			fclose(client.rx);
			close(clientSocket);
			memset(&client, 0, sizeof(client));
			continue;
		}

		setvbuf(client.tx, (char *) NULL, _IOLBF, 0);
		setvbuf(client.rx, (char *) NULL, _IOLBF, 0);

		trace(TRACE_DEBUG,
		      "PerformChildTask(): client info init complete, calling client handler");

		/* streams are ready, perform handling */
		info->ClientHandler(&client);
#ifdef PROC_TITLES
		set_proc_title("%s", "Idle");
#endif

		trace(TRACE_DEBUG,
		      "PerformChildTask(): client handling complete, closing streams");

		if (client.tx) {
			fflush(client.tx);
			fclose(client.tx);	/* closes clientSocket as well */
			client.tx = NULL;
		}

		if (client.rx) {
			shutdown(fileno(client.rx), SHUT_RDWR);
			fclose(client.rx);
			client.rx = NULL;
		}

		trace(TRACE_INFO, "PerformChildTask(): connection closed");
	}

	db_disconnect();
	auth_disconnect();
	connected = 0;		/* FIXME a signal between this line and the previous one 
				 * would screw things up. Would like to have all this in
				 * db_disconnect() making 'connected' obsolete
				 */

	if (!ChildStopRequested)
		trace(TRACE_ERROR,
		      "PerformChildTask(): maximum number of connections reached, stopping now");
	else
		trace(TRACE_ERROR, "PerformChildTask(): stop requested");

	return 0;
}
