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
 * Debugging and memory checking functions */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "debug.h"
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

struct debug_mem {
	long addr;
	int linenr;
	char fname[200];

	struct debug_mem *nextaddr;
};

typedef struct debug_mem debug_mem_t;

debug_mem_t *__dm_first = 0, *__dm_last = 0;

#define err_out_stream stderr
#define EXIT_CODE 75

/* the debug variables */
int TRACE_TO_SYSLOG = 1;	/* default: yes */
int TRACE_VERBOSE = 0;		/* default: no */
int TRACE_LEVEL = 2;		/* default: error operations */


/*
 * configure the debug settings
 */
void configure_debug(trace_t level, int trace_syslog, int trace_verbose)
{
	TRACE_LEVEL = level;
	TRACE_TO_SYSLOG = trace_syslog;
	TRACE_VERBOSE = trace_verbose;
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

	va_start(argp, formatstring);

	if (level <= TRACE_LEVEL) {
		if (TRACE_VERBOSE != 0) {
			vfprintf(err_out_stream, formatstring, argp);
			if (formatstring[strlen(formatstring)] != '\n')
				fprintf(err_out_stream, "\n");
		}
		if (TRACE_TO_SYSLOG != 0) {
			if (formatstring[strlen(formatstring)] == '\n')
				formatstring[strlen(formatstring)] = '\0';
			if (level <= TRACE_WARNING) {
				/* set LOG_ALERT at warnings */
				vsyslog(LOG_ALERT, formatstring, argp);
			} else
				vsyslog(LOG_NOTICE, formatstring, argp);
		}
		va_end(argp);
	}

	/* very big fatal error 
	 * bailout */

	if (level == TRACE_FATAL)
		exit(EX_TEMPFAIL);

	if (level == TRACE_STOP)
		exit(EX_TEMPFAIL);
}

void *__debug_malloc(unsigned long size, const char *fname, int linenr)
{
	void *ptr = malloc(size);
	debug_mem_t *new;

	if (!ptr)
		return NULL;

	new = (debug_mem_t *) malloc(sizeof(debug_mem_t));

	if (!new) {
		trace(TRACE_WARNING,
		      "__debug_malloc(): could not add malloc to list (call: %s, %d)\n",
		      fname, linenr);
		return ptr;
	}

	new->addr = (long) ptr;
	new->linenr = linenr;
	if (fname)
		strncpy(new->fname, fname, 200);
	else
		new->fname[0] = 0;

	new->fname[199] = 0;
	new->nextaddr = 0;

	if (!__dm_first) {
		__dm_first = new;
		__dm_last = new;
	} else {
		__dm_last->nextaddr = new;
		__dm_last = new;
	}

	return ptr;
}

void __debug_free(void *ptr, const char *fname, int linenr)
{
	debug_mem_t *curr = __dm_first, *prev = NULL;

	if (!ptr)
		return;

	while (curr && curr->addr != (long) ptr) {
		prev = curr;
		curr = curr->nextaddr;
	}

	if (!curr) {
		trace(TRACE_WARNING,
		      "__debug_free(): freeing a memory block that is not in the list\n");
		trace(TRACE_WARNING,
		      "__debug_free(): called in file %s, line %d\n",
		      fname, linenr);
		free(ptr);
		return;
	}

	if (prev) {
		prev->nextaddr = curr->nextaddr;
		if (__dm_last == curr)
			__dm_last = prev;
	} else {
		__dm_first = __dm_first->nextaddr;
		if (__dm_first == 0)
			__dm_last = 0;

/*      if (__dm_last == __dm_first)
	__dm_last = 0; */
	}

	free(curr);
	free(ptr);
}

void __debug_dumpallocs()
{
#ifdef __DEBUG_TRACE_MEMALLOC

	debug_mem_t *curr = __dm_first;

	trace(TRACE_WARNING,
	      "__debug_dumpallocs(): retrieving list of currently allocated items\n");

	while (curr) {
		trace(TRACE_WARNING, "    From %s, line %d: %X\n",
		      curr->fname, curr->linenr, curr);
		curr = curr->nextaddr;
	}

	trace(TRACE_WARNING, "\n__debug_dumpallocs(): end\n");

#endif
}


char * dm_strdup(const char *str)
{
	char *new_str;
	size_t length;
	
	if (str) {
		length = strlen(str) + 1;
		new_str = (char *)dm_malloc(length);
		memcpy(new_str,str,length);
	} else {
		new_str = NULL;
	}

	return new_str;
}
