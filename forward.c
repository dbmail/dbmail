/* $Id$
	takes care of forwarding mail to an external address */

#include "forward.h"

int pipe_forward(FILE *instream, struct list *targets, char *header, unsigned long databasemessageid)
{

	struct list descriptors; /* target streams */
	struct element *target;
	struct element *descriptor_temp;
	char *sendmail_command;
	char *strblock;
	FILE *sendmail_pipe;
	int usedmem, totalmem;
	int err;
	
	/* takes input from instream and forwards that directly to 
		a number of pipes (depending on the targets. Sends headers
		first */
	
	trace (TRACE_INFO,"pipe_forward(): delivering to %d"
			"external addresses", list_totalnodes(targets));

	memtst ((strblock = (char *)malloc(READ_BLOCK_SIZE))==NULL);
	
	target = list_getstart (targets);

	while (target != NULL)
	{
		sendmail_command = (char *)malloc(strlen((char *)target->data)+
				strlen(FW_SENDMAIL)+1); /* +1 for extra space */
		trace (TRACE_DEBUG,"pipe_forward(): allocated memory for"
				" external command call");

		sprintf (sendmail_command, "%s %s",FW_SENDMAIL, (char *)target->data);
		trace (TRACE_INFO,"pipe_forward(): opening pipe to command "
				"%s",sendmail_command);
	
		sendmail_pipe = popen(sendmail_command,"w"); /* opening pipe */
		free (sendmail_command);

		if (sendmail_pipe != NULL)
		{
			trace (TRACE_DEBUG,"pipe_forward(): call to popen() successfull"
					" opened descriptor %d", fileno(sendmail_pipe));

			/* first send header */
			fprintf (sendmail_pipe,"%s",header);
			trace (TRACE_DEBUG,"pipe_forward(): wrote header to pipe");

			/* add descriptor to pipe to a descriptors list */
			list_nodeadd(&descriptors, &sendmail_pipe, sizeof(FILE *));

		}
		else 
		{
			trace (TRACE_ERROR,"pipe_forward(): Could not open pipe to"
					" [%s]",FW_SENDMAIL);
		}
		target = target->nextnode;
	}

	if (descriptors.total_nodes>0)
	{
		while (!feof (instream))
		{
			/* read in a datablock */
			usedmem = fread (strblock, sizeof(char), READ_BLOCK_SIZE, instream);
			
			if (usedmem>0)
			{
				totalmem = totalmem + usedmem;

				trace (TRACE_DEBUG,"pipe_forward(): Sending block"
						"size=%d total=%d", usedmem, totalmem);
				
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
						if (!databasemessageid)
						{
							fprintf (*((FILE **)(descriptor_temp->data)),"%s",strblock);
						}
						else
						{
							trace (TRACE_DEBUG, "Sending from database");
							db_send_message_special (*((FILE **)(descriptor_temp->data)),
									databasemessageid, -2, header);
						}
					}
					else
						trace (TRACE_ERROR,"pipe_forward(): error writing"
								" to pipe");

					
					trace (TRACE_DEBUG,"pipe_forward(): wrote data to pipe");

					descriptor_temp = descriptor_temp->nextnode;
				}

				/* resetting buffer and index */
				strblock[0]='\0';
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
					fprintf (*((FILE **)(descriptor_temp->data)),"\n.\n");
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
	else
	{
		trace (TRACE_ERROR,"pipe_forward(): No descriptors in list"
				" nothing to send");
		return -1;
	}

	free (strblock);
	
	return 0;			
}


