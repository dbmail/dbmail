/*	$Id$
 *	(c) 2000-2001 IC&S, The Netherlands
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

void func_memtst (char filename[255],int line,int tst);
void trace (int level, const char *formatstring, ...);
void configure_debug(int level, int trace_syslog, int trace_verbose);

#endif
