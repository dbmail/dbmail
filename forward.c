/* $Id$
 * (c) 2000-2001 IC&S, The Netherlands
 *
 * takes care of forwarding mail to an external address */

#include <stdio.h>
#include <time.h>
#include "dbmysql.h"
#include "debug.h"
#include "list.h"
#include "bounce.h"
#include "forward.h"

int pipe_forward(FILE *instream, struct list *targets, char *from, char *header, unsigned long databasemessageid)
{

  struct list descriptors; /* target streams */
  struct element *target=NULL;
  struct element *descriptor_temp=NULL;
  char *sendmail_command=NULL;
  char *strblock=NULL;
  FILE *sendmail_pipe=NULL;
  int usedmem, totalmem;
  int err;
  char *sendmail;
  char timestr[50];
  time_t td;
  struct tm tm;

 
  /* takes input from instream and forwards that directly to 
     a number of pipes (depending on the targets. Sends headers
     first */


  time(&td);              /* get time */
  tm = *localtime(&td);   /* get components */
  strftime(timestr, sizeof(timestr), "%a %b %e %H:%M:%S %Y", &tm);
  
  totalmem = 0;
	
  sendmail = db_get_config_item ("SENDMAIL", CONFIG_MANDATORY);
  
  trace (TRACE_INFO,"pipe_forward(): delivering to %d "
	 "external addresses", list_totalnodes(targets));

  if (!instream)
    {
      trace(TRACE_ERROR,"pipe_forward(): got NULL as instream");
      return -1;
    }
  
  memtst ((strblock = (char *)my_malloc(READ_BLOCK_SIZE))==NULL);
	
  target = list_getstart (targets);

  list_init(&descriptors);

  while (target != NULL)
    {
      if ((((char *)target->data)[0]=='|') || (((char *)target->data)[0]=='!'))
	{
        
	  /* external pipe command */
	  sendmail_command = (char *)my_malloc(strlen((char *)(target->data)));
	  if (!sendmail_command)
	    {
	      trace(TRACE_ERROR,"pipe_forward(): out of memory");
	      list_freelist(&descriptors.start);
	      my_free(strblock);
	      return -1;
	    }
	  strcpy (sendmail_command, (char *)(target->data)+1); /* skip the pipe (|) sign */
	}
      else
	{
	  /* pipe to sendmail */
	  sendmail_command = (char *)my_malloc(strlen((char *)(target->data))+
					    strlen(sendmail)+2); /* +2 for extra space and \0 */
	  if (!sendmail_command)
	    {
	      trace(TRACE_ERROR,"pipe_forward(): out of memory");
	      list_freelist(&descriptors.start);
	      my_free(strblock);
	      return -1;
	    }

	  trace (TRACE_DEBUG,"pipe_forward(): allocated memory for"
		 " external command call");
	  sprintf (sendmail_command, "%s %s",sendmail, (char *)(target->data));
	}

      trace (TRACE_INFO,"pipe_forward(): opening pipe to command "
	     "%s",sendmail_command);
	
      sendmail_pipe = popen(sendmail_command,"w"); /* opening pipe */
      my_free (sendmail_command);
      sendmail_command = NULL;

      if (sendmail_pipe != NULL)
	{
	  trace (TRACE_DEBUG,"pipe_forward(): call to popen() successfull"
		 " opened descriptor %d", fileno(sendmail_pipe));
			
        if (((char *)target->data)[0]=='!')
        {
        /* ! tells u to prepend a mbox style header in this pipe */
            trace (TRACE_DEBUG,"pipe_forward(): appending mbox style from header to pipe returnpath : %s", from);
            /* format: From<space>address<space><space>Date */
            fprintf (sendmail_pipe,"From %s  %s\n",from,timestr);   
        }

	  /* first send header if this is a direct pipe through */
	  if (databasemessageid == 0)
	    {
				/* yes this is a direct pipe-through */
	      if (!header)
		{
		  trace(TRACE_ERROR,"pipe_forward(): could not write header to pipe: header is NULL");
		  list_freelist(&descriptors.start);
		  my_free(strblock);
		  return -1;
		}

	      fprintf (sendmail_pipe,"%s",header);
	      trace (TRACE_DEBUG,"pipe_forward(): wrote header to pipe");  
	    }

	  /* add descriptor to pipe to a descriptors list */
	  if (list_nodeadd(&descriptors, &sendmail_pipe, sizeof(FILE *))==NULL)
	    trace (TRACE_ERROR,"pipe_forward(): failed to add descriptor");

	}
      else 
	{
	  trace (TRACE_ERROR,"pipe_forward(): Could not open pipe to"
		 " [%s]",sendmail);
	}
      target = target->nextnode;
    }

  if (descriptors.total_nodes>0)
    {

      if (databasemessageid != 0)
	{
	  /* send messages directly from database
	   * using message databasemessageid */
			
	  trace (TRACE_INFO,"pipe_forward(): writing to pipe using dbmessage %lu",
		 databasemessageid);
			
	  descriptor_temp = list_getstart(&descriptors);
	  while (descriptor_temp!=NULL)
	    {
	      err = ferror(*((FILE **)(descriptor_temp->data)));
				
	      trace (TRACE_DEBUG, "pipe_forward(): ferror reports"
		     " %d, feof reports %d on descriptor %d", err,
		     feof (*((FILE **)(descriptor_temp->data))),
		     fileno(*((FILE **)(descriptor_temp->data))));

	      if (!err)
		{
		  if (databasemessageid != 0)
		    {
		      db_send_message_lines (*((FILE **)(descriptor_temp->data)),
					     databasemessageid, -2, 1);
		    }
		}
	      descriptor_temp = descriptor_temp->nextnode;
	    }
	}

      else
		
	{
	  while (!feof (instream))
	    {
				/* read in a datablock */
	      usedmem = fread (strblock, sizeof(char), READ_BLOCK_SIZE, instream);
				
				/* fread won't do this for us */
	      if (strblock)
		strblock[usedmem]='\0';
				
				
	      if (databasemessageid != 0)
		trace(TRACE_INFO,"pipe_forward(): forwarding from database using id %lu",
		      databasemessageid);

			
	      if (usedmem>0)
		{
		  totalmem = totalmem + usedmem;
	
		  trace (TRACE_DEBUG,"pipe_forward(): Sending block"
			 "size=%d total=%d (%d\%)", usedmem, totalmem,
			 (((usedmem/totalmem)*100))); 
					
		  descriptor_temp = list_getstart(&descriptors);
		  while (descriptor_temp != NULL)
		    {
		      err = ferror(*((FILE **)(descriptor_temp->data)));
		      trace (TRACE_DEBUG, "pipe_forward(): ferror reports"
			     " %d, feof reports %d on descriptor %d", err,
			     feof (*((FILE **)(descriptor_temp->data))),
			     fileno(*((FILE **)(descriptor_temp->data))));
	
		      if (!err)
			{
			  if (databasemessageid != 0)
			    {
			      db_send_message_lines (*((FILE **)(descriptor_temp->data)),
						     databasemessageid, -2, 1);
			    }
			  else
			    {
			      fprintf (*((FILE **)(descriptor_temp->data)),"%s",strblock);
			    }
			}
		      else
			trace (TRACE_ERROR,"pipe_forward(): error writing"
			       " to pipe");
	
		      trace (TRACE_DEBUG,"pipe_forward(): wrote data to pipe");
	
		      descriptor_temp = descriptor_temp->nextnode;
		    }
	
				/* resetting buffer and index */
		  memset (strblock, '\0', READ_BLOCK_SIZE);
		  usedmem = 0;
		}
	      else
		{
		  trace(TRACE_DEBUG,"pipe_forward(): end of instream");
		}
	    }
			
	  /* done forwarding */
	  trace (TRACE_DEBUG, "pipe_forward(): closing pipes");
	  descriptor_temp = list_getstart(&descriptors);
	  while (descriptor_temp != NULL)
	    {
	      if (descriptor_temp->data != NULL)
		{
		  if (!ferror(*((FILE **)(descriptor_temp->data))))
		    {
		      pclose (*((FILE **)(descriptor_temp->data)));
		      trace (TRACE_DEBUG, "pipe_forward(): descriptor_closed");
		    }
		  else
		    {
		      trace (TRACE_ERROR,"pipe_forward(): error on descriptor");
		    }
		}
	      else
		{
		  trace (TRACE_ERROR,"pipe_forward(): descriptor value NULL"
			 " this is not supposed to happen");
		}
	      descriptor_temp = descriptor_temp->nextnode;
	    }
	  /* freeing descriptor list */
	  list_freelist(&descriptors.start);
	}
    }
  else
    {
      trace (TRACE_ERROR,"pipe_forward(): No descriptors in list"
	     " nothing to send");
      my_free(strblock);
      return -1;
    }

  my_free (sendmail);
  my_free (strblock);
  return 0;			
}


