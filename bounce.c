/* $Id$
	bounce.c takes care of bouncing undeliverably messages */

#include "bounce.h"
#include "list.h"
#include "mime.h"

int bounce (char *header, char *destination_address, int type)
{
	void *sendmail_stream;
	struct list from_addresses;
	struct element *tmpelement;
	
	trace (TRACE_DEBUG,"bounce(): creating bounce message for bounce type [%d]",type);

		
	switch (type)
	{
		case BOUNCE_NO_SUCH_USER:
			{
			/* no such user found */
			trace (TRACE_MESSAGE,"bounce(): sending 'no such user' bounce for destination [%s]",
					destination_address);
			list_init(&from_addresses);
			/* scan the from header for addresses */
			mail_adr_list ("from", &from_addresses);

			/* loop target addresses */
			tmpelement=list_getstart (&from_addresses);
			while (tmpelement!=NULL)
				{
				/* open a stream to sendmail 
				the SENDMAIL macro is defined in bounce.h */

				(FILE *)sendmail_stream=popen (SENDMAIL,"w");
	
				if (sendmail_stream==NULL)
					{
					/* could not open a succesfull stream */
					trace(TRACE_MESSAGE,"bounce(): could not open a pipe to %s",SENDMAIL);
					return -1;
					}
				fprintf ((FILE *)sendmail_stream,"From: %s\n",DBMAIL_FROM_ADDRESS);
				fprintf ((FILE *)sendmail_stream,"To: %s\n",(char *)tmpelement->data);
				fprintf ((FILE *)sendmail_stream,"Subject: DBMAIL: delivery failure\n");
				fprintf ((FILE *)sendmail_stream,"\n");
				fprintf ((FILE *)sendmail_stream,"This is DBMAIL-SMTP program.\n");
				fprintf ((FILE *)sendmail_stream,"I'm sorry to inform you that your message, addressed to %s,",
						destination_address);
				fprintf ((FILE *)sendmail_stream,"could not be delivered due to the following error.\n");
				fprintf ((FILE *)sendmail_stream,"E-mail address %s is not known here.\n",destination_address);
				fprintf ((FILE *)sendmail_stream,"Header of your message follows...\n\n");
				fprintf ((FILE *)sendmail_stream,"--- header of your message ---\n");
				fprintf ((FILE *)sendmail_stream,"%s\n",header);
				fprintf ((FILE *)sendmail_stream,"--- end of header ---\n");
				fprintf ((FILE *)sendmail_stream,"\n.\n");
				/* jump forward to next recipient */
				pclose ((FILE *)sendmail_stream);
				
				tmpelement=tmpelement->nextnode;
				}
			break;
			}
	}
	
	return 0;
}
