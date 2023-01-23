/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2004-2013 NFG Net Facilities Group BV support@nfg.nl
 Copyright (c) 2014-2019 Paul J Stevens, The Netherlands, support@nfg.nl
 Copyright (c) 2020-2023 Alan Hicks, Persistent Objects Ltd support@p-o.co.uk

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
 *
 * Debugging and memory checking functions */

#include "dbmail.h"
#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(BSD4_4)
extern char     *__progname;            /* Program name, from crt0. */
#else
char            *__progname = NULL;
#endif

char		hostname[16];
 
#define THIS_MODULE "debug"

/* the debug variables */
static Trace_T TRACE_SYSLOG = TRACE_EMERG | TRACE_ALERT | TRACE_CRIT | TRACE_ERR | TRACE_WARNING;  /* default: emerg, alert, crit, err, warning */
static Trace_T TRACE_STDERR = TRACE_EMERG | TRACE_ALERT | TRACE_CRIT | TRACE_ERR | TRACE_WARNING;  /* default: emerg, alert, crit, err, warning */

/*
 * libzdb abort handler to handle logs correctly
 */
void TabortHandler(const char *error)
{
	trace(TRACE_ALERT, "libzdb", __func__, __LINE__, "%s", error);
}

/*
 * configure the debug settings
 */
FILE *fstderr = NULL;

static void configure_stderr(const char *service_name)
{
	Field_T error_log;
	config_get_value("errorlog", service_name, error_log);
	if (! (fstderr = freopen(error_log, "a", stderr))) {
		int serr = errno;
		TRACE(TRACE_ERR, "freopen failed on [%s] [%s]", error_log, strerror(serr));
	}
}

void configure_debug(const char *service_name, Trace_T trace_syslog, Trace_T trace_stderr)
{
	Trace_T old_syslog, old_stderr;
	
	old_syslog = TRACE_SYSLOG;
	old_stderr = TRACE_STDERR;

	configure_stderr(service_name?service_name:"DBMAIL");

	TRACE_SYSLOG = trace_syslog;
	TRACE_STDERR = trace_stderr;

	if ((old_syslog != trace_syslog) || (old_stderr != trace_stderr)) {
		TRACE(TRACE_INFO, "[%s] syslog [%d -> %d] stderr [%d -> %d]",
				service_name?service_name:"DBMAIL", 
				old_syslog, trace_syslog,
				old_stderr, trace_stderr);
	}
}

/* Make sure that these match Trace_T. */
void null_logger(const char UNUSED *log_domain, GLogLevelFlags UNUSED log_level, const char UNUSED *message, gpointer UNUSED data)
{
	// ignore glib messages	
	return;
}

static const char * Trace_To_text(Trace_T level)
{
	const char * const Trace_Text[] = {
		"EMERGENCY",
		"Alert",
		"Critical",
		"Error",
		"Warning",
		"Notice",
		"Info",
		"Debug",
		"Database"
	};
	return Trace_Text[ilogb((double) level)];
}

/* Call me like this:
 *
 * TRACE(TRACE_ERR, "Something happened with error code [%d]", resultvar);
 *
 * Please #define THIS_MODULE "mymodule" at the top of each file.
 * Arguments for __FILE__ and __func__ are added by the TRACE macro.
 *
 * trace() and TRACE() are macros in debug.h
 *
 */

#define SYSLOGFORMAT "%s:[%s] %s(+%d): %s"
#define STDERRFORMAT "%s %s %s[%d]: [%p] %s:[%s] %s(+%d): %s"
#define MESSAGESIZE 4096

void trace(Trace_T level, const char * module, const char * function, int line, const char *formatstring, ...)
{
	Trace_T syslog_level;
	va_list ap, cp;

	char message[MESSAGESIZE];

	static int configured=0;
	size_t l;

	/* Return now if we're not logging anything. */
	if ( !(level & TRACE_STDERR) && !(level & TRACE_SYSLOG))
		return;

	memset(message, 0, sizeof(message));

	va_start(ap, formatstring);
	va_copy(cp, ap);
	vsnprintf(message, MESSAGESIZE-1, formatstring, cp);
	va_end(cp);
	va_end(ap);

	l = strlen(message);
	
	if (level & TRACE_STDERR) {
		time_t now = time(NULL);
		struct tm tmp;
		char date[33];

 		if (! configured) {
 			memset(hostname,'\0',sizeof(hostname));
 			gethostname(hostname,sizeof(hostname)-1);
 			configured=1;
 		}
 
		memset(date,0,sizeof(date));
		localtime_r(&now, &tmp);
		strftime(date,32,"%b %d %H:%M:%S", &tmp);

 		fprintf(stderr, STDERRFORMAT, date, hostname, __progname?__progname:"", getpid(), 
			g_thread_self(), Trace_To_text(level), module, function, line, message);
 
		if (message[l - 1] != '\n')
			fprintf(stderr, "\n");
		fflush(stderr);
	}

	if (level & TRACE_SYSLOG) {
		/* Convert our extended log levels (>128) to syslog levels */
		switch((int)ilogb((double) level))
		{
			case 0:
				syslog_level = LOG_EMERG;
				break;
			case 1:
				syslog_level = LOG_ALERT;
				break;
			case 2:
				syslog_level = LOG_CRIT;
				break;
			case 3:
				syslog_level = LOG_ERR;
				break;
			case 4:
				syslog_level = LOG_WARNING;
				break;
			case 5:
				syslog_level = LOG_NOTICE;
				break;
			case 6:
				syslog_level = LOG_INFO;
				break;
			case 7:
				syslog_level = LOG_DEBUG;
				break;
			case 8:
				syslog_level = LOG_DEBUG;
				break;
			default:
				syslog_level = LOG_DEBUG;
				break;
		}
		if (l > MESSAGESIZE) {
			l = MESSAGESIZE;
			message[l - 1] = 0;
		}
		syslog(syslog_level, SYSLOGFORMAT, Trace_To_text(level), module, function, line, message);
	}

	/* Bail out on fatal errors. */
	if (level == TRACE_EMERG)
		exit(EX_TEMPFAIL);
}


