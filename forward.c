/*
 Copyright (C) 1999-2003 IC & S  dbmail@ic-s.nl

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
 * (c) 2000-2002 IC&S, The Netherlands
 *
 * takes care of forwarding mail to an external address */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <time.h>
#include "db.h"
#include "debug.h"
#include "list.h"
#include "bounce.h"
#include "forward.h"
#include "dbmail.h"
#include <string.h>
#include <stdlib.h>

extern struct list smtpItems;


/* For each of the addresses or programs in targets,
 * send out a copy of the message pointed to by msgidnr.
 *
 * Returns 0 if all went well, -1 if there was an error.
 * FIXME: there is no detail in the error reporting,
 * so there's no way to tell *which* targets failed...
 * */
int forward(u64_t msgidnr, struct list *targets, const char *from, const char *header, u64_t headersize UNUSED)
{

  struct element *target=NULL;
  char *command=NULL;
  FILE *pipe=NULL;
  int err;
  field_t sendmail;
  char timestr[50];
  time_t td;
  struct tm tm;
 

  time(&td);              /* get time */
  tm = *localtime(&td);   /* get components */
  strftime(timestr, sizeof(timestr), "%a %b %e %H:%M:%S %Y", &tm);
  
  GetConfigValue("SENDMAIL", &smtpItems, sendmail);
  if (sendmail[0] == '\0')
    trace(TRACE_FATAL, "forward(): SENDMAIL not configured (see config file). Stop.");
  
  trace(TRACE_INFO, "forward(): delivering to [%ld] external addresses",
		  list_totalnodes(targets));

  if (!msgidnr)
    {
      trace(TRACE_ERROR, "forward(): got NULL as message id number");
      return -1;
    }
  
  target = list_getstart (targets);

  while (target != NULL)
    {
      if ((((char *)target->data)[0]=='|') || (((char *)target->data)[0]=='!'))
	{
        
	  /* external pipe command */
	  command = (char *)my_malloc(strlen((char *)(target->data)));
	  if (!command)
	    {
	      trace(TRACE_ERROR,"forward(): out of memory");
	      return -1;
	    }
	  strcpy (command, (char *)(target->data)+1); /* skip the pipe (|) sign */
	}
      else
	{
	  /* pipe to sendmail */
	  command = (char *)my_malloc(strlen((char *)(target->data))+
					    strlen(sendmail)+2); /* +2 for extra space and \0 */
	  if (!command)
	    {
	      trace(TRACE_ERROR,"forward(): out of memory");
	      return -1;
	    }

	  trace(TRACE_DEBUG, "forward(): allocated memory for external command call");
	  sprintf (command, "%s %s", sendmail, (char *)(target->data));
	}

      trace(TRACE_INFO, "forward(): opening pipe to command %s", command);
	
      pipe = popen(command, "w"); /* opening pipe */
      my_free (command);
      command = NULL;

      if (pipe != NULL)
	{
	  trace(TRACE_DEBUG, "forward(): call to popen() successfully opened pipe [%d]",
			  fileno(pipe));
			
          if (((char *)target->data)[0]=='!')
            {
              /* ! tells us to prepend an mbox style header in this pipe */
              trace(TRACE_DEBUG, "forward(): appending mbox style from header to pipe returnpath : %s", from);
              /* format: From<space>address<space><space>Date */
              fprintf (pipe, "From %s  %s\n", from, timestr);   
            }

          /* first send header if this is a direct pipe through */
          fprintf (pipe, "%s", header);
          trace(TRACE_DEBUG, "forward(): wrote header to pipe");  

	  trace(TRACE_INFO, "forward(): sending message id number [%llu] to forward pipe", msgidnr);
			
          err = ferror(pipe);

          trace(TRACE_DEBUG, "forward(): ferror reports"
              " %d, feof reports %d on pipe %d", err,
              feof (pipe),
              fileno (pipe));

          if (!err)
            {
              if (msgidnr != 0)
                {
                  trace(TRACE_DEBUG, "forward(): sending lines from"
                      "message %llu on pipe %d", msgidnr, fileno(pipe));
                  db_send_message_lines (pipe, msgidnr, -2, 1);
                }
            }

	  trace(TRACE_DEBUG, "forward(): closing pipes");

          if (!ferror(pipe))
            {
              pclose (pipe);
              trace(TRACE_DEBUG, "forward(): pipe closed");
            }
          else
            {
              trace(TRACE_ERROR, "forward(): error on pipe");
            }
        }
      else 
        {
          trace(TRACE_ERROR, "forward(): Could not open pipe to [%s]", sendmail);
        }
      target = target->nextnode;
    }

  return 0;			
}

