	/* Debugging and memory checking functions */

#include "debug.h"

#define err_out_stream stderr

void func_memtst (char filename[255],int line,int tst)
{
	if (tst) 
		trace(TRACE_FATAL,"func_memtst(): fatal: %s:%d Memory error, result should not be NULL)");
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
			vsyslog (LOG_NOTICE, formatstring, argp);

	/* very big fatal error 
	 * bailout */
	
	if (level==TRACE_FATAL)
		exit(1);

	if (level==TRACE_STOP)
		exit(0);
}
