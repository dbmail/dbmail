/* $Id$
	bounce.c takes care of bouncing undeliverably messages */

#include "bounce.h"
#include "list.h"
#include "mime.h"

int bounce (char *header, char *destination_address, int type)
{
	void *sendmail_stream;
	struct list from_addresses;
	
	trace (TRACE_DEBUG,"bounce(): creating bounce message for bounce type [%d]",type);

	/* open a stream to sendmail 
		the SENDMAIL macro is defined in bounce.h */

	(FILE *)sendmail_stream=popen (SENDMAIL,"w");
	
	if (sendmail_stream==NULL)
		{
		/* could not open a succesfull stream */
		trace(TRACE_MESSAGE,"bounce(): could not open a pipe to %s",SENDMAIL);
		return -1;
		}
		
	switch (type)
	{
		case BOUNCE_NO_SUCH_USER:
			{
			/* no such user found */
			trace (TRACE_MESSAGE,"bounce(): sending 'no such user' bounce for destination [%s]",
					destination_address);
			/* scan the from header for addresses */
			mail_adr_list (
			
			break;
			}
	}
	
	pclose ((FILE *)sendmail_stream);
	return 0;
}
