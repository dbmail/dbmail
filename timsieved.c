/* $Id$
 
Copyright (C) 2004 Aaron Stone aaron at serendipity dot cx

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
* timsieved.c
* 
* main prg for tims daemon
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
#include "pidfile.h"
#include "dbmail.h"
#include "dbmailtypes.h"
#include "timsieve.h"
#include "sort/sortsieve.h"


#define PNAME "dbmail/timsieved"

/* server timeout error */
#define TIMS_TIMEOUT_MSG "221 Connection timeout BYE"

char *pidFile = DEFAULT_PID_DIR "dbmail-timsieved" DEFAULT_PID_EXT;
char *configFile = DEFAULT_CONFIG_FILE;

/* this is write-once read-many, so we'll do it once for all children. */
char *sieve_extensions = NULL;

/* set up database login data */
extern db_param_t _db_params;

static void SetConfigItems(serverConfig_t * config);
static int SetMainSigHandler(void);
static void Daemonize(void);
static void MainSigHandler(int sig, siginfo_t * info, void *data);

static int tims_before_smtp = 0;
static int mainRestart = 0;
static int mainStop = 0;

int do_showhelp(void) {
	printf("*** dbmail-timsieved ***\n");

	printf("This daemon provides Tim's Sieve Daemon services.\n");
	printf("See the man page for more info.\n");

        printf("\nCommon options for all DBMail daemons:\n");
	printf("     -f file   specify an alternative config file\n");
	printf("     -p file   specify an alternative runtime pidfile\n");
	printf("     -n        do not daemonize (no children are forked)\n");
	printf("     -v        log to the console (only useful with -n)\n");
	printf("     -V        show the version\n");
	printf("     -h        show this help message\n");

	return 0;
}

int main(int argc, char *argv[])
{
	serverConfig_t config;
	int result, status, no_daemonize = 0;
	pid_t pid;
	int opt;

	openlog(PNAME, LOG_PID, LOG_MAIL);

	/* get command-line options */
	opterr = 0;		/* suppress error message from getopt() */
	while ((opt = getopt(argc, argv, "vVhqnf:p:")) != -1) {
		switch (opt) {
		case 'v':
			/* TODO: Perhaps verbose should log to the console with -n? */
			break;
		case 'V':
			printf("\n*** DBMAIL: dbmail-timsieved version "
			       "$Revision$ %s\n\n", COPYRIGHT);
			return 0;
		case 'n':
			/* TODO: We should also prevent children from forking,
			 * but for now we'll just set a flag and skip Daemonize. */
			no_daemonize = 1;
			break;
		case 'h':
			do_showhelp();
			return 0;
		case 'p':
			if (optarg && strlen(optarg) > 0)
				pidFile = optarg;
			else {
				fprintf(stderr,
					"dbmail-timsieved: -p requires a filename "
					"argument\n\n");
				return 1;
			}
			break;
		case 'f':
			if (optarg && strlen(optarg) > 0)
				configFile = optarg;
			else {
				fprintf(stderr,
					"dbmail-timsieved: -f requires a filename "
					"argument\n\n");
				return 1;
			}
			break;

		default:
			break;
		}
	}

	/* Anonymous block */ {
		int res = 0;
		sieve2_support_t *p;

		res = sieve2_support_alloc(&p);
		if (res != SIEVE2_OK) {
			/* FIXME: Complain. */
		}

		/* Macro to register all of the currently-supported extensions. */
		SIEVE2_SUPPORT_REGISTER(res);
		if (res != SIEVE2_OK) {
			/* FIXME: Complain. */
		}

		/* load up on the extensions we support. */
		res = sieve2_extensions_listsupport(p, &sieve_extensions);
		if (res != SIEVE2_OK) {
			/* FIXME: Complain. */
		}

		res = sieve2_support_free(p);
		if (res != SIEVE2_OK) {
			/* FIXME: Complain. */
		}
	} /* End of anonymous block. */

	SetMainSigHandler();

	/* TODO: don't spawn children, either. this is at least a good start. */
	if (!no_daemonize)
		Daemonize();

	/* We write the pidFile after Daemonize because
	 * we may actually be a child of the original process. */
	pidfile_create(pidFile, getpid());

	result = 0;
	do {
		mainStop = 0;
		mainRestart = 0;

		trace(TRACE_DEBUG, "main(): reading config");

		/* We need smtp config for bounce.c and forward.c */
                config_read(configFile);
		SetConfigItems(&config);
		SetTraceLevel("TIMSIEVED");
		GetDBParams(&_db_params);

		config.ClientHandler = tims_handle_connection;
		config.timeoutMsg = TIMS_TIMEOUT_MSG;

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
	config_get_value("NCHILDREN", "TIMSIEVED", val);
	if (strlen(val) == 0)
		trace(TRACE_FATAL,
		      "SetConfigItems(): no value for NCHILDREN in config file");

	if ((config->startChildren = atoi(val)) <= 0)
		trace(TRACE_FATAL,
		      "SetConfigItems(): value for NCHILDREN is invalid: [%d]",
		      config->startChildren);

	trace(TRACE_DEBUG,
	      "SetConfigItems(): server will create  [%d] children",
	      config->startChildren);


	/* read items: MAXCONNECTS */
	config_get_value("MAXCONNECTS", "TIMSIEVED", val);
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
	config_get_value("TIMEOUT", "TIMSIEVED", val);
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
	config_get_value("PORT", "TIMSIEVED", val);
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
	config_get_value("BINDIP", "TIMSIEVED", val);
	if (strlen(val) == 0)
		trace(TRACE_FATAL,
		      "SetConfigItems(): no value for BINDIP in config file");

	strncpy(config->ip, val, IPLEN);
	config->ip[IPLEN - 1] = '\0';

	trace(TRACE_DEBUG, "SetConfigItems(): binding to IP [%s]",
	      config->ip);


	/* read items: RESOLVE_IP */
	config_get_value("RESOLVE_IP", "TIMSIEVED", val);
	if (strlen(val) == 0)
		trace(TRACE_DEBUG,
		      "SetConfigItems(): no value for RESOLVE_IP in config file");

	config->resolveIP = (strcasecmp(val, "yes") == 0);

	trace(TRACE_DEBUG, "SetConfigItems(): %sresolving client IP",
	      config->resolveIP ? "" : "not ");


	/* read items: IMAP-BEFORE-SMTP */
	config_get_value("TIMS_BEFORE_SMTP", "TIMSIEVED", val);
	if (strlen(val) == 0)
		trace(TRACE_DEBUG,
		      "SetConfigItems(): no value for TIMS_BEFORE_SMTP  in config file");

	tims_before_smtp = (strcasecmp(val, "yes") == 0);

	trace(TRACE_DEBUG, "SetConfigItems(): %s TIMS-before-SMTP",
	      tims_before_smtp ? "Enabling" : "Disabling");


	/* read items: EFFECTIVE-USER */
	config_get_value("EFFECTIVE_USER", "TIMSIEVED", val);
	if (strlen(val) == 0)
		trace(TRACE_FATAL,
		      "SetConfigItems(): no value for EFFECTIVE_USER in config file");

	strncpy(config->serverUser, val, FIELDSIZE);
	config->serverUser[FIELDSIZE - 1] = '\0';

	trace(TRACE_DEBUG,
	      "SetConfigItems(): effective user shall be [%s]",
	      config->serverUser);


	/* read items: EFFECTIVE-GROUP */
	config_get_value("EFFECTIVE_GROUP", "TIMSIEVED", val);
	if (strlen(val) == 0)
		trace(TRACE_FATAL,
		      "SetConfigItems(): no value for EFFECTIVE_GROUP in config file");

	strncpy(config->serverGroup, val, FIELDSIZE);
	config->serverGroup[FIELDSIZE - 1] = '\0';

	trace(TRACE_DEBUG,
	      "SetConfigItems(): effective group shall be [%s]",
	      config->serverGroup);



}
