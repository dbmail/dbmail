/* $Id$
 * Functions for reading the pipe from the MTA */


#include "config.h"
#include "pipe.h"

#define HEADER_BLOCK_SIZE 1024
#define QUERY_SIZE 255
/* including the possible all escape strings blocks */

void create_unique_id(char *target, unsigned long messageid)
{
  time_t now;
  time(&now);
  trace (TRACE_DEBUG,"create_unique_id(): createding id",target);
  snprintf (target,UID_SIZE,"%luA%lu",messageid,now);
  trace (TRACE_DEBUG,"create_unique_id(): created: %s",target);
}
	

char *read_header(unsigned long *blksize)
     /* returns <0 on failure */
{
  /* reads incoming pipe until header is found */
  /* we're going to check every DB_READ_BLOCK_SIZE if header is read in memory */

  char *header, *strblock;
  int usedmem=0; 
  int end_of_header=0;
	
  memtst ((strblock = (char *)malloc(READ_BLOCK_SIZE))==NULL);
  memtst ((header = (char *)malloc(HEADER_BLOCK_SIZE))==NULL);

  /* here we will start a loop to read in the message header */
  /* the header will be everything up until \n\n or an EOF of */
  /* in_stream (stdin) */
	
  trace (TRACE_INFO, "read_header(): readheader start");

  while ((end_of_header==0) && (!feof(stdin)))
    {
		trace(TRACE_DEBUG,"Reading blocks");
		usedmem = fread (strblock, sizeof (char), READ_BLOCK_SIZE, stdin);
		trace (TRACE_DEBUG,"First block read, size [%d]",strlen(strblock));
      usedmem=usedmem + strlen(strblock);
		
      if (usedmem>HEADER_BLOCK_SIZE)
	
	/* add one for \0, since we use strlen for size */
	memtst(((char *)realloc(header,usedmem+1))==NULL);
		
      /* now we concatenate all we have to the header array */
      memtst((header=strcat (header,strblock))==NULL);
      if (strstr(header,"\n\n")!=NULL)
	end_of_header=1;
		
      /* reset strlen to 0 */
      strblock[0]='\0';
    }
	
  trace (TRACE_INFO, "read_header(): readheader done");
  trace (TRACE_DEBUG, "read_header(): found header [%s]",header);
	
  free(strblock);
	
  if (usedmem==0)
    {
      free(strblock);
      free(header);
      trace (TRACE_STOP, "read_header(): not a valid mailheader found");
    }
  *blksize=strlen(header);

  trace (TRACE_INFO, "read_header(): function successfull");
  return header;
}

int insert_messages(char *firstblock, unsigned long headersize, struct list *users)
{
  /* 	this loop gets all the users from the list 
	and check if they're in the database
	firstblock is the header which was already read */

  struct element *tmp, *tmp_pipe;
  char *insertquery;
  char *updatequery;
  char *unique_id;
  char *strblock;
  char *domain, *ptr;
  char *myscan, *nextscan;
  char *tmpbuffer;
  size_t usedmem=0, totalmem=0;
  struct list userids;
  struct list messageids;
  void *sendmail_pipe;
  struct list external_forwards;
  struct list bounces;
  
  unsigned long temp,userid;
  int i;
  
	/* step 1.
	 inserting first message
	 first insert the header into the database
	 the result is the first message block
	 next create a message record
	 update the first block with the messagerecord id number
	 add the rest of the messages
	 update the last and the total memory field*/
	
	/* creating a message record for the user */
	/* all email users to which this message is sent much receive this */

	
  memtst((insertquery = (char *)malloc(QUERY_SIZE))==NULL);
  memtst((updatequery = (char *)malloc(QUERY_SIZE))==NULL);
  memtst((unique_id = (char *)malloc(UID_SIZE))==NULL);

  /* initiating list with userid's */
  list_init(&userids);

  /* initiating list with messageid's */
  list_init(&messageids);
	
  /* initiating list with external forwards */
	list_init(&external_forwards);

	/* initiating list with bounces */
	list_init (&bounces);
	
  tmp=list_getstart(users);

  while (tmp!=NULL)
    {
      /* loops all mailusers and adds them to the list */
      /* db_check_user(): returns a list with character array's containing 
		 * either userid's or forward addresses */
      db_check_user((char *)tmp->data,&userids);
      trace (TRACE_DEBUG,"insert_messages(): user [%s] found total of [%d] aliases",(char *)tmp->data,
	     userids.total_nodes);
      
		if (userids.total_nodes==0)
			{
				/* I needed to change this because my girlfriend said so
					and she was actually right. Domain forwards are last resorts
					if a delivery cannot be found with an existing address then
					and only then we need to check if there are domain delivery's */
				
				trace (TRACE_INFO,"insert_messages(): no users found to deliver to. Checking for domain forwards");	
				
				domain=strchr((char *)tmp->data,'@');
				if (domain!=NULL)	/* this should always be the case! */
				{
					trace (TRACE_DEBUG,"insert_messages(): checking for domain aliases. Domain = [%s]",domain);
					/* checking for domain aliases */
					db_check_user(domain,&userids);
					trace (TRACE_DEBUG,"insert_messages(): domain [%s] found total of [%d] aliases",domain,
						userids.total_nodes);
				}
			}
    
	 	/* user does not excists in aliases tables
			so bounce this message back with an error message */
      if (userids.total_nodes==0)
			list_nodeadd(&bounces, tmp->data, strlen(tmp->data)+1);

      tmp=tmp->nextnode;
    }
		
   tmp=list_getstart(&userids);

  while (tmp!=NULL)
    {	
      /* traversing list with userids and creating a message for each userid */
		
		/* checking if tmp->data is numeric. If so, we should try to 
		 * insert to that address in the database 
		 * else we need to forward the message 
		 * ---------------------------------------------------------
		 * FIXME: The id needs to be checked!, it might be so that it is set in the 
		 * virtual user table but that doesn't mean it's valid! */

		trace (TRACE_DEBUG,"insert_messages(): alias deliver_to is [%s]",
				(char *)tmp->data);
		
		ptr=(char *)tmp->data;
		i = 0;
		
		while (isdigit(ptr[0]))
				{
				i++;
				ptr++;
				}
		
		if (i<strlen((char *)tmp->data))
			{
				/* FIXME: it's probably a forward to another address
				 * to make sure it could be a mailaddress we're checking for a @*/
				trace (TRACE_DEBUG,"insert_messages(): no numeric value in deliver_to, calling external_forward");
				/* creating a list of external forward addresses */
				list_nodeadd(&external_forwards,tmp->data,strlen((char *)tmp->data));
			}

		else
		{
			userid=atol((char *)tmp->data);

	      temp=db_insert_message ((unsigned long *)&userid);

			/* message id is an array of returned message id's
			 * all messageblks are inserted for each message id
			 * we could change this in the future for efficiency
			 * still we would need a way of checking which messageblks
			 * belong to which messages */
		
			/* adding this messageid to the message id list */
			list_nodeadd(&messageids,&temp,sizeof(temp));
		
		   /* adding the first header block per user */
			db_insert_message_block (firstblock,temp);
		}
			/* get next item */	
		   tmp=tmp->nextnode;
    }

	trace(TRACE_MESSAGE,"insert_messages(): we need to deliver [%lu] messages to external addresses",
			list_totalnodes(&external_forwards));
	
  
  /* reading rest of the pipe and creating messageblocks 
	 * we need to create a messageblk for each messageid */

  trace (TRACE_DEBUG,"insert_messages(): allocating [%d] bytes of memory for readblock",READ_BLOCK_SIZE);

  memtst ((strblock = (char *)malloc(READ_BLOCK_SIZE))==NULL);
	
	/* here we'll loop until we've read all what's left in the buffer 
	 * fread is used here because we want large blocks */

	if (list_totalnodes(&messageids)>0)
	{
		while (!feof(stdin))
		{
			usedmem = fread (strblock, sizeof (char), READ_BLOCK_SIZE, stdin);
		
			if (strblock!=NULL) /* this happends when a eof occurs */
			{
				totalmem=totalmem+usedmem;
			
				tmp=list_getstart(&messageids);

				while (tmp!=NULL)
				{
					trace(TRACE_DEBUG,"insert_messages(): inserting for [%lu]",
						*(unsigned long *)tmp->data);
					db_insert_message_block (strblock,*(unsigned long *)tmp->data);
					tmp=tmp->nextnode;
				}
			}
		/* resetting strlen for strblock */
		strblock[0]='\0';
		usedmem = 0;
		}
		
		trace (TRACE_DEBUG,"insert_messages(): updating size fields");
	
		/* we need to update messagesize in all messages */
		tmp=list_getstart(&messageids);
		while (tmp!=NULL)
		{
			/* we need to create a unique id per message 
			* we're using the messageidnr for this, it's unique 
			* a special field is created in the database for other possible 
			* even more unique strings */
			create_unique_id(unique_id,*(unsigned long*)tmp->data); 
			db_update_message ((unsigned long*)tmp->data,unique_id,totalmem+headersize);
			trace (TRACE_MESSAGE,"insert_messages(): message id=%lu, size=%lu is inserted",
				*(unsigned long*)tmp->data, totalmem+headersize);
			temp=*(unsigned long*)tmp->data;
			tmp=tmp->nextnode;
		}
	}

  if (list_totalnodes(&bounces)>0)
  {
		/* bouncing invalid messages */
		trace (TRACE_DEBUG,"insert_messages(): sending bounces");
		tmp=list_getstart(&bounces);
		while (tmp!=NULL)
		{	
			bounce (firstblock,(char *)tmp->data,BOUNCE_NO_SUCH_USER);
			tmp=tmp->nextnode;	
		}
  }

  /* do we have forward addresses ? */
	if (list_totalnodes(&external_forwards)>0)
	{
		/* sending the message to forwards */
  
		trace (TRACE_DEBUG,"insert_messages(): delivering to external addresses");
  
		/* send deliver to */
		/* send header without original To field */
		/* i don't know if this the legal way (tm) */
		
		/* we're storing the new header in tmpbuffer */	
		memtst((tmpbuffer = (char *)malloc(strlen(firstblock)+1025))==NULL); 
	
		myscan=strstr(firstblock,"\nTo:");
		if (myscan!=NULL)
		{
			myscan++;
			nextscan=strchr(myscan,'\n');
			if (nextscan!=NULL)
			{
				nextscan++;
				myscan--;
				myscan[0]='\0';
			
				/* getting a message. Any message will do */
				tmp=list_getstart(&messageids); 
			
				/* sending block to external deliverers 
					we're using the db_send_message_lines() call for this */
				tmp_pipe=list_getstart(&external_forwards); 

				if (list_totalnodes(&messageids)==0)
				{

					trace (TRACE_DEBUG,"insert_messages(): Forwarding message directly through pipes");
					
					/* this is tricky. Since there are no messages inserted 
						in the database we need to redirect the pipe into all forwards */
					/* list_init (&descriptors); */

					/* first open the pipes and send the header */
					while (tmp_pipe!=NULL)
					{
						sprintf (tmpbuffer,"%s\nTo: %s\n%s",firstblock,
								(char *)tmp_pipe->data,nextscan);
						trace (TRACE_DEBUG,"insert_messages(): new header [%s]",tmpbuffer);
						(FILE *)sendmail_pipe=popen(SENDMAIL,"w");
						trace (TRACE_DEBUG,"insert_messages(): popen() executed (forked)");
						if (sendmail_pipe!=NULL)
						{
							/* build a list of descriptors */
							trace (TRACE_DEBUG,"insert_messages(): popen() successfull");
							/* -2 will send the complete message to sendmail_pipe */
							fprintf ((FILE *)sendmail_pipe,"%s",tmpbuffer);
							while (!feof(stdin))
							{
								usedmem = fread (strblock, sizeof (char), READ_BLOCK_SIZE, stdin);
	
								if (strblock!=NULL) /* this happends when a eof occurs */
								{
								totalmem=totalmem+usedmem;
		
								trace(TRACE_DEBUG,"insert_messages(): Sending block size=[%d] total=[%d]",
										usedmem, totalmem);
								fprintf ((FILE *)sendmail_pipe,"%s",strblock);
								/* resetting strlen for strblock */
								strblock[0]='\0';
								usedmem = 0;
								}
								else 
									trace (TRACE_DEBUG,"insert_messages(): End of STDIN reached. we're done here");
							}
							/* done forwarding */
							trace(TRACE_DEBUG, "insert_messages(): Closing pipe");
							{	
								if (sendmail_pipe!=NULL)
									fprintf ((FILE *)sendmail_pipe,"\n.\n");
								else 
									trace(TRACE_DEBUG, "insert_messages(): The pipe just died on me?!?");
							}
						}
						else 
						{
							trace (TRACE_ERROR,"insert_messages(): Could not open pipe to [%s]",SENDMAIL);
						}
						tmp_pipe=tmp_pipe->nextnode;
					}
				}
				else	
				{		
					trace (TRACE_DEBUG,"insert_messages(): Forwarding message via database");
					while (tmp_pipe!=NULL)
					{
						sprintf (tmpbuffer,"%s\nTo: %s\n%s",firstblock,
								(char *)tmp_pipe->data,nextscan);
						trace (TRACE_DEBUG,"insert_messages(): new header [%s]",tmpbuffer);
						(FILE *)sendmail_pipe=popen(SENDMAIL,"w");
						trace (TRACE_DEBUG,"insert_messages(): popen() executed");
						if (sendmail_pipe!=NULL)
						{
							trace (TRACE_DEBUG,"insert_messages(): popen() successfull");
							/* -2 will send the complete message to sendmail_pipe */
							fprintf ((FILE *)sendmail_pipe,"%s",tmpbuffer);
							trace (TRACE_DEBUG,"insert_messages(): sending message from database");
							db_send_message_special (sendmail_pipe, *(unsigned long*)tmp->data, -2, tmpbuffer); 
						}
						else 
						{
							trace (TRACE_ERROR,"insert_messages(): Could not open pipe to [%s]",SENDMAIL);
						}

					tmp_pipe=tmp_pipe->nextnode;
					}
				}
			}
			else 
			{
				trace (TRACE_ERROR,"insert_messages(): Could not forward message. Header is invalid");
			}
		}
		else
			{
				trace (TRACE_ERROR,"insert_messages(): Could not forward message. Header is invalid");
			}
	}
	
	trace (TRACE_DEBUG,"insert_messages(): Freeing memory blocks");
	/* memory cleanup */
	free(tmpbuffer);
	free(firstblock);  
	free(unique_id);
	free (strblock);
	free(insertquery);
	free(updatequery);
	trace (TRACE_DEBUG,"insert_messages(): End of function");
  return 0;
}
