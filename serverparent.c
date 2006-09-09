/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (C) 2004-2006 NFG Net Facilities Group BV support@nfg.nl
 Copyright (C) 2006 Aaron Stone aaron@serendipity.cx

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

/* $Id: serverparent.c 2199 2006-07-18 11:07:53Z paul $
 *
 * serverparent.c
 * 
 * Main program for all daemons.
 */

#include "dbmail.h"
#define THIS_MODULE "serverparent"

/* set up database login data */
extern db_param_t _db_params;

static char *configFile = DEFAULT_CONFIG_FILE;

extern volatile sig_atomic_t mainRestart;
extern volatile sig_atomic_t mainStatus;
extern volatile sig_atomic_t mainStop;
extern volatile sig_atomic_t mainSig;

static int SetMainSigHandler(void);
static void MainSigHandler(int sig, siginfo_t * info, void *data);
static void ClearConfig(serverConfig_t * conf);
static void DoConfig(serverConfig_t * conf, const char * const service);
static void LoadServerConfig(serverConfig_t * config, const char * const service);

void serverparent_showhelp(const char *name, const char *greeting) {
	printf("*** %s ***\n", name);

	printf("%s\n", greeting);
	printf("See the man page for more info.\n");

        printf("\nCommon options for all DBMail daemons:\n");
	printf("     -f file   specify an alternative config file\n");
	printf("     -p file   specify an alternative runtime pidfile\n");
	printf("     -s file   specify an alternative runtime statefile\n");
	printf("     -n        do not daemonize (no children are forked)\n");
	printf("     -v        verbose logging to syslog and stderr\n");
	printf("     -V        show the version\n");
	printf("     -h        show this help message\n");
}

/* Return values:
 * -1 just quit
 * 0 all is well, config is updated
 * 1 help must be shown, then quit
 */ 
int serverparent_getopt(serverConfig_t *config, const char *service, int argc, char *argv[])
{
	int opt;
<<<<<<< .mine
	char *configFile = g_strdup(DEFAULT_CONFIG_FILE);
=======
>>>>>>> .r2259

	ClearConfig(config);

	TRACE(TRACE_DEBUG, "checking command line options");

	/* get command-line options */
	opterr = 0;		/* suppress error message from getopt() */
	while ((opt = getopt(argc, argv, "vVhqnf:p:s:")) != -1) {
		switch (opt) {
		case 'v':
			config->log_verbose = 1;
			break;
		case 'V':
			printf("This is DBMail version %s\n\n%s\n", VERSION, COPYRIGHT);
			return -1;
		case 'n':
			config->no_daemonize = 1;
			break;
		case 'h':
			return 1;
		case 'p':
			if (optarg && strlen(optarg) > 0)
				config->pidFile = g_strdup(optarg);
			else {
				fprintf(stderr, "%s: -p requires a filename argument\n\n", argv[0]);
				return 1;
			}
			break;
		case 's':
			if (optarg && strlen(optarg) > 0)
				config->stateFile = g_strdup(optarg);
			else {
				fprintf(stderr, "%s: -s requires a filename argument\n\n", argv[0]);
				return 1;
			}
			break;
		case 'f':
			if (optarg && strlen(optarg) > 0)
				configFile = g_strdup(optarg);
			else {
				fprintf(stderr, "%s: -f requires a filename argument\n\n", argv[0]);
				return 1;
			}
			break;

		default:
			fprintf(stderr, "%s: unrecognized option [-%c]\n\n", argv[0], opt);
			break;
		}
	}

	DoConfig(config, service);

	return 0;
}

int serverparent_mainloop(serverConfig_t *config, const char *service, const char *servicename)
{
	SetMainSigHandler();

	if (config->no_daemonize) {
		StartCliServer(config);
		TRACE(TRACE_INFO, "exiting cli server");
		return 0;
	}
	
	server_daemonize(config);

	/* We write the pidFile after daemonize because
	 * we may actually be a child of the original process. */
	if (! config->pidFile)
		config->pidFile = config_get_pidfile(config, servicename);
	pidfile_create(config->pidFile, getpid());

	if (! config->stateFile)
		config->stateFile = config_get_statefile(config, servicename);
	statefile_create(config->stateFile);

	 /* Reread the config file. */
	while (!mainStop && server_run(config)) {
		DoConfig(config, service);
	}

	TRACE(TRACE_INFO, "leaving main loop");
	return 0;
}

void MainSigHandler(int sig, siginfo_t * info UNUSED, void *data UNUSED)
{
	mainSig = sig;

	if (sig == SIGHUP)
		mainRestart = 1;
	else if (sig == SIGUSR1)
		mainStatus = 1;
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
	sigaction(SIGUSR1, &act, 0);

	return 0;
}

void ClearConfig(serverConfig_t * config)
{
	memset(config, 0, sizeof(serverConfig_t));
}

void DoConfig(serverConfig_t * config, const char * const service) {
	TRACE(TRACE_DEBUG, "reading config");
	config_free();
	config_read(configFile);

	SetTraceLevel(service);
	/* Override SetTraceLevel. */
	if (config->log_verbose) {
		configure_debug(5,5);
	}
	
	LoadServerConfig(config, service);
	GetDBParams(&_db_params);
}

void LoadServerConfig(serverConfig_t * config, const char * const service)
{
	field_t val;

	config_get_logfiles(config);

	/* read items: NCHILDREN */
	config_get_value("NCHILDREN", service, val);
	if (strlen(val) == 0)
		TRACE(TRACE_FATAL, "no value for NCHILDREN in config file");

	if ((config->startChildren = atoi(val)) <= 0)
		TRACE(TRACE_FATAL, "value for NCHILDREN is invalid: [%d]",
		      config->startChildren);

	TRACE(TRACE_DEBUG, "server will create  [%d] children",
	      config->startChildren);


	/* read items: MAXCONNECTS */
	config_get_value("MAXCONNECTS", service, val);
	if (strlen(val) == 0)
		TRACE(TRACE_FATAL, "no value for MAXCONNECTS in config file");

	if ((config->childMaxConnect = atoi(val)) <= 0)
		TRACE(TRACE_FATAL, "value for MAXCONNECTS is invalid: [%d]",
		      config->childMaxConnect);

	TRACE(TRACE_DEBUG, "children will make max. [%d] connections",
	      config->childMaxConnect);


	/* read items: TIMEOUT */
	config_get_value("TIMEOUT", service, val);
	if (strlen(val) == 0) {
		TRACE(TRACE_DEBUG, "no value for TIMEOUT in config file");
		config->timeout = 0;
	} else if ((config->timeout = atoi(val)) <= 30)
		TRACE(TRACE_FATAL, "value for TIMEOUT is invalid: [%d]",
		      config->timeout);

	TRACE(TRACE_DEBUG, "timeout [%d] seconds",
	      config->timeout);

	/* SOCKET */
	config_get_value("SOCKET", service, val);
	if (strlen(val) == 0)
		TRACE(TRACE_DEBUG, "no value for SOCKET in config file");
	strncpy(config->socket, val, FIELDSIZE);
	TRACE(TRACE_DEBUG, "socket [%s]", 
		config->socket);
	
	/* read items: PORT */
	config_get_value("PORT", service, val);
	if (strlen(val) == 0)
		TRACE(TRACE_FATAL, "no value for PORT in config file");

	if ((config->port = atoi(val)) <= 0)
		TRACE(TRACE_FATAL, "value for PORT is invalid: [%d]",
		      config->port);

	TRACE(TRACE_DEBUG, "binding to PORT [%d]",
	      config->port);


	/* read items: BINDIP */
	config_get_value("BINDIP", service, val);
	if (strlen(val) == 0)
		TRACE(TRACE_FATAL, "no value for BINDIP in config file");

	strncpy(config->ip, val, IPLEN);
	config->ip[IPLEN - 1] = '\0';

	TRACE(TRACE_DEBUG, "binding to IP [%s]",
			config->ip);

	/* read items: BACKLOG */
	config_get_value("BACKLOG", service, val);
	if (strlen(val) == 0) {
		TRACE(TRACE_DEBUG, "no value for BACKLOG in config file. Using default value [%d]",
			BACKLOG);
		config->backlog = BACKLOG;
	} else if ((config->backlog = atoi(val)) <= 0)
		TRACE(TRACE_FATAL, "value for BACKLOG is invalid: [%d]",
			config->backlog);

	/* read items: RESOLVE_IP */
	config_get_value("RESOLVE_IP", service, val);
	if (strlen(val) == 0)
		TRACE(TRACE_DEBUG, "no value for RESOLVE_IP in config file");

	config->resolveIP = (strcasecmp(val, "yes") == 0);

	TRACE(TRACE_DEBUG, "%sresolving client IP",
	      config->resolveIP ? "" : "not ");

	/* read items: service-BEFORE-SMTP */
	char *service_before_smtp = g_strconcat(service, "_BEFORE_SMTP", NULL);
	config_get_value(service_before_smtp, service, val);
	g_free(service_before_smtp);

	if (strlen(val) == 0)
		TRACE(TRACE_DEBUG, "no value for %s_BEFORE_SMTP  in config file",
		      service);

	config->service_before_smtp = (strcasecmp(val, "yes") == 0);

	TRACE(TRACE_DEBUG, "%s %s-before-SMTP",
	      config->service_before_smtp ? "Enabling" : "Disabling", service);


	/* read items: EFFECTIVE-USER */
	config_get_value("EFFECTIVE_USER", service, val);
	if (strlen(val) == 0)
		TRACE(TRACE_FATAL, "no value for EFFECTIVE_USER in config file");

	strncpy(config->serverUser, val, FIELDSIZE);
	config->serverUser[FIELDSIZE - 1] = '\0';

	TRACE(TRACE_DEBUG, "effective user shall be [%s]",
	      config->serverUser);


	/* read items: EFFECTIVE-GROUP */
	config_get_value("EFFECTIVE_GROUP", service, val);
	if (strlen(val) == 0)
		TRACE(TRACE_FATAL, "no value for EFFECTIVE_GROUP in config file");

	strncpy(config->serverGroup, val, FIELDSIZE);
	config->serverGroup[FIELDSIZE - 1] = '\0';

	TRACE(TRACE_DEBUG, "effective group shall be [%s]",
	      config->serverGroup);


       /* read items: MINSPARECHILDREN */
       config_get_value("MINSPARECHILDREN", service, val);
       if (strlen(val) == 0)
               TRACE(TRACE_FATAL, "no value for MINSPARECHILDREN in config file");
       if ( (config->minSpareChildren = atoi(val)) <= 0)
               TRACE(TRACE_FATAL, "value for MINSPARECHILDREN is invalid: [%d]",
                       config->minSpareChildren);

       TRACE(TRACE_DEBUG, "will maintain minimum of [%d] spare children in reserve",
               config->minSpareChildren);


       /* read items: MAXSPARECHILDREN */
       config_get_value("MAXSPARECHILDREN", service, val);
       if (strlen(val) == 0)
               TRACE(TRACE_FATAL, "no value for MAXSPARECHILDREN in config file");
       if ( (config->maxSpareChildren = atoi(val)) <= 0)
               TRACE(TRACE_FATAL, "value for MAXSPARECHILDREN is invalid: [%d]",
                       config->maxSpareChildren);

       TRACE(TRACE_DEBUG, "will maintain maximum of [%d] spare children in reserve",
               config->maxSpareChildren);


       /* read items: MAXCHILDREN */
       config_get_value("MAXCHILDREN", service, val);
       if (strlen(val) == 0)
               TRACE(TRACE_FATAL, "no value for MAXCHILDREN in config file");
       if ( (config->maxChildren = atoi(val)) <= 0)
               TRACE(TRACE_FATAL, "value for MAXCHILDREN is invalid: [%d]",
                       config->maxSpareChildren);

       TRACE(TRACE_DEBUG, "will allow maximum of [%d] children",
               config->maxChildren);

}

