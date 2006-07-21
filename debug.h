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

 
 $Id$
 
 headers for debug.c 
 
*/

#ifndef  _DEBUG_H
#define  _DEBUG_H

#include "dbmail.h"

#ifdef USE_GC
#define GC_DEBUG
#include <gc/gc.h>
#endif

typedef enum {
	TRACE_FATAL = 0,
	TRACE_ERROR,
	TRACE_WARNING,
	TRACE_MESSAGE,
	TRACE_INFO,
	TRACE_DEBUG
} trace_t;

#define memtst(tstbool) func_memtst (__FILE__,__LINE__,tstbool)

/* Define several macros for GCC specific attributes.
 * Although the __attribute__ macro can be easily defined
 * to nothing, these macros make them a little prettier.
 * */
#ifdef __GNUC__
#define UNUSED __attribute__((__unused__))
#define PRINTF_ARGS(X, Y) __attribute__((format(printf, X, Y)))
#else
#define UNUSED
#define PRINTF_ARGS(X, Y)
#endif


#ifdef USE_GC

#define dm_malloc(s) GC_MALLOC(s)
#define dm_free(p) GC_FREE(p)
#define dm_calloc(n,p) GC_MALLOC((n) * (p))
#define dm_realloc(n,p) GC_REALLOC((n),(p))

#else

#define dm_malloc(s) g_malloc(s)
#define dm_free(p) g_free(p)
#define dm_calloc(n,p) g_malloc0(n,p)
#define dm_realloc(n,p) g_realloc(n,p)

#endif


void func_memtst(const char *filename, int line, int tst);
void trace(trace_t level, char *formatstring, ...) PRINTF_ARGS(2, 3);

void newtrace(trace_t level, const char * module,
		const char * file, const char * function,
		char *formatstring, ...) PRINTF_ARGS(5, 6);

void configure_debug(trace_t trace_syslog, trace_t trace_stderr);

#define dm_strdup(s) g_strdup(s)

#endif
