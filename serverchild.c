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
 * 
 * 
 * function implementations of server children code (connection handling)
 */

#include "dbmail.h"
#define THIS_MODULE "serverchild"

volatile sig_atomic_t ChildStopRequested = 0;
volatile sig_atomic_t childSig = 0;
extern volatile sig_atomic_t alarm_occured;


static int connected = 0;

static int selfPipe[2];

volatile clientinfo_t client;

static void disconnect_all(void);

static int PerformChildTask(ChildInfo_t * info);

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

void noop_child_sig_handler(int sig)
{
	if (sig == SIGSEGV)
		_exit(0);
}

void active_child_sig_handler(int sig)
{
	int saved_errno = errno;
	
	/* Perform reinit at SIGHUP otherwise exit, but do nothing on
	 * SIGCHLD. Make absolutely sure that everything downwind of
	 * this function is signal-safe! Symptoms of signal-unsafe
	 * calls are random errors like this:
	 * *** glibc detected *** corrupted double-linked list: 0x0805f028 ***
	 * Right, so keep that in mind! */

	if (selfPipe[1] > -1)
		write(selfPipe[1], "S", 1);

	switch (sig) {

	case SIGCHLD:
		break;
	case SIGALRM:
		alarm_occured = 1;
		break;
	case SIGPIPE:
		break;
	default:
	 	ChildStopRequested = 1;
		childSig = sig;
		break;
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
	struct sigaction rstact;

	memset(&act, 0, sizeof(act));
	memset(&rstact, 0, sizeof(rstact));

	act.sa_sigaction = active_child_sig_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;

	rstact.sa_sigaction = active_child_sig_handler;
	sigemptyset(&rstact.sa_mask);
	rstact.sa_flags = SA_RESETHAND;

	sigaddset(&act.sa_mask, SIGINT);
	sigaddset(&act.sa_mask, SIGQUIT);
	sigaddset(&act.sa_mask, SIGILL);
	sigaddset(&act.sa_mask, SIGBUS);
	sigaddset(&act.sa_mask, SIGFPE);
	sigaddset(&act.sa_mask, SIGSEGV);
	sigaddset(&act.sa_mask, SIGTERM);
	sigaddset(&act.sa_mask, SIGHUP);

	sigaction(SIGINT,	&rstact, 0);
	sigaction(SIGQUIT,	&rstact, 0);
	sigaction(SIGILL,	&rstact, 0);
	sigaction(SIGBUS,	&rstact, 0);
	sigaction(SIGFPE,	&rstact, 0);
	sigaction(SIGSEGV,	&rstact, 0);
	sigaction(SIGTERM,	&rstact, 0);
	sigaction(SIGHUP,	&rstact, 0);
	sigaction(SIGPIPE,	&rstact, 0);
	sigaction(SIGALRM,	&act, 0);
	sigaction(SIGCHLD,	&act, 0);

	TRACE(TRACE_INFO, "signal handler placed");

	return 0;
}
int DelChildSigHandler()
{
	struct sigaction act;

	/* init & install signal handlers */
	memset(&act, 0, sizeof(act));

	act.sa_sigaction = noop_child_sig_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;

	sigaction(SIGINT,	&act, 0);
	sigaction(SIGQUIT,	&act, 0);
	sigaction(SIGILL,	&act, 0);
	sigaction(SIGBUS,	&act, 0);
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
	extern int isGrandChildProcess;
	pid_t pid;

 	pid = fork();

	if (! pid) {
		if ( child_register() == -1 ) {
			TRACE(TRACE_FATAL, "child_register failed");
			_exit(0);
		}
	
		isGrandChildProcess = 1;
 		ChildStopRequested = 0;
		alarm_occured = 0;
		childSig = 0;

		if (pipe(selfPipe))
			return -1;

		fcntl(selfPipe[0], F_SETFL, O_NONBLOCK);
		fcntl(selfPipe[1], F_SETFL, O_NONBLOCK);

 		SetChildSigHandler();
 		
		if (PerformChildTask(info) == -1) {
			close(selfPipe[0]); selfPipe[0] = -1;
			close(selfPipe[1]); selfPipe[1] = -1;
			return -1;
		}
		
 		child_unregister();
 		exit(0);
	} else {
 		usleep(5000);
		/* check for failed forkes */
		if (waitpid(pid, NULL, WNOHANG|WUNTRACED) == pid) 
			return -1;
 		return pid;
	}
}

static int select_and_accept(ChildInfo_t * info, int * clientSocket)
{
	fd_set rfds;
	int ip, result, flags;
	int active = 0, maxfd = 0;

	/* This is adapted from man 2 select */
	FD_ZERO(&rfds);
	for (ip = 0; ip < info->numSockets; ip++) {
		FD_SET(info->listenSockets[ip], &rfds);
		maxfd = MAX(maxfd, info->listenSockets[ip]);
	}

	FD_SET(selfPipe[0], &rfds);
	maxfd = MAX(maxfd, selfPipe[0]);

	/* A null timeval means block indefinitely until there's activity. */
	result = select(maxfd+1, &rfds, NULL, NULL, NULL);
	if (result < 1)
		return -1;

	// Clear the self-pipe and return; we received a signal
	// and we need to loop again upstream to handle it.
	// See http://cr.yp.to/docs/selfpipe.html
	if (FD_ISSET(selfPipe[0], &rfds)) {
		char buf[1];
		while (read(selfPipe[0], buf, 1) > 0)
			;
		return -1;
	}

	/* This is adapted from man 2 select */
	for (ip = 0; ip < info->numSockets; ip++) {
		if (FD_ISSET(info->listenSockets[ip], &rfds)) {
			active = ip;
			break;
		}
	}

	/* accept the active fd */
	// the listenSockets are set non-blocking in server.c,create_inet_socket
	*clientSocket = accept(info->listenSockets[active], NULL, NULL);
	if (*clientSocket < 0)
		return -1;

	// the clientSocket *must* be blocking 
	flags = fcntl(*clientSocket, F_GETFL);
	if (*clientSocket > 0)
		fcntl(*clientSocket, F_SETFL, flags & ~ O_NONBLOCK);

	TRACE(TRACE_INFO, "connection accepted");
	return 0;
}

int PerformChildTask(ChildInfo_t * info)
{
	int i, clientSocket, result, err;
	socklen_t len = sizeof(struct sockaddr_storage);
	if (!info) {
		TRACE(TRACE_ERROR, "NULL info supplied");
		return -1;
	}

	if (db_connect() != 0) {
		TRACE(TRACE_ERROR, "could not connect to database");
		return -1;
	}

	if (auth_connect() != 0) {
		TRACE(TRACE_ERROR, "could not connect to authentication");
		return -1;
	}

	srand((int) ((int) time(NULL) + (int) getpid()));
	connected = 1;

	
	for (i = 0; i < info->maxConnect && !ChildStopRequested; i++) {

		if (db_check_connection()) {
			TRACE(TRACE_ERROR, "database has gone away");
			ChildStopRequested=1;
			continue;
		}

		child_reg_disconnected();

		/* wait for connect */
		result = select_and_accept(info, &clientSocket);
		if (result != 0) {
			i--;	/* don't count this as a connect */
			continue;	/* accept failed, refuse connection & continue */
		}

		memset((void *)&client, 0, sizeof(client));	/* zero-init */

		if (getsockname(clientSocket, (struct sockaddr *)&client.saddr, &len)) {
			int err = errno;
			TRACE(TRACE_FATAL, "getsockname::error [%s]", strerror(err));
			return -1;
		}
		client.saddr_len = len;

		if (getpeername(clientSocket, (struct sockaddr *)&client.caddr, &len)) {
			int err = errno;
			TRACE(TRACE_FATAL, "getpeername::error [%s]", strerror(err));
			return -1;
		}
		client.caddr_len = len;

		child_reg_connected();

		client.timeout = info->timeout;
		client.login_timeout = info->login_timeout;
		client.clientname[0] = '\0';
			

		if ((err = getnameinfo((struct sockaddr *)&client.saddr, client.saddr_len, (char *)&client.dst_ip, NI_MAXHOST, (char *)&client.dst_port, NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV))) {
			TRACE(TRACE_INFO,"getnameinfo::error [%s]", gai_strerror(err));
		}

		TRACE(TRACE_INFO, "incoming connection on [%s:%s]", (char *)&client.dst_ip, (char *)&client.dst_port);

		if ((err = getnameinfo((struct sockaddr *)&client.caddr, client.caddr_len, (char *)&client.src_ip, NI_MAXHOST, (char *)&client.src_port, NI_MAXSERV, NI_NUMERICHOST | NI_NUMERICSERV))) {
			TRACE(TRACE_INFO,"getnameinfo::error [%s]", gai_strerror(err));
		}

		if (info->resolveIP) {
			if ((err = getnameinfo((struct sockaddr *)&client.caddr, client.caddr_len, (char *)&client.clientname, sizeof(field_t), NULL, 0, NI_NAMEREQD))) {
				TRACE(TRACE_INFO, "getnameinfo:error [%s]", gai_strerror(err));
			} 

			TRACE(TRACE_INFO, "incoming connection from [%s:%s (%s)]",
					client.src_ip, client.src_port,
					client.clientname[0] ? client.clientname : "Lookup failed");
		} else {
			TRACE(TRACE_INFO, "incoming connection from [%s:%s]", (char *)&client.src_ip, (char *)&client.src_port);
		}

		child_reg_connected_client((const char *)client.src_ip, (const char *)client.clientname);
		
		/* make streams */
		if (!(client.rx = fdopen(dup(clientSocket), "r"))) {
			/* read-FILE opening failure */
			TRACE(TRACE_ERROR, "error opening read file stream");
			close(clientSocket);
			continue;
		}

		if (!(client.tx = fdopen(clientSocket, "w"))) {
			/* write-FILE opening failure */
			TRACE(TRACE_ERROR, "error opening write file stream");
			fclose(client.rx);
			close(clientSocket);
			memset((void *)&client, 0, sizeof(client));
			continue;
		}

		setvbuf(client.tx, (char *) NULL, _IOLBF, 0);
		setvbuf(client.rx, (char *) NULL, _IOLBF, 0);

		TRACE(TRACE_DEBUG, "client info init complete, calling client handler");

		/* streams are ready, perform handling */
		info->ClientHandler((clientinfo_t *)&client);

		TRACE(TRACE_DEBUG, "client handling complete, closing streams");
		client_close();
		TRACE(TRACE_INFO, "connection closed");
	}

	if (!ChildStopRequested)
		TRACE(TRACE_ERROR, "maximum number of connections reached, stopping now");
	else{
		switch(childSig){
		case SIGHUP:
		case SIGTERM:
		case SIGQUIT:
			client_close();
			disconnect_all();
			child_unregister();
			exit(1);
		default:
			child_unregister();
			_exit(1);
		}
		TRACE(TRACE_ERROR, "stop requested");
	}

	child_reg_disconnected();
	disconnect_all();
	
	return 0;
}

int manage_start_cli_server(ChildInfo_t * info)
{
	if (!info) {
		TRACE(TRACE_ERROR, "NULL info supplied");
		return -1;
	}

	if (db_connect() != 0) {
		TRACE(TRACE_ERROR, "could not connect to database");
		return -1;
	}

	if (auth_connect() != 0) {
		TRACE(TRACE_ERROR, "could not connect to authentication");
		return -1;
	}

	srand((int) ((int) time(NULL) + (int) getpid()));
	connected = 1;

	if (db_check_connection()) {
		TRACE(TRACE_ERROR, "database has gone away");
		return -1;
	}

		
	memset((void *)&client, 0, sizeof(client));	/* zero-init */

	client.timeout = info->timeout;
	client.login_timeout = info->login_timeout;

	/* make streams */
	client.rx = stdin;
	client.tx = stdout;

	setvbuf(client.tx, (char *) NULL, _IOLBF, 0);
	setvbuf(client.rx, (char *) NULL, _IOLBF, 0);

	TRACE(TRACE_DEBUG, "client info init complete, calling client handler");

	/* streams are ready, perform handling */
	info->ClientHandler((clientinfo_t *)&client);

	TRACE(TRACE_DEBUG, "client handling complete, closing streams");
	client_close();
	TRACE(TRACE_INFO, "connection closed"); 
	disconnect_all();
	
	return 0;
}

