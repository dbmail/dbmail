/*
  $Id: server.c 2097 2006-05-01 01:12:02Z aaron $
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


int GeneralStopRequested = 0;
int Restart = 0;
int mainStop = 0;
int mainRestart = 0;

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

	if (db_connect() != DM_SUCCESS) 
		trace(TRACE_FATAL, "%s,%s: unable to connect to sql storage", __FILE__, __func__);
	
 	manage_start_children();
 	manage_spare_children();
 	
  
 	trace(TRACE_DEBUG, "%s,%s: starting main service loop", __FILE__, __func__);
 	while (!GeneralStopRequested) {
		if (db_check_connection() != 0) {
			
			if (! stopped) 
				manage_stop_children();
		
			stopped=1;
			sleep(10);
			
		} else {
			if (stopped) {
				manage_restart_children();
				stopped=0;
			}
			
			manage_spare_children();
			sleep(1);
		}
	}
   
 	manage_stop_children();
 	scoreboard_delete();


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
	
	if (! (freopen(conf->log,"a",stdout))) {
		serr = errno;
		trace(TRACE_FATAL,"%s,%s: freopen failed on [%s] [%s]", 
				__FILE__, __func__, conf->log, strerror(serr));
	}
	if (! (freopen(conf->error_log,"a",stderr))) {
		serr = errno;
		trace(TRACE_FATAL,"%s,%s: freopen failed on [%s] [%s]", 
				__FILE__, __func__, conf->error_log, strerror(serr));
	}
	if (! (freopen("/dev/null","r",stdin))) {
		serr = errno;
		trace(TRACE_FATAL,"%s,%s: freopen failed on stdin [%s]", 
				__FILE__, __func__, strerror(serr));
	}
	
	trace(TRACE_DEBUG,"%s,%s: sid: [%d]", __FILE__, 
			__func__, getsid(0));

	return getsid(0);
}

int server_run(serverConfig_t *conf)
{
	mainStop = 0;
	mainRestart = 0;
	int serrno, status, result = 0;
	pid_t pid = -1;

	CreateSocket(conf);

	switch ((pid = fork())) {
	case -1:
		serrno = errno;
		close(conf->listenSocket);
		trace(TRACE_FATAL, "%s,%s: fork failed [%s]",
				__FILE__, __func__,
				strerror(serrno));
		errno = serrno;
		break;

	case 0:
		/* child process */
		drop_privileges(conf->serverUser, conf->serverGroup);
		result = StartServer(conf);
		trace(TRACE_INFO, "%s,%s: server done, restart = [%d]",
				__FILE__, __func__, result);
		exit(result);		
		break;
	default:
		/* parent process, wait for child to exit */
		while (waitpid(pid, &status, WNOHANG | WUNTRACED) == 0) {
			if (mainStop)
				kill(pid, SIGTERM);

			if (mainRestart)
				kill(pid, SIGHUP);

			sleep(2);
		}

		if (WIFEXITED(status)) {
			/* child process terminated neatly */
			result = WEXITSTATUS(status);
			trace(TRACE_DEBUG, "%s,%s: server has exited, exit status [%d]",
			      __FILE__, __func__, result);
		} else {
			/* child stopped or signaled so make sure it is dead */
			trace(TRACE_DEBUG, "%s,%s: server has not exited normally. Killing..",
			      __FILE__, __func__);

			kill(pid, SIGKILL);
			result = 0;
		}

		if (strlen(conf->socket) > 0) {
			if (unlink(conf->socket)) {
				serrno = errno;
				trace(TRACE_ERROR, "%s,%s: unlinking unix socket failed [%s]",
						__FILE__, __func__, strerror(serrno));
				errno = serrno;
			}
		}
 

		break;
	}
	
	close(conf->listenSocket);
	
	return result;
}

void ParentSigHandler(int sig, siginfo_t * info, void *data)
{
	pid_t chpid;
	int saved_errno = errno;
	Restart = 0;
	
	/* this call is for a child but it's handler is not yet installed */
	if (ParentPID != getpid())
		active_child_sig_handler(sig, info, data); 

	switch (sig) {
 
	case SIGCHLD:
		/* ignore, wait for child in main loop */
		/* but we need to catch zombie */
		while((chpid = waitpid(-1,&sig,WNOHANG)) > 0) 
			scoreboard_release(chpid);
		break;		

	case SIGSEGV:
		sleep(60);
		exit(1);
		break;

	case SIGHUP:
		Restart = 1;
		GeneralStopRequested = 1;
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
		trace(TRACE_FATAL, "%s,%s: %s", __FILE__, __func__, strerror(err));
	}
	trace(TRACE_DEBUG, "%s,%s: done", __FILE__, __func__);
	return sock;
}

static int dm_bind_and_listen(int sock, struct sockaddr *saddr, socklen_t len, int backlog)
{
	int err;
	/* bind the address */
	if ((bind(sock, saddr, len)) == -1) {
		err = errno;
		trace(TRACE_DEBUG, "%s,%s: failed", __FILE__, __func__);
		return err;
	}

	if ((listen(sock, backlog)) == -1) {
		err = errno;
		trace(TRACE_DEBUG, "%s,%s: failed", __FILE__, __func__);
		return err;
	}
	
	trace(TRACE_DEBUG, "%s,%s: done", __FILE__, __func__);
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

	trace(TRACE_DEBUG, "%s,%s: creating socket on [%s] with backlog [%d]",
			__FILE__, __func__, conf->socket, conf->backlog);

	err = dm_bind_and_listen(sock, (struct sockaddr *)&saServer, sizeof(saServer), conf->backlog);
	if (err != 0) {
		close(sock);
		trace(TRACE_FATAL, "%s,%s: Fatal error, could not bind to [%s] %s",
			__FILE__, __func__, conf->socket, strerror(err));
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

	trace(TRACE_DEBUG, "%s,%s: creating socket on [%s:%d] with backlog [%d]",
			__FILE__, __func__, ip, port, backlog);
	
	if (ip[0] == '*') {
		
		saServer.sin_addr.s_addr = htonl(INADDR_ANY);
		
	} else if (! (inet_aton(ip, &saServer.sin_addr))) {
		
		close(sock);
		trace(TRACE_FATAL, "%s,%s: IP invalid [%s]",
				__FILE__, __func__, ip);
	}

	err = dm_bind_and_listen(sock, (struct sockaddr *)&saServer, sizeof(saServer), backlog);
	if (err != 0) {
		close(sock);
		trace(TRACE_FATAL, "%s,%s: Fatal error, could not bind to [%s:%d] %s",
			__FILE__, __func__, ip, port, strerror(err));
	}

	return sock;	
}

void CreateSocket(serverConfig_t * conf)
{
	if (strlen(conf->socket) > 0) 
		conf->listenSocket = create_unix_socket(conf);
	else
		conf->listenSocket = create_inet_socket(conf->ip, conf->port, conf->backlog);
}

void ClearConfig(serverConfig_t * conf)
{
	memset(conf, 0, sizeof(serverConfig_t));
}

void LoadServerConfig(serverConfig_t * config, const char * const service)
{
	field_t val;

	config_get_logfiles(config);

	/* read items: NCHILDREN */
	config_get_value("NCHILDREN", service, val);
	if (strlen(val) == 0)
		trace(TRACE_FATAL,
		      "%s,%s: no value for NCHILDREN in config file",
		      __FILE__, __func__);

	if ((config->startChildren = atoi(val)) <= 0)
		trace(TRACE_FATAL,
		      "%s,%s: value for NCHILDREN is invalid: [%d]",
		      __FILE__, __func__, config->startChildren);

	trace(TRACE_DEBUG,
	      "%s,%s: server will create  [%d] children",
	      __FILE__, __func__, config->startChildren);


	/* read items: MAXCONNECTS */
	config_get_value("MAXCONNECTS", service, val);
	if (strlen(val) == 0)
		trace(TRACE_FATAL,
		      "%s,%s: no value for MAXCONNECTS in config file",
		      __FILE__, __func__);

	if ((config->childMaxConnect = atoi(val)) <= 0)
		trace(TRACE_FATAL,
		      "%s,%s: value for MAXCONNECTS is invalid: [%d]",
		      __FILE__, __func__, config->childMaxConnect);

	trace(TRACE_DEBUG,
	      "%s,%s: children will make max. [%d] connections",
	      __FILE__, __func__, config->childMaxConnect);


	/* read items: TIMEOUT */
	config_get_value("TIMEOUT", service, val);
	if (strlen(val) == 0) {
		trace(TRACE_DEBUG,
		      "%s,%s: no value for TIMEOUT in config file",
		      __FILE__, __func__);
		config->timeout = 0;
	} else if ((config->timeout = atoi(val)) <= 30)
		trace(TRACE_FATAL,
		      "%s,%s: value for TIMEOUT is invalid: [%d]",
		      __FILE__, __func__, config->timeout);

	trace(TRACE_DEBUG, "%s,%s: timeout [%d] seconds",
	      __FILE__, __func__, config->timeout);

	/* SOCKET */
	config_get_value("SOCKET", service, val);
	if (strlen(val) == 0)
		trace(TRACE_DEBUG,"%s,%s: no value for SOCKET in config file",
				__FILE__, __func__);
	strncpy(config->socket, val, FIELDSIZE);
	trace(TRACE_DEBUG, "%s,%s: socket %s", 
			__FILE__, __func__, config->socket);
	
	/* read items: PORT */
	config_get_value("PORT", service, val);
	if (strlen(val) == 0)
		trace(TRACE_FATAL,
		      "%s,%s: no value for PORT in config file",
		      __FILE__, __func__);

	if ((config->port = atoi(val)) <= 0)
		trace(TRACE_FATAL,
		      "%s,%s: value for PORT is invalid: [%d]",
		      __FILE__, __func__, config->port);

	trace(TRACE_DEBUG, "%s,%s: binding to PORT [%d]",
	      __FILE__, __func__, config->port);


	/* read items: BINDIP */
	config_get_value("BINDIP", service, val);
	if (strlen(val) == 0)
		trace(TRACE_FATAL,
			"%s,%s: no value for BINDIP in config file",
			__FILE__, __func__);

	strncpy(config->ip, val, IPLEN);
	config->ip[IPLEN - 1] = '\0';

	trace(TRACE_DEBUG, "%s,%s: binding to IP [%s]",
			__FILE__, __func__, config->ip);

	/* read items: BACKLOG */
	config_get_value("BACKLOG", service, val);
	if (strlen(val) == 0) {
		trace(TRACE_DEBUG,
			"%s,%s: no value for BACKLOG in config file. Using default value [%d]",
			__FILE__, __func__, BACKLOG);
		config->backlog = BACKLOG;
	} else if ((config->backlog = atoi(val)) <= 0)
		trace(TRACE_FATAL,
			"%s,%s: value for BACKLOG is invalid: [%d]",
			__FILE__, __func__, config->backlog);

	/* read items: RESOLVE_IP */
	config_get_value("RESOLVE_IP", service, val);
	if (strlen(val) == 0)
		trace(TRACE_DEBUG,
		      "%s,%s: no value for RESOLVE_IP in config file",
		      __FILE__, __func__);

	config->resolveIP = (strcasecmp(val, "yes") == 0);

	trace(TRACE_DEBUG, "%s,%s: %sresolving client IP",
	      __FILE__, __func__, config->resolveIP ? "" : "not ");

	/* read items: service-BEFORE-SMTP */
	char *service_before_smtp = g_strconcat(service, "_BEFORE_SMTP", NULL);
	config_get_value(service_before_smtp, service, val);
	g_free(service_before_smtp);

	if (strlen(val) == 0)
		trace(TRACE_DEBUG,
		      "%s,%s: no value for %s_BEFORE_SMTP  in config file",
		      __FILE__, __func__, service);

	config->service_before_smtp = (strcasecmp(val, "yes") == 0);

	trace(TRACE_DEBUG, "%s,%s: %s %s-before-SMTP",
	      __FILE__, __func__,
	      config->service_before_smtp ? "Enabling" : "Disabling", service);


	/* read items: EFFECTIVE-USER */
	config_get_value("EFFECTIVE_USER", service, val);
	if (strlen(val) == 0)
		trace(TRACE_FATAL,
		      "%s,%s: no value for EFFECTIVE_USER in config file",
		      __FILE__, __func__);

	strncpy(config->serverUser, val, FIELDSIZE);
	config->serverUser[FIELDSIZE - 1] = '\0';

	trace(TRACE_DEBUG,
	      "%s,%s: effective user shall be [%s]",
	      __FILE__, __func__, config->serverUser);


	/* read items: EFFECTIVE-GROUP */
	config_get_value("EFFECTIVE_GROUP", service, val);
	if (strlen(val) == 0)
		trace(TRACE_FATAL,
		      "%s,%s: no value for EFFECTIVE_GROUP in config file",
		      __FILE__, __func__);

	strncpy(config->serverGroup, val, FIELDSIZE);
	config->serverGroup[FIELDSIZE - 1] = '\0';

	trace(TRACE_DEBUG,
	      "%s,%s: effective group shall be [%s]",
	      __FILE__, __func__, config->serverGroup);


       /* read items: MINSPARECHILDREN */
       config_get_value("MINSPARECHILDREN", service, val);
       if (strlen(val) == 0)
               trace(TRACE_FATAL,
                       "%s,%s: no value for MINSPARECHILDREN in config file",
		        __FILE__, __func__);
       if ( (config->minSpareChildren = atoi(val)) <= 0)
               trace(TRACE_FATAL,
                       "%s,%s: value for MINSPARECHILDREN is invalid: [%d]",
                       __FILE__, __func__, config->minSpareChildren);

       trace(TRACE_DEBUG,
               "%s,%s: will maintain minimum of [%d] spare children in reserve",
               __FILE__, __func__, config->minSpareChildren);


       /* read items: MAXSPARECHILDREN */
       config_get_value("MAXSPARECHILDREN", service, val);
       if (strlen(val) == 0)
               trace(TRACE_FATAL,
                       "%s,%s: no value for MAXSPARECHILDREN in config file",
		       __FILE__, __func__);
       if ( (config->maxSpareChildren = atoi(val)) <= 0)
               trace(TRACE_FATAL,
                       "%s,%s: value for MAXSPARECHILDREN is invalid: [%d]",
                       __FILE__, __func__, config->maxSpareChildren);

       trace(TRACE_DEBUG,
               "%s,%s: will maintain maximum of [%d] spare children in reserve",
               __FILE__, __func__, config->maxSpareChildren);


       /* read items: MAXCHILDREN */
       config_get_value("MAXCHILDREN", service, val);
       if (strlen(val) == 0)
               trace(TRACE_FATAL,
                       "%s,%s: no value for MAXCHILDREN in config file",
		       __FILE__, __func__);
       if ( (config->maxChildren = atoi(val)) <= 0)
               trace(TRACE_FATAL,
                       "%s,%s: value for MAXCHILDREN is invalid: [%d]",
                       __FILE__, __func__, config->maxSpareChildren);

       trace(TRACE_DEBUG,
               "%s,%s: will allow maximum of [%d] children",
               __FILE__, __func__, config->maxChildren);


}
