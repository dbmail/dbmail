/*	$Id$
 *	(c) 2000-2002 IC&S, The Netherlands
 *
 *	debug.h : headers for debug.c */

#include <stdio.h>
#include <sys/syslog.h>
#include <stdarg.h>

#ifndef  _DEBUG_H
#define  _DEBUG_H

extern int TRACE_TO_SYSLOG;
extern int TRACE_VERBOSE;

extern int TRACE_LEVEL;     /* 5: maximum debugging */
                            /* 2: normal operations */

#define TRACE_FATAL -1
#define TRACE_STOP 0
#define TRACE_MESSAGE 1
#define TRACE_ERROR 2
#define TRACE_WARNING 3
#define TRACE_INFO 4
#define TRACE_DEBUG 5

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


void func_memtst (const char *filename,int line,int tst);
void trace (int level, char *formatstring, ...);
void configure_debug(int level, int trace_syslog, int trace_verbose);

void* __debug_malloc(unsigned long size, const char *fname, int linenr);
void __debug_free(void *ptr, const char *fname, int linenr);

void __debug_dumpallocs();

#endif
