/*
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

/* $Id: debug.c 2114 2006-05-10 18:21:42Z paul $
 *
 * Debugging and memory checking functions */

#include "dbmail.h"

/* the debug variables */
static trace_t TRACE_SYSLOG = TRACE_ERROR;  /* default: errors, warnings, fatals */
static trace_t TRACE_STDERR = TRACE_FATAL;  /* default: fatal errors only */

/*
 * configure the debug settings
 */
void configure_debug(trace_t trace_syslog, trace_t trace_stderr)
{
	TRACE_SYSLOG = trace_syslog;
	TRACE_STDERR = trace_stderr;
}

void func_memtst(const char *filename, int line, int tst)
{
	if (tst != 0)
		trace(TRACE_FATAL,
		      "func_memtst(): fatal: %s:%d Memory error, result should not be NULL)",
		      filename, line);
}

void trace(trace_t level, char *formatstring, ...)
{
	va_list argp;

	gchar *message;
	size_t l;

	va_start(argp, formatstring);
	message = g_strdup_vprintf(formatstring, argp);
	va_end(argp);
	l = strlen(message);
	
	if (level <= TRACE_STDERR) {
		fprintf(stderr, message);
		if (message[l] != '\n')
			fprintf(stderr, "\n");
	}

	if (level <= TRACE_SYSLOG) {
		if (message[l] == '\n')
			message[l] = '\0';
		if (level <= TRACE_WARNING) {
			/* set LOG_ALERT at warnings */
			syslog(LOG_ALERT, "%s", message);
		} else
			syslog(LOG_NOTICE, "%s", message);
	}
	g_free(message);

	/* Bail out on fatal errors. */
	if (level == TRACE_FATAL)
		exit(EX_TEMPFAIL);
}


