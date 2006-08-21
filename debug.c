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

/* $Id$
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
	size_t l;

	va_start(argp, formatstring);

	/*
	if (isnew && TRACE_SYSLOG >= 5) {
		// If the global trace level is really high
		formatstring_verbose = g_strdup_printf("from %s, %s, %s: %s",
				module, file, function, formatstring);
	} else {
		// If the global trace level is really low
		formatstring = g_strdup_printf("%s: %s",
				module, formatstring);
	}
	*/

	message = g_strdup_vprintf(formatstring, argp);

	va_end(argp);
	l = strlen(message);
	
	if (level <= TRACE_STDERR) {
		if (isnew) {
			// FIXME: Only do this for high global trace level.
			fprintf(stderr, "%s module %s file %s func %s line %d: %s",
				trace_to_text(level), module, file, function, line, message);
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
			if (isnew) {
				syslog(LOG_ALERT, "%s module %s file %s func %s line %d: %s",
					trace_to_text(level), module, file, function, line, message);
			} else {
				syslog(LOG_ALERT, "%s %s", trace_to_text(level), message);
			}
		} else {
			if (isnew) {
				syslog(LOG_NOTICE, "%s module %s file %s func %s line %d: %s",
					trace_to_text(level), module, file, function, line, message);
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


