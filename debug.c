/* $Id$
 * (c) 2000-2001 IC&S, The Netherlands
 *
 * Debugging and memory checking functions */

#include "debug.h"

#define err_out_stream stderr
#define EXIT_CODE 75

/* the debug variables */
int TRACE_TO_SYSLOG = 1; /* default: yes */
int TRACE_VERBOSE = 0;   /* default: no */

int TRACE_LEVEL = 2;     /* default: normal operations */
 
/*
 * configure the debug settings
 */
void configure_debug(int level, int trace_syslog, int trace_verbose)
{
  TRACE_LEVEL = level;
  TRACE_TO_SYSLOG = trace_syslog;
  TRACE_VERBOSE = trace_verbose;
}


void func_memtst (char filename[255],int line,int tst)
{
  if (tst) 
    trace(TRACE_FATAL,"func_memtst(): fatal: %s:%d Memory error, result should not be NULL)",
	  filename,line);
}

void trace (int level, const char *formatstring, ...)
{
  va_list argp;
  
  va_start(argp, formatstring);

  if (level <= TRACE_LEVEL)
    {
      if (TRACE_VERBOSE)
	vfprintf (err_out_stream, formatstring, argp);
      if (TRACE_TO_SYSLOG)
	{
	  if (level <= TRACE_WARNING) 
	    {
	      /* set LOG_ALERT at warnings */
	      vsyslog (LOG_ALERT, formatstring, argp);
	    }
	  else 
	    vsyslog (LOG_NOTICE, formatstring, argp);
	}
      va_end(argp);
    }
  else
    if (level == TRACE_MESSAGE)
      {
	vsyslog (LOG_NOTICE, formatstring, argp);
	va_end(argp);
      }

  /* very big fatal error 
   * bailout */
	
  if (level==TRACE_FATAL)
    exit(EXIT_CODE);

  if (level==TRACE_STOP)
    exit(EXIT_CODE);
}
