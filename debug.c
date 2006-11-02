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

/* $Id: debug.c 2347 2006-11-01 21:35:52Z paul $
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

/* Make sure that these match trace_t. */
static const char * const trace_text[] = {
	"FATAL",
	"Error",
	"Warning",
	"Message",
	"Info",
	"Debug"
};

static const char * trace_to_text(trace_t level)
{
	return trace_text[level];
}

/* Call me like this:
 *
 * TRACE(TRACE_ERROR, "Something happened with error code [%d]", resultvar);
 *
 * Please #define THIS_MODULE "mymodule" at the top of each file.
 * Arguments for __FILE__ and __func__ are added by the TRACE macro.
 *
 * trace() and TRACE() are macros in debug.h
 *
 */

void newtrace(int isnew, trace_t level, const char * module,
		const char * file, const char * function,
		int line, char *formatstring, ...)
{
	va_list argp;

	gchar *message;
	const char *format = "%s:[%s] %s,%s(+%d): %s";
	size_t l;

	/* Return now if we're not logging anything. */
	if (level > TRACE_STDERR && level > TRACE_SYSLOG)
		return;

	va_start(argp, formatstring);

	message = g_strdup_vprintf(formatstring, argp);

	va_end(argp);
	l = strlen(message);
	
	if (level <= TRACE_STDERR) {
		if (isnew && TRACE_STDERR >= TRACE_DEBUG) {
			fprintf(stderr, format, trace_to_text(level), module, file, function, line, message);
		} else {
			fprintf(stderr, "%s %s", trace_to_text(level), message);
		}
		if (message[l] != '\n')
			fprintf(stderr, "\n");
		fflush(stderr);
	}

	if (level <= TRACE_SYSLOG) {
		if (message[l] == '\n')
			message[l] = '\0';
		if (level <= TRACE_WARNING) {
			/* set LOG_ALERT at warnings */
			if (isnew && TRACE_SYSLOG >= TRACE_DEBUG) {
				syslog(LOG_ALERT, format, trace_to_text(level), module, file, function, line, message);
			} else {
				syslog(LOG_ALERT, "%s %s", trace_to_text(level), message);
			}
		} else {
			if (isnew && TRACE_SYSLOG >= TRACE_DEBUG) {
				syslog(LOG_NOTICE, format, trace_to_text(level), module, file, function, line, message);
			} else {
				syslog(LOG_NOTICE, "%s %s", trace_to_text(level), message);
			}
		}
	}
	g_free(message);

	/* Bail out on fatal errors. */
	if (level == TRACE_FATAL)
		exit(EX_TEMPFAIL);
}


