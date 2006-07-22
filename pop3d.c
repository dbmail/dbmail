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

/* $Id: pop3d.c 2199 2006-07-18 11:07:53Z paul $
*
* pop3d.c
*
* main prg for pop3 daemon
*/

#include "dbmail.h"

#define PNAME "dbmail/pop3d"

/* server timeout error */
#define POP_TIMEOUT_MSG "-ERR I'm leaving, you're tooo slow\r\n"

char *configFile = DEFAULT_CONFIG_FILE;

/* set up database login data */
extern db_param_t _db_params;

static int SetMainSigHandler(void);
static void MainSigHandler(int sig, siginfo_t * info, void *data);

static void get_config(serverConfig_t *config);

/* also used in pop3.c */
int pop_before_smtp = 0;

extern volatile sig_atomic_t mainRestart;
extern volatile sig_atomic_t mainStop;
extern volatile sig_atomic_t mainSig;

int do_showhelp(void) {
	printf("*** dbmail-pop3d ***\n");

	printf("This daemon provides Post Office Protocol v3 services.\n");
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

	g_mime_init(0);
	openlog(PNAME, LOG_PID, LOG_MAIL);

	/* get command-line options */
	opterr = 0;		/* suppress error message from getopt() */
	while ((opt = getopt(argc, argv, "vVhqnf:p:")) != -1) {
		switch (opt) {
		case 'v':
			log_verbose = 1;
			break;
		case 'V':
			printf("\n*** DBMAIL: dbmail-pop3d version "
			       "$Revision: 2199 $ %s\n\n", COPYRIGHT);
			return 0;
		case 'n':
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
					"dbmail-pop3d: -p requires a filename "
					"argument\n\n");
				return 1;
			}
			break;
		case 'f':
			if (optarg && strlen(optarg) > 0)
				configFile = optarg;
			else {
				fprintf(stderr,
					"dbmail-pop3d: -f requires a filename "
					"argument\n\n");
				return 1;
			}
			break;

		default:
			break;
		}
	}

	SetMainSigHandler();
	
	get_config(&config);
	pop_before_smtp = config.service_before_smtp;
	
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
		pidFile = config_get_pidfile(&config, "dbmail-pop3d");
	
	pidfile_create(pidFile, getpid());
	g_free(pidFile);

	do {
		result = server_run(&config);
		
	} while (result == 1 && !mainStop);	/* 1 means reread-config and restart */
	config_free();
	
	trace(TRACE_INFO, "%s,%s: exit", __FILE__, __func__);
	g_mime_shutdown();
	return 0;
}

void get_config(serverConfig_t *config)
{
	trace(TRACE_DEBUG, "%s,%s: reading config",
			__FILE__, __func__);
	
	config_read(configFile);
	ClearConfig(config);
	SetTraceLevel("POP");
	LoadServerConfig(config, "POP");
	GetDBParams(&_db_params);

	config->ClientHandler = pop3_handle_connection;
	config->timeoutMsg = POP_TIMEOUT_MSG;
}



void MainSigHandler(int sig, siginfo_t * info UNUSED, void *data UNUSED)
{
	mainSig = sig;

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

