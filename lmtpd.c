/*
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

/* $Id$
*
* lmtpd.c
*
* main prg for lmtp daemon
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include "imap4.h"
#include "server.h"
#include "debug.h"
#include "misc.h"
#include "dbmail.h"
#include "clientinfo.h"
#include "lmtp.h"
#ifdef PROC_TITLES
#include "proctitleutils.h"
#endif

#define PNAME "dbmail/lmtpd"

/* server timeout error */
#define LMTP_TIMEOUT_MSG "221 Connection timeout BYE"

char *configFile = DEFAULT_CONFIG_FILE;

/* set up database login data */
extern db_param_t _db_params;

static void SetConfigItems(serverConfig_t * config);
static void Daemonize(void);
static int SetMainSigHandler(void);
static void MainSigHandler(int sig, siginfo_t * info, void *data);

static int lmtp_before_smtp = 0;
static int mainRestart = 0;
static int mainStop = 0;

#ifdef PROC_TITLES
int main(int argc, char *argv[], char **envp)
#else
int main(int argc, char *argv[])
#endif
{
	serverConfig_t config;
	int result, status;
	pid_t pid;

	openlog(PNAME, LOG_PID, LOG_MAIL);

	if (argc >= 2 && (argv[1])) {
		if (strcmp(argv[1], "-v") == 0) {
			printf
			    ("\n*** DBMAIL: dbmail-lmtpd version $Revision$ %s\n\n",
			     COPYRIGHT);
			return 0;
		} else if (strcmp(argv[1], "-f") == 0 && (argv[2]))
			configFile = argv[2];
	}

	SetMainSigHandler();
	Daemonize();
	result = 0;

	do {
		mainStop = 0;
		mainRestart = 0;

		trace(TRACE_DEBUG, "main(): reading config");
#ifdef PROC_TITLES
		init_set_proc_title(argc, argv, envp, PNAME);
		set_proc_title("%s", "Idle");
#endif

		/* We need smtp config for bounce.c and forward.c */
		ReadConfig("SMTP", configFile);
		ReadConfig("LMTP", configFile);
		ReadConfig("DBMAIL", configFile);
		SetConfigItems(&config);
		SetTraceLevel("LMTP");
		GetDBParams(&_db_params);

		config.ClientHandler = lmtp_handle_connection;
		config.timeoutMsg = LMTP_TIMEOUT_MSG;

		CreateSocket(&config);
		trace(TRACE_DEBUG,
		      "main(): socket created, starting server");

		switch ((pid = fork())) {
		case -1:
			close(config.listenSocket);
			trace(TRACE_FATAL, "main(): fork failed [%s]",
			      strerror(errno));

		case 0:
			/* child process */
			drop_privileges(config.serverUser,
					config.serverGroup);
			result = StartServer(&config);

			trace(TRACE_INFO, "main(): server done, exit.");
			exit(result);

		default:
			/* parent process, wait for child to exit */
			while (waitpid(pid, &status, WNOHANG | WUNTRACED)
			       == 0) {
				if (mainStop)
					kill(pid, SIGTERM);

				if (mainRestart)
					kill(pid, SIGHUP);

				sleep(2);
			}

			if (WIFEXITED(status)) {
				/* child process terminated neatly */
				result = WEXITSTATUS(status);
				trace(TRACE_DEBUG,
				      "main(): server has exited, exit status [%d]",
				      result);
			} else {
				/* child stopped or signaled, don't like */
				/* make sure it is dead */
				trace(TRACE_DEBUG,
				      "main(): server has not exited normally. Killing..");

				kill(pid, SIGKILL);
				result = 0;
			}
		}
		
		close(config.listenSocket);
		config_free();

	} while (result == 1 && !mainStop);	/* 1 means reread-config and restart */

	trace(TRACE_INFO, "main(): exit");
	return 0;
}


void MainSigHandler(int sig, siginfo_t * info UNUSED, void *data UNUSED)
{
	trace(TRACE_DEBUG, "MainSigHandler(): got signal [%d]", sig);

	if (sig == SIGHUP)
		mainRestart = 1;
	else
		mainStop = 1;
}


void Daemonize()
{
	if (fork())
		exit(0);
	setsid();

	if (fork())
		exit(0);
}


int SetMainSigHandler()
{
	struct sigaction act;

	/* init & install signal handlers */
	memset(&act, 0, sizeof(act));

	act.sa_sigaction = MainSigHandler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_SIGINFO;

	sigaction(SIGINT, &act, 0);
	sigaction(SIGQUIT, &act, 0);
	sigaction(SIGTERM, &act, 0);
	sigaction(SIGHUP, &act, 0);

	return 0;
}


void SetConfigItems(serverConfig_t * config)
{
	field_t val;

	/* read items: NCHILDREN */
	GetConfigValue("NCHILDREN", "LMTP", val);
	if (strlen(val) == 0)
		trace(TRACE_FATAL,
		      "SetConfigItems(): no value for NCHILDREN in config file");

	if ((config->nChildren = atoi(val)) <= 0)
		trace(TRACE_FATAL,
		      "SetConfigItems(): value for NCHILDREN is invalid: [%d]",
		      config->nChildren);

	trace(TRACE_DEBUG,
	      "SetConfigItems(): server will create  [%d] children",
	      config->nChildren);


	/* read items: MAXCONNECTS */
	GetConfigValue("MAXCONNECTS", "LMTP", val);
	if (strlen(val) == 0)
		trace(TRACE_FATAL,
		      "SetConfigItems(): no value for MAXCONNECTS in config file");

	if ((config->childMaxConnect = atoi(val)) <= 0)
		trace(TRACE_FATAL,
		      "SetConfigItems(): value for MAXCONNECTS is invalid: [%d]",
		      config->childMaxConnect);

	trace(TRACE_DEBUG,
	      "SetConfigItems(): children will make max. [%d] connections",
	      config->childMaxConnect);


	/* read items: TIMEOUT */
	GetConfigValue("TIMEOUT", "LMTP", val);
	if (strlen(val) == 0) {
		trace(TRACE_DEBUG,
		      "SetConfigItems(): no value for TIMEOUT in config file");
		config->timeout = 0;
	} else if ((config->timeout = atoi(val)) <= 30)
		trace(TRACE_FATAL,
		      "SetConfigItems(): value for TIMEOUT is invalid: [%d]",
		      config->timeout);

	trace(TRACE_DEBUG, "SetConfigItems(): timeout [%d] seconds",
	      config->timeout);


	/* read items: PORT */
	GetConfigValue("PORT", "LMTP", val);
	if (strlen(val) == 0)
		trace(TRACE_FATAL,
		      "SetConfigItems(): no value for PORT in config file");

	if ((config->port = atoi(val)) <= 0)
		trace(TRACE_FATAL,
		      "SetConfigItems(): value for PORT is invalid: [%d]",
		      config->port);

	trace(TRACE_DEBUG, "SetConfigItems(): binding to PORT [%d]",
	      config->port);


	/* read items: BINDIP */
	GetConfigValue("BINDIP", "LMTP", val);
	if (strlen(val) == 0)
		trace(TRACE_FATAL,
		      "SetConfigItems(): no value for BINDIP in config file");

	strncpy(config->ip, val, IPLEN);
	config->ip[IPLEN - 1] = '\0';

	trace(TRACE_DEBUG, "SetConfigItems(): binding to IP [%s]",
	      config->ip);


	/* read items: RESOLVE_IP */
	GetConfigValue("RESOLVE_IP", "LMTP", val);
	if (strlen(val) == 0)
		trace(TRACE_DEBUG,
		      "SetConfigItems(): no value for RESOLVE_IP in config file");

	config->resolveIP = (strcasecmp(val, "yes") == 0);

	trace(TRACE_DEBUG, "SetConfigItems(): %sresolving client IP",
	      config->resolveIP ? "" : "not ");


	/* read items: IMAP-BEFORE-SMTP */
	GetConfigValue("LMTP_BEFORE_SMTP", "LMTP", val);
	if (strlen(val) == 0)
		trace(TRACE_DEBUG,
		      "SetConfigItems(): no value for LMTP_BEFORE_SMTP  in config file");

	lmtp_before_smtp = (strcasecmp(val, "yes") == 0);

	trace(TRACE_DEBUG, "SetConfigItems(): %s LMTP-before-SMTP",
	      lmtp_before_smtp ? "Enabling" : "Disabling");


	/* read items: EFFECTIVE-USER */
	GetConfigValue("EFFECTIVE_USER", "LMTP", val);
	if (strlen(val) == 0)
		trace(TRACE_FATAL,
		      "SetConfigItems(): no value for EFFECTIVE_USER in config file");

	strncpy(config->serverUser, val, FIELDSIZE);
	config->serverUser[FIELDSIZE - 1] = '\0';

	trace(TRACE_DEBUG,
	      "SetConfigItems(): effective user shall be [%s]",
	      config->serverUser);


	/* read items: EFFECTIVE-GROUP */
	GetConfigValue("EFFECTIVE_GROUP", "LMTP", val);
	if (strlen(val) == 0)
		trace(TRACE_FATAL,
		      "SetConfigItems(): no value for EFFECTIVE_GROUP in config file");

	strncpy(config->serverGroup, val, FIELDSIZE);
	config->serverGroup[FIELDSIZE - 1] = '\0';

	trace(TRACE_DEBUG,
	      "SetConfigItems(): effective group shall be [%s]",
	      config->serverGroup);



}
