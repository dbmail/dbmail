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

/* $Id: pop3d.c 2028 2006-03-16 08:38:06Z paul $
*
* pop3d.c
*
* main prg for pop3 daemon
*/

#include "dbmail.h"

#define PNAME "dbmail/pop3d"

/* server timeout error */
#define POP_TIMEOUT_MSG "-ERR I'm leaving, you're tooo slow\r\n"

char *pidFile = DEFAULT_PID_DIR "dbmail-pop3d" DEFAULT_PID_EXT;
char *configFile = DEFAULT_CONFIG_FILE;

/* set up database login data */
extern db_param_t _db_params;

static void SetConfigItems(serverConfig_t * config);
static int SetMainSigHandler(void);
static void MainSigHandler(int sig, siginfo_t * info, void *data);

static void get_config(serverConfig_t *config);

/* also used in pop3.c */
int pop_before_smtp = 0;

extern int mainRestart;
extern int mainStop;

int do_showhelp(void) {
	printf("*** dbmail-pop3d ***\n");

	printf("This daemon provides Post Office Protocol v3 services.\n");
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
	int result, no_daemonize = 0;
	int opt;

	g_mime_init(0);
	openlog(PNAME, LOG_PID, LOG_MAIL);

	/* get command-line options */
	opterr = 0;		/* suppress error message from getopt() */
	while ((opt = getopt(argc, argv, "vVhqnf:p:")) != -1) {
		switch (opt) {
		case 'v':
			/* TODO: Perhaps verbose should log to the console with -n? */
			break;
		case 'V':
			printf("\n*** DBMAIL: dbmail-pop3d version "
			       "$Revision: 2028 $ %s\n\n", COPYRIGHT);
			return 0;
		case 'n':
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
	
	if (no_daemonize) {
		StartCliServer(&config);
		config_free();
		g_mime_shutdown();
		return 0;
	}
	
	server_daemonize(&config);

	/* We write the pidFile after daemonize because
	 * we may actually be a child of the original process. */
	pidfile_create(pidFile, getpid());

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
	SetConfigItems(config);
	SetTraceLevel("POP");
	GetDBParams(&_db_params);

	config->ClientHandler = pop3_handle_connection;
	config->timeoutMsg = POP_TIMEOUT_MSG;
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


void SetConfigItems(serverConfig_t * config)
{
	field_t val;

	config_get_logfiles(config);

	/* read items: NCHILDREN */
	config_get_value("NCHILDREN", "POP", val);
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
	config_get_value("MAXCONNECTS", "POP", val);
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
	
	/* read items: MINSPARECHILDREN */
	config_get_value("MINSPARECHILDREN", "POP", val);
	if (strlen(val) == 0)
		trace(TRACE_FATAL, 
			"SetConfigItems(): no value for MINSPARECHILDREN in config file");
	if ( (config->minSpareChildren = atoi(val)) <= 0)
		trace(TRACE_FATAL, 
			"SetConfigItems(): value for MINSPARECHILDREN is invalid: [%d]", 
			config->minSpareChildren);
 
	trace(TRACE_DEBUG, 
		"SetConfigItems(): will maintain minimum of [%d] spare children in reserve", 
		config->minSpareChildren);
   
   
	/* read items: MAXSPARECHILDREN */
	config_get_value("MAXSPARECHILDREN", "POP", val);
	if (strlen(val) == 0)
		trace(TRACE_FATAL, 
			"SetConfigItems(): no value for MAXSPARECHILDREN in config file");
	if ( (config->maxSpareChildren = atoi(val)) <= 0)
		trace(TRACE_FATAL, 
			"SetConfigItems(): value for MAXSPARECHILDREN is invalid: [%d]", 
			config->maxSpareChildren);
 
	trace(TRACE_DEBUG, 
		"SetConfigItems(): will maintain maximum of [%d] spare children in reserve", 
		config->maxSpareChildren);
 
   
	/* read items: MAXCHILDREN */
	config_get_value("MAXCHILDREN", "POP", val);
	if (strlen(val) == 0)
	trace(TRACE_FATAL, 
		"SetConfigItems(): no value for MAXCHILDREN in config file");
	if ( (config->maxChildren = atoi(val)) <= 0)
		trace(TRACE_FATAL, 
			"SetConfigItems(): value for MAXCHILDREN is invalid: [%d]", 
			config->maxSpareChildren);
 
	trace(TRACE_DEBUG, 
		"SetConfigItems(): will allow maximum of [%d] children", 
		config->maxChildren);

	/* read items: TIMEOUT */
	config_get_value("TIMEOUT", "POP", val);
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
	config_get_value("PORT", "POP", val);
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
	config_get_value("BINDIP", "POP", val);
	if (strlen(val) == 0)
		trace(TRACE_FATAL,
		      "SetConfigItems(): no value for BINDIP in config file");

	strncpy(config->ip, val, IPLEN);
	config->ip[IPLEN - 1] = '\0';

	trace(TRACE_DEBUG, "SetConfigItems(): binding to IP [%s]",
	      config->ip);

	
	/* SOCKET */
	config_get_value("SOCKET","POP", val);
	if (strlen(val) == 0)
		trace(TRACE_DEBUG,"%s,%s: no value for SOCKET in config file",
				__FILE__, __func__);
	strncpy(config->socket, val, FIELDSIZE);
	trace(TRACE_DEBUG, "%s,%s: socket %s", 
			__FILE__, __func__,
			config->socket);


	/* read items: RESOLVE_IP */
	config_get_value("RESOLVE_IP", "POP", val);
	if (strlen(val) == 0)
		trace(TRACE_DEBUG,
		      "SetConfigItems(): no value for RESOLVE_IP in config file");

	config->resolveIP = (strcasecmp(val, "yes") == 0);

	trace(TRACE_DEBUG, "SetConfigItems(): %sresolving client IP",
	      config->resolveIP ? "" : "not ");


	/* read items: IMAP-BEFORE-SMTP */
	config_get_value("POP_BEFORE_SMTP", "POP", val);
	if (strlen(val) == 0)
		trace(TRACE_DEBUG,
		      "SetConfigItems(): no value for POP_BEFORE_SMTP  in config file");

	pop_before_smtp = (strcasecmp(val, "yes") == 0);

	trace(TRACE_DEBUG, "SetConfigItems(): %s POP-before-SMTP",
	      pop_before_smtp ? "Enabling" : "Disabling");


	/* read items: EFFECTIVE-USER */
	config_get_value("EFFECTIVE_USER", "POP", val);
	if (strlen(val) == 0)
		trace(TRACE_FATAL,
		      "SetConfigItems(): no value for EFFECTIVE_USER in config file");

	strncpy(config->serverUser, val, FIELDSIZE);
	config->serverUser[FIELDSIZE - 1] = '\0';

	trace(TRACE_DEBUG,
	      "SetConfigItems(): effective user shall be [%s]",
	      config->serverUser);


	/* read items: EFFECTIVE-GROUP */
	config_get_value("EFFECTIVE_GROUP", "POP", val);
	if (strlen(val) == 0)
		trace(TRACE_FATAL,
		      "SetConfigItems(): no value for EFFECTIVE_GROUP in config file");

	strncpy(config->serverGroup, val, FIELDSIZE);
	config->serverGroup[FIELDSIZE - 1] = '\0';

	trace(TRACE_DEBUG,
	      "SetConfigItems(): effective group shall be [%s]",
	      config->serverGroup);
}
