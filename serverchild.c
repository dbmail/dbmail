/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2005-2006 NFG Net Facilities Group BV support@nfg.nl

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
 * $Id: serverchild.c 2057 2006-04-01 18:12:11Z paul $
 * 
 * function implementations of server children code (connection handling)
 */

#include "dbmail.h"

int ChildStopRequested = 0;
int connected = 0;
volatile clientinfo_t client;

static void client_close(void);
static void disconnect_all(void);

int PerformChildTask(ChildInfo_t * info);

void client_close(void)
{
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
}

void disconnect_all(void)
{
	if (! connected)
		return;
	
	db_disconnect();
	auth_disconnect();
	connected = 0;
}

void noop_child_sig_handler(int sig, siginfo_t *info UNUSED, void *data UNUSED)
{
	int saved_errno = errno;
	
	if (sig == SIGSEGV)
		_exit(0);
	
	trace(TRACE_DEBUG, "%s,%s: ignoring signal [%d]", __FILE__, __func__, sig);

	errno = saved_errno;
}

void active_child_sig_handler(int sig, siginfo_t * info UNUSED, void *data UNUSED)
{
	int saved_errno = errno;
	
	/* perform reinit at SIGHUP otherwise exit, but do nothing on
	 *  SIGCHLD*/
	switch (sig) {
	case SIGCHLD:
		break;
	case SIGALRM:
		client_close();
		break;

	case SIGHUP:
	case SIGTERM:
	case SIGQUIT:
	case SIGSTOP:
		if (ChildStopRequested) {
			client_close();
			disconnect_all();
			child_unregister();
			exit(1);
		}
		DelChildSigHandler();
	 	ChildStopRequested = 1;
		break;

	default:
		child_unregister();
		_exit(1);
	}

	errno = saved_errno;
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

	act.sa_sigaction = active_child_sig_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_SIGINFO;

	sigaction(SIGINT,	&act, 0);
	sigaction(SIGQUIT,	&act, 0);
	sigaction(SIGILL,	&act, 0);
	sigaction(SIGBUS,	&act, 0);
	sigaction(SIGFPE,	&act, 0);
	sigaction(SIGSEGV,	&act, 0);
	sigaction(SIGTERM,	&act, 0);
	sigaction(SIGHUP,	&act, 0);
	sigaction(SIGALRM,	&act, 0);
	sigaction(SIGCHLD,	&act, 0);
	return 0;
}
int DelChildSigHandler()
{
	struct sigaction act;

	/* init & install signal handlers */
	memset(&act, 0, sizeof(act));

	act.sa_sigaction = noop_child_sig_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_SIGINFO;

	sigaction(SIGINT,	&act, 0);
	sigaction(SIGQUIT,	&act, 0);
	sigaction(SIGILL,	&act, 0);
	sigaction(SIGBUS,	&act, 0);
	//sigaction(SIGPIPE,	&act, 0);
	sigaction(SIGFPE,	&act, 0);
	sigaction(SIGSEGV,	&act, 0);
	sigaction(SIGTERM,	&act, 0);
	sigaction(SIGHUP,	&act, 0);
	sigaction(SIGALRM,	&act, 0);
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

	if (! pid) {
		if (child_register() == -1) {
			trace(TRACE_FATAL, "%s,%s: child_register failed", 
				__FILE__, __func__);
			exit(0);
		}
	
 		ChildStopRequested = 0;
 		SetChildSigHandler();
		
 		trace(TRACE_INFO, "%s,%s: signal handler placed, going to perform task now",
			__FILE__, __func__);
 		
		if (PerformChildTask(info) == -1)
			return -1;
		
 		child_unregister();
 		exit(0);
	} else {
 		usleep(5000);
 		return pid;
	}
}

int PerformChildTask(ChildInfo_t * info)
{
	int i, len, serr, clientSocket;
	struct sockaddr_in saClient;
	struct hostent *clientHost;

	if (!info) {
		trace(TRACE_ERROR, "%s,%s: NULL info supplied", 
				__FILE__, __func__);
		return -1;
	}

	if (db_connect() != 0) {
		trace(TRACE_ERROR, "%s,%s: could not connect to database", 
				__FILE__, __func__);
		return -1;
	}

	if (auth_connect() != 0) {
		trace(TRACE_ERROR, "%s,%s: could not connect to authentication", 
				__FILE__, __func__);
		return -1;
	}

	srand((int) ((int) time(NULL) + (int) getpid()));
	connected = 1;

	
	for (i = 0; i < info->maxConnect && !ChildStopRequested; i++) {
		if (db_check_connection()) {
			trace(TRACE_ERROR, "%s,%s: database has gone away", 
					__FILE__, __func__);
			ChildStopRequested=1;
			continue;
		}

		trace(TRACE_INFO, "%s,%s: waiting for connection", 
				__FILE__, __func__);

		child_reg_disconnected();

		/* wait for connect */
		len = sizeof(saClient);
		clientSocket = accept(info->listenSocket, (struct sockaddr *) &saClient, (socklen_t *)&len);

		if (clientSocket == -1) {
			serr = errno;
			i--;	/* don't count this as a connect */
			trace(TRACE_INFO, "%s,%s: accept failed [%s]", 
					__FILE__, __func__, strerror(serr));
			errno = serr;
			continue;	/* accept failed, refuse connection & continue */
		}

		child_reg_connected();
		
		memset(&client, 0, sizeof(client));	/* zero-init */

		client.timeoutMsg = info->timeoutMsg;
		client.timeout = info->timeout;
		strncpy(client.ip_src, inet_ntoa(saClient.sin_addr), IPNUM_LEN);
		client.clientname[0] = '\0';
			
		if (info->resolveIP) {
			clientHost = gethostbyaddr((char *) &saClient.sin_addr, 
					sizeof(saClient.sin_addr), saClient.sin_family);

			if (clientHost && clientHost->h_name)
				strncpy(client.clientname, clientHost->h_name, FIELDSIZE);

			trace(TRACE_MESSAGE, "%s,%s: incoming connection from [%s (%s)] by pid [%d]",
					__FILE__, __func__,
			      client.ip_src,
			      client.clientname[0] ? client.clientname : "Lookup failed", getpid());
		} else {
			trace(TRACE_MESSAGE, "%s,%s: incoming connection from [%s] by pid [%d]", 
					__FILE__, __func__,
			      client.ip_src, getpid());
		}
		
		/* make streams */
		if (!(client.rx = fdopen(dup(clientSocket), "r"))) {
			/* read-FILE opening failure */
			trace(TRACE_ERROR, "%s,%s: error opening read file stream", 
					__FILE__, __func__);
			close(clientSocket);
			continue;
		}

		if (!(client.tx = fdopen(clientSocket, "w"))) {
			/* write-FILE opening failure */
			trace(TRACE_ERROR, "%s,%s: error opening write file stream", 
					__FILE__, __func__);
			fclose(client.rx);
			close(clientSocket);
			memset(&client, 0, sizeof(client));
			continue;
		}

		setvbuf(client.tx, (char *) NULL, _IOLBF, 0);
		setvbuf(client.rx, (char *) NULL, _IOLBF, 0);

		trace(TRACE_DEBUG, "%s,%s: client info init complete, calling client handler",
				__FILE__, __func__);

		/* streams are ready, perform handling */
		info->ClientHandler(&client);

		trace(TRACE_DEBUG, "%s,%s: client handling complete, closing streams",
				__FILE__, __func__);
		client_close();
		trace(TRACE_INFO, "%s,%s: connection closed", 
				__FILE__, __func__);
	}

	if (!ChildStopRequested)
		trace(TRACE_ERROR, "%s,%s: maximum number of connections reached, stopping now", 
				__FILE__, __func__);
	else
		trace(TRACE_ERROR, "%s,%s: stop requested", 
				__FILE__, __func__);

	child_reg_disconnected();
	disconnect_all();
	
	return 0;
}

int manage_start_cli_server(ChildInfo_t * info)
{
	if (!info) {
		trace(TRACE_ERROR, "%s,%s: NULL info supplied", 
				__FILE__, __func__);
		return -1;
	}

	if (db_connect() != 0) {
		trace(TRACE_ERROR, "%s,%s: could not connect to database", 
				__FILE__, __func__);
		return -1;
	}

	if (auth_connect() != 0) {
		trace(TRACE_ERROR, "%s,%s: could not connect to authentication", 
				__FILE__, __func__);
		return -1;
	}

	srand((int) ((int) time(NULL) + (int) getpid()));
	connected = 1;

	if (db_check_connection()) {
		trace(TRACE_ERROR, "%s,%s: database has gone away", 
				__FILE__, __func__);
		return -1;
	}

		
	memset(&client, 0, sizeof(client));	/* zero-init */

	client.timeoutMsg = info->timeoutMsg;
	client.timeout = info->timeout;

	/* make streams */
	client.rx = stdin;
	client.tx = stdout;

	setvbuf(client.tx, (char *) NULL, _IOLBF, 0);
	setvbuf(client.rx, (char *) NULL, _IOLBF, 0);

	trace(TRACE_DEBUG,
	      "%s,%s: client info init complete, calling client handler", __FILE__, __func__);

	/* streams are ready, perform handling */
	info->ClientHandler(&client);

	trace(TRACE_DEBUG,
	      "%s,%s: client handling complete, closing streams", __FILE__, __func__);
	client_close();
	trace(TRACE_INFO, "%s,%s: connection closed", __FILE__, __func__);
	
	disconnect_all();
	
	return 0;
}


