/* $Id$
 * (c) 2000-2001 IC&S, The Netherlands
 *
 * Bounce.c implements functions to bounce email back to a sender 
 * with a message saying why the message was bounced */

	
#include "bounce.h"
#include "list.h"
#include "mime.h"
#include "dbmysql.h"
#include "debug.h"

 extern char *header; 
 extern unsigned long headersize; 

 extern struct list mimelist;  
 extern struct list users;  

int bounce (char *header, char *destination_address, int type)
{
  void *sendmail_stream;
  struct list from_addresses;
  struct element *tmpelement;
  char *dbmail_from_address;
  char *sendmail;

  /* reading configuration from db */
  dbmail_from_address = db_get_config_item ("DBMAIL_FROM_ADDRESS",CONFIG_MANDATORY);
  sendmail = db_get_config_item ("SENDMAIL", CONFIG_MANDATORY);
	
  trace (TRACE_DEBUG,"bounce(): creating bounce message for bounce type [%d]",type);
		
  if (!destination_address)
    {
      trace(TRACE_ERROR,"bounce(): cannot deliver to NULL.");
      return -1;
    }

  switch (type)
    {
    case BOUNCE_NO_SUCH_USER:
      {
	/* no such user found */
	trace (TRACE_MESSAGE,"bounce(): sending 'no such user' bounce for destination [%s]",
	       destination_address);
	list_init(&from_addresses);
	/* scan the from header for addresses */
	mail_adr_list ("from", &from_addresses,&mimelist,&users,header,headersize);

	/* loop target addresses */
	tmpelement=list_getstart (&from_addresses);
	while (tmpelement!=NULL)
	  {
				/* open a stream to sendmail 
				   the sendmail macro is defined in bounce.h */

	    (FILE *)sendmail_stream=popen (sendmail,"w");
	
	    if (sendmail_stream==NULL)
	      {
		/* could not open a succesfull stream */
		trace(TRACE_MESSAGE,"bounce(): could not open a pipe to %s",sendmail);
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
	    fprintf ((FILE *)sendmail_stream,"If you think this message is incorrect please contact %s.\n\n",POSTMASTER);
	    fprintf ((FILE *)sendmail_stream,"Header of your message follows...\n\n\n");
	    fprintf ((FILE *)sendmail_stream,"--- header of your message ---\n");
	    fprintf ((FILE *)sendmail_stream,"%s",header);
	    fprintf ((FILE *)sendmail_stream,"--- end of header ---\n\n\n");
	    fprintf ((FILE *)sendmail_stream,"\n.\n");
	    pclose ((FILE *)sendmail_stream);
				
				/* jump forward to next recipient */
	    tmpelement=tmpelement->nextnode;
	  }
	break;
      };
    case BOUNCE_STORAGE_LIMIT_REACHED:
      {
	/* mailbox size exceeded */
	trace (TRACE_MESSAGE,"bounce(): sending 'mailboxsize exceeded' bounce for user [%s]",
	       destination_address);
	list_init(&from_addresses);
	/* scan the from header for addresses */
	mail_adr_list ("from", &from_addresses,&mimelist,&users,header,headersize);

	/* loop target addresses */
	tmpelement=list_getstart (&from_addresses);
	while (tmpelement!=NULL)
	  {
				/* open a stream to sendmail 
				   the sendmail macro is defined in bounce.h */

	    (FILE *)sendmail_stream=popen (sendmail,"w");
	
	    if (sendmail_stream==NULL)
	      {
		/* could not open a succesfull stream */
		trace(TRACE_MESSAGE,"bounce(): could not open a pipe to %s",sendmail);
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
	    fprintf ((FILE *)sendmail_stream,"If you think this message is incorrect please contact %s.\n\n",POSTMASTER);
	    fprintf ((FILE *)sendmail_stream,"Header of your message follows...\n\n\n");
	    fprintf ((FILE *)sendmail_stream,"--- header of your message ---\n");
	    fprintf ((FILE *)sendmail_stream,"%s",header);
	    fprintf ((FILE *)sendmail_stream,"--- end of header ---\n\n\n");
	    fprintf ((FILE *)sendmail_stream,"\n.\n");
	    pclose ((FILE *)sendmail_stream);
				
				/* jump forward to next recipient */
	    tmpelement=tmpelement->nextnode;
	  }
	break;
      }
    }
  my_free (dbmail_from_address);
  my_free (sendmail);
  return 0;
}
