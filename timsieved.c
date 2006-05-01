/* $Id: timsieved.c 2096 2006-04-30 18:39:56Z aaron $
 
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

#include "dbmail.h"

#define PNAME "dbmail/timsieved"

/* server timeout error */
#define TIMS_TIMEOUT_MSG "221 Connection timeout BYE"

char *configFile = DEFAULT_CONFIG_FILE;

/* this is write-once read-many, so we'll do it once for all children. */
const char *sieve_extensions = NULL;

/* set up database login data */
extern db_param_t _db_params;

static int SetMainSigHandler(void);
static void MainSigHandler(int sig, siginfo_t * info, void *data);

static void get_config(serverConfig_t *config);

extern int mainRestart;
extern int mainStop;

int do_showhelp(void) {
	printf("*** dbmail-timsieved ***\n");

	printf("This daemon provides Tim's Sieve Daemon services.\n");
	printf("See the man page for more info.\n");

        printf("\nCommon options for all DBMail daemons:\n");
	printf("     -f file   specify an alternative config file\n");
	printf("     -p file   specify an alternative runtime pidfile\n");
	printf("     -n        do not daemonize (no children are forked)\n");
	printf("     -v        verbose logging to syslog and stderr\n");
	printf("     -V        show the version\n");
	printf("     -h        show this help message\n");

	return 0;
}

int main(int argc, char *argv[])
{
	serverConfig_t config;
	int result, no_daemonize = 0, log_verbose = 0;
	int opt;
	char *pidFile = NULL;

	openlog(PNAME, LOG_PID, LOG_MAIL);

	/* get command-line options */
	opterr = 0;		/* suppress error message from getopt() */
	while ((opt = getopt(argc, argv, "vVhqnf:p:")) != -1) {
		switch (opt) {
		case 'v':
			log_verbose = 1;
			break;
		case 'V':
			printf("\n*** DBMAIL: dbmail-timsieved version "
			       "$Revision: 2096 $ %s\n\n", COPYRIGHT);
			return 0;
		case 'n':
			/* TODO: We should also prevent children from forking,
			 * but for now we'll just set a flag and skip daemonize. */
			no_daemonize = 1;
			break;
		case 'h':
			do_showhelp();
			return 0;
		case 'p':
			if (optarg && strlen(optarg) > 0)
				pidFile = g_strdup(optarg);
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

	/* Get the Sieve capabilities. This may also cause the
	 * program to bomb out if Sieve support was not compiled in. */
	sieve_extensions = sort_listextensions();
	if (sieve_extensions == NULL) {
		fprintf(stderr, "dbmail-timsieved: error loading Sieve extensions.\n\n");
		return 1;
	}

	SetMainSigHandler();

	get_config(&config);
	
	/* Override SetTraceLevel. */
	if (log_verbose) {
		configure_debug(5,5);
	}
	
	if (no_daemonize) {
		StartCliServer(&config);
		config_free();
		g_mime_shutdown();
		return 0;
	}

	server_daemonize(&config);
	
	/* We write the pidFile after daemonize because
	 * we may actually be a child of the original process. */
	if (! pidFile)
		pidFile = config_get_pidfile(&config, "dbmail-timsieved");
	pidfile_create(pidFile, getpid());
	
	do {
		result = server_run(&config);
	
	} while (result == 1 && !mainStop);	/* 1 means reread-config and restart */
	config_free();

	trace(TRACE_INFO, "%s,%s: exit", __FILE__, __func__);
	return 0;
}


void get_config(serverConfig_t *config)
{
	trace(TRACE_DEBUG, "%s,%s: reading config", __FILE__, __func__);
	config_read(configFile);
	ClearConfig(config);
	SetTraceLevel("SIEVE");
	LoadServerConfig(config, "SIEVE");
	GetDBParams(&_db_params);

	config->ClientHandler = tims_handle_connection;
	config->timeoutMsg = TIMS_TIMEOUT_MSG;
}


void MainSigHandler(int sig, siginfo_t * info UNUSED, void *data UNUSED)
{
	trace(TRACE_DEBUG, "MainSigHandler(): got signal [%d]", sig);

	if (sig == SIGHUP)
		mainRestart = 1;
	else
		mainStop = 1;
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


