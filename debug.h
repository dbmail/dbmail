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
 * debug.h : headers for debug.c */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "dbmail.h"

#include <stdio.h>
#include <sys/syslog.h>
#include <stdarg.h>

#ifndef  _DEBUG_H
#define  _DEBUG_H

extern int TRACE_TO_SYSLOG;
extern int TRACE_VERBOSE;

extern int TRACE_LEVEL;		/* 5: maximum debugging */
			    /* 2: normal operations */

typedef enum {
	TRACE_FATAL = -1,
	TRACE_STOP,
	TRACE_MESSAGE,
	TRACE_ERROR,
	TRACE_WARNING,
	TRACE_INFO,
	TRACE_DEBUG
} trace_t;

#define memtst(tstbool) func_memtst (__FILE__,__LINE__,tstbool)

/*
#define my_malloc(s) __debug_malloc(s, __FILE__, __LINE__)
#define my_free(p) __debug_free(p, __FILE__, __LINE__)
#define __DEBUG_TRACE_MEMALLOC
*/

#define my_malloc(s) malloc(s)
#define my_free(p) free(p)
#ifdef __DEBUG_TRACE_MEMALLOC
#undef __DEBUG_TRACE_MEMALLOC
#endif


void func_memtst(const char *filename, int line, int tst);
void trace(trace_t level, char *formatstring, ...) PRINTF_ARGS(2, 3);
//void trace(trace_t level, char *formatstring, ...) __attribute__((format(printf, 2, 3)));

void configure_debug(trace_t level, int trace_syslog, int trace_verbose);

void *__debug_malloc(unsigned long size, const char *fname, int linenr);
void __debug_free(void *ptr, const char *fname, int linenr);

void __debug_dumpallocs(void);

#endif
