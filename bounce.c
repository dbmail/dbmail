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
 * Bounce.c implements functions to bounce email back to a sender 
 * with a message saying why the message was bounced */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dbmail.h"
#include "bounce.h"
#include "list.h"
#include "mime.h"
#include "db.h"
#include "debug.h"
#include <stdlib.h>
#include <string.h>

extern struct list mimelist;  
extern struct list users;  
extern struct list smtpItems;  

int bounce(const char *header, const char *destination_address, bounce_reason_t reason)
{
  void *sendmail_stream;
  char *sendmail_command = NULL;
  struct list from_addresses;
  struct element *tmpelement;
  field_t dbmail_from_address, sendmail, postmaster;


  /* reading configuration from db */
  GetConfigValue("DBMAIL_FROM_ADDRESS", &smtpItems, dbmail_from_address);
  if (dbmail_from_address[0] == '\0')
    trace(TRACE_FATAL, "%s,%s: DBMAIL_FROM_ADDRESS not configured "
	  "(see config file). Stop.", __FILE__, __FUNCTION__);

  GetConfigValue("SENDMAIL", &smtpItems, sendmail);
  if (sendmail[0] == '\0')
    trace(TRACE_FATAL, "%s,%s: SENDMAIL not configured "
	  "(see config file). Stop.", __FILE__, __FUNCTION__);
	
  GetConfigValue("POSTMASTER", &smtpItems, postmaster);
  if (postmaster[0] == '\0')
    trace(TRACE_FATAL, "%s,%s: POSTMASTER not configured "
	  "(see config file). Stop.", __FILE__, __FUNCTION__);
	  
  trace (TRACE_DEBUG,"%s,%s: creating bounce message for bounce reason [%d]",
	 __FILE__, __FUNCTION__, reason);
		
  if (!destination_address)
    {
	 trace(TRACE_ERROR,"%s,%s: cannot deliver to NULL.", 
	       __FILE__, __FUNCTION__);
      return -1;
    }


  switch (reason)
    {
    case BOUNCE_NO_SUCH_USER:
      /* no such user found */
      trace (TRACE_MESSAGE,"%s,%s: sending 'no such user' bounce "
	     "for destination [%s]", __FILE__, __FUNCTION__,
	     destination_address);
      list_init(&from_addresses);
      /* scan the from header for addresses */
      mail_adr_list ("Return-Path", &from_addresses,&mimelist);

      if (list_totalnodes(&from_addresses) == 0)
	{
	  trace (TRACE_INFO,"%s,%s: can't find Return-Path values, "
		 "resorting to From values", __FILE__, __FUNCTION__);
	  mail_adr_list ("From", &from_addresses, &mimelist);
	} /* RR logix :) */

      /* loop target addresses */
      if (list_totalnodes(&from_addresses) > 0)
	{
	  tmpelement=list_getstart (&from_addresses);
	  while (tmpelement!=NULL)
	    {
	      /* open a stream to sendmail 
		 the sendmail macro is defined in bounce.h */
	      sendmail_command = (char *)my_malloc(
			    strlen((char *)(tmpelement->data))+
			    strlen(sendmail)+2); /* +2 for extra space and \0 */
		    if (!sendmail_command)
	        {
	          trace(TRACE_ERROR,"bounce(): out of memory");
	          list_freelist(&from_addresses.start);
	          return -1;
	        }

	      trace (TRACE_DEBUG,"bounce(): allocated memory for"
		     " external command call");
	      sprintf (sendmail_command, "%s %s",
		       sendmail, (char *)(tmpelement->data));

              trace (TRACE_INFO,"bounce(): opening pipe to command "
	             "%s",sendmail_command);
	      (FILE *)sendmail_stream=popen (sendmail_command,"w");

	      if (sendmail_stream==NULL)
		{
		  /* could not open a succesfull stream */
		  trace(TRACE_MESSAGE,"%s,%s: could not open a pipe "
			"to %s", __FILE__, __FUNCTION__,sendmail);
		  return -1;
		}
	      fprintf ((FILE *)sendmail_stream,"From: %s\n",dbmail_from_address);
	      fprintf ((FILE *)sendmail_stream,"To: %s\n",(char *)tmpelement->data);
	      fprintf ((FILE *)sendmail_stream,"Subject: DBMAIL: delivery failure\n");
	      fprintf ((FILE *)sendmail_stream,"\n");
	      fprintf ((FILE *)sendmail_stream,"This is the DBMAIL-SMTP program.\n\n");
	      fprintf ((FILE *)sendmail_stream,"I'm sorry to inform you that your message, addressed to %s,\n",
		       destination_address);
	      fprintf ((FILE *)sendmail_stream,"could not be delivered due to the following error.\n\n");
	      fprintf ((FILE *)sendmail_stream,"*** E-mail address %s is not known here. ***\n\n",destination_address);
	      fprintf ((FILE *)sendmail_stream,"If you think this message is incorrect please contact %s.\n\n",postmaster);
	      fprintf ((FILE *)sendmail_stream,"Header of your message follows...\n\n\n");
	      fprintf ((FILE *)sendmail_stream,"--- header of your message ---\n");
	      fprintf ((FILE *)sendmail_stream,"%s",header);
	      fprintf ((FILE *)sendmail_stream,"--- end of header ---\n\n\n");
	      fprintf ((FILE *)sendmail_stream,"\n.\n");
	      pclose ((FILE *)sendmail_stream);

	      /* jump forward to next recipient */
	      tmpelement=tmpelement->nextnode;
	    }
	}
      else
	{
	  trace(TRACE_MESSAGE,"%s,%s: "
		"Message does not have a Return-Path nor a From headerfield, "
		"bounce failed", __FILE__, __FUNCTION__);
	}
      
      break;
    case BOUNCE_STORAGE_LIMIT_REACHED:
      /* mailbox size exceeded */
      trace (TRACE_MESSAGE,"%s,%s: sending 'mailboxsize exceeded' bounce "
	     "for user [%s].",  __FILE__, __FUNCTION__, destination_address);
      list_init(&from_addresses);
	
      /* scan the Return-Path header for addresses 
	 if they don't exist, resort to From addresses */
      mail_adr_list ("Return-Path", &from_addresses,&mimelist);
      
      if (list_totalnodes(&from_addresses) == 0)
	{
	  trace (TRACE_INFO,"%s,%s: can't find Return-Path values, "
		 "resorting to From values", __FILE__, __FUNCTION__);
	  mail_adr_list ("From", &from_addresses, &mimelist);
	} /* RR logix :) */
      
      if (list_totalnodes(&from_addresses)>0)
	{
	  /* loop target addresses */
	  tmpelement=list_getstart (&from_addresses);
	  while (tmpelement!=NULL)
	    {
	      /* open a stream to sendmail 
		 the sendmail macro is defined in bounce.h */
		    sendmail_command = (char *)my_malloc(strlen((char *)(tmpelement->data))+
					    strlen(sendmail)+2); /* +2 for extra space and \0 */
	      if (!sendmail_command)
	        {
	          trace(TRACE_ERROR,"bounce(): out of memory");
	          list_freelist(&from_addresses.start);
	          return -1;
	        }

	      trace (TRACE_DEBUG,"bounce(): allocated memory for"
		     " external command call");
	      sprintf (sendmail_command, "%s %s",sendmail, (char *)(tmpelement->data));

              trace (TRACE_INFO,"bounce(): opening pipe to command "
	             "%s",sendmail_command);
	      (FILE *)sendmail_stream=popen (sendmail_command,"w");

	      if (sendmail_stream==NULL)
		{
		  /* could not open a succesfull stream */
		  trace(TRACE_MESSAGE,"%s,%s: could not open a pipe to %s",
			__FILE__, __FUNCTION__, sendmail);
		  return -1;
		}
	      fprintf ((FILE *)sendmail_stream,"From: %s\n",dbmail_from_address);
	      fprintf ((FILE *)sendmail_stream,"To: %s\n",(char *)tmpelement->data);
	      fprintf ((FILE *)sendmail_stream,"Subject: DBMAIL: delivery failure\n");
	      fprintf ((FILE *)sendmail_stream,"\n");
	      fprintf ((FILE *)sendmail_stream,"This is the DBMAIL-SMTP program.\n\n");
	      fprintf ((FILE *)sendmail_stream,"I'm sorry to inform you that your message, addressed to %s,\n",
		       destination_address);
	      fprintf ((FILE *)sendmail_stream,"could not be delivered due to the following error.\n\n");
	      fprintf ((FILE *)sendmail_stream,"*** Mailbox of user %s is FULL ***\n\n",destination_address);
	      fprintf ((FILE *)sendmail_stream,"If you think this message is incorrect please contact %s.\n\n",postmaster);
	      fprintf ((FILE *)sendmail_stream,"Header of your message follows...\n\n\n");
	      fprintf ((FILE *)sendmail_stream,"--- header of your message ---\n");
	      fprintf ((FILE *)sendmail_stream,"%s",header);
	      fprintf ((FILE *)sendmail_stream,"--- end of header ---\n\n\n");
	      fprintf ((FILE *)sendmail_stream,"\n.\n");
	      pclose ((FILE *)sendmail_stream);

	      /* jump forward to next recipient */
	      tmpelement=tmpelement->nextnode;
	    }
	}
      else
	{
	  trace(TRACE_MESSAGE,"%s,%s: "
		"Message does not have a Return-Path nor a From headerfield, "
		"bounce failed", __FILE__, __FUNCTION__);
	break;
      }
    }

  return 0;
}

