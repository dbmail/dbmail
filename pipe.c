/* $Id$
 * Functions for reading the pipe from the MTA */


#include "config.h"
#include "pipe.h"

#define HEADER_BLOCK_SIZE 1024
#define QUERY_SIZE 255

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
	
  trace (TRACE_INFO, "read_header(): readheader start\n");

  while ((end_of_header==0) && (!feof(stdin)))
    {
      /* fgets will read until \n occurs */
      strblock = fgets (strblock, READ_BLOCK_SIZE, stdin);
      usedmem += (strlen(strblock)+1);
	
      /* If this happends it's a very big header */	
      if (usedmem>HEADER_BLOCK_SIZE)
	memtst(((char *)realloc(header,usedmem))==NULL);
		
      /* now we concatenate all we have to the header */
      memtst((header=strcat(header,strblock))==NULL);

      /* check if the end of header has occured */
      if (strstr(header,"\n\n")!=NULL)
	{
	  /* we've found the end of the header */
	  trace (TRACE_DEBUG,"read_header(): end header found\n");
	  end_of_header=1;
	}
		
      /* reset strlen to 0 */
      *strblock='\0';
    }
	
  trace (TRACE_INFO, "read_header(): readheader done\n");
  trace (TRACE_DEBUG, "read_header(): found header [%s]\n",header);
	
  free(strblock);
	
  if (usedmem==0)
    {
      free(strblock);
      free(header);
      trace (TRACE_STOP, "read_header(): not a valid mailheader found\n");
      *blksize=0;
    }
  else
    *blksize=strlen(header);

  trace (TRACE_INFO, "read_header(): function successfull\n");
  return header;
}

int insert_messages(char *header, unsigned long headersize, struct list *users)
{
  /* 	this loop gets all the users from the list 
	and check if they're in the database */

  struct element *tmp, *tmp_pipe, *descriptor_temp;
  char *insertquery;
  char *updatequery;
  char *unique_id;
  char *strblock;
  char *domain, *ptr;
  char *tmpbuffer=NULL;
  char *sendmail_command;
  size_t usedmem=0, totalmem=0;
  struct list userids;
  struct list messageids;
  FILE *sendmail_pipe;
  struct list external_forwards;
  struct list bounces;
  struct list descriptors;
  unsigned long temp_message_record_id,userid;
  int i,err;
  
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
	
  /* get the first target address */
  tmp=list_getstart(users);

  while (tmp!=NULL)
    {
      /* loops all mailusers and adds them to the list */
      /* db_check_user(): returns a list with character array's containing 
       * either userid's or forward addresses 
       */
      db_check_user((char *)tmp->data,&userids);
      trace (TRACE_DEBUG,"insert_messages(): user [%s] found total of [%d] aliases",(char *)tmp->data,
	     userids.total_nodes);
      
      if (userids.total_nodes==0) /* userid's found */
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
    
      /* user does not exists in aliases tables
	 so bounce this message back with an error message */
      if (userids.total_nodes==0)
	{
	  /* still no effective deliveries found, create bouncelist */
	  list_nodeadd(&bounces, tmp->data, strlen(tmp->data)+1);
	}

      /* get the next taget in list */
      tmp=tmp->nextnode;
    }
		
  /* get first target uiserid */
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
	  list_nodeadd(&external_forwards,tmp->data,strlen(tmp->data)+1);
	}
      else
	{
	  /* make the id numeric */
	  userid=atol((char *)tmp->data);

	  /* create a message record */
	  temp_message_record_id=db_insert_message ((unsigned long *)&userid);

	  /* message id is an array of returned message id's
	   * all messageblks are inserted for each message id
	   * we could change this in the future for efficiency
	   * still we would need a way of checking which messageblks
	   * belong to which messages */
		
	  /* adding this messageid to the message id list */
	  list_nodeadd(&messageids,&temp_message_record_id,sizeof(temp_message_record_id));
		
	  /* adding the first header block per user */
	  db_insert_message_block (header,temp_message_record_id);
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
	
	/* here we'll loop until we've read all what's left in the buffer  */

  if (list_totalnodes(&messageids)>0)
    {
      /* we have local deliveries */ 
      while (!feof(stdin))
	{
	  usedmem = fread (strblock, sizeof(char), READ_BLOCK_SIZE, stdin);
			
	  if (usedmem>0) /* this happends when a eof occurs */
	    {
	      totalmem=totalmem+usedmem;
			
	      tmp=list_getstart(&messageids);

	      while (tmp!=NULL)
		{
		  db_insert_message_block (strblock,*(unsigned long *)tmp->data);
		  tmp=tmp->nextnode;
		}
	    }
	  /* resetting strlen for strblock */
	  *strblock='\0';
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
	  temp_message_record_id=*(unsigned long*)tmp->data;
	  tmp=tmp->nextnode;
	}
    }

  /* handle all bounced messages */
  if (list_totalnodes(&bounces)>0)
    {
      /* bouncing invalid messages */
      trace (TRACE_DEBUG,"insert_messages(): sending bounces");
      tmp=list_getstart(&bounces);
      while (tmp!=NULL)
	{	
	  bounce (header,(char *)tmp->data,BOUNCE_NO_SUCH_USER);
	  tmp=tmp->nextnode;	
	}
    }

  /* do we have forward addresses ? */
  if (list_totalnodes(&external_forwards)>0)
    {
      /* sending the message to forwards */
  
      trace (TRACE_DEBUG,"insert_messages(): delivering to external addresses");
  
	      if (list_totalnodes(&messageids)==0)
		{
		  trace (TRACE_DEBUG,
			 "insert_messages(): Forwarding message directly through pipes");
					
		  /* this is tricky. Since there are no messages inserted 
		     in the database we need to redirect the pipe into all forwards */

		  /* first open the pipes and send the header */
		  while (tmp_pipe!=NULL)
		    {
		      sendmail_command = (char *)malloc(strlen((char *)tmp_pipe->data)+strlen(SENDMAIL)+1); /* add 1 for the space */
				sprintf (sendmail_command,"%s %s",SENDMAIL,(char *)tmp_pipe->data);
				trace (TRACE_DEBUG,"insert_messages(): opening pipe using command [%s]",sendmail_command);
		      sendmail_pipe = popen(sendmail_command,"w");
		      trace (TRACE_DEBUG,"insert_messages(): popen() executed");
				free (sendmail_command);

		      if (sendmail_pipe!=NULL)
			{
			  /* build a list of descriptors */
			  trace (TRACE_DEBUG,"insert_messages(): popen() successfull "
				 "[descriptor=%d]",fileno(sendmail_pipe));

			  /* -2 will send the complete message to sendmail_pipe */
			  fprintf (sendmail_pipe,"%s",header);
			  trace (TRACE_DEBUG,"insert_messages(): wrote header to pipe");

			  /* add the external pipe descriptor to a list */
			  list_nodeadd(&descriptors,&sendmail_pipe,sizeof(FILE*));
			}
		      else 
			{
			  trace (TRACE_ERROR,"insert_messages(): Could not open pipe to [%s]",
				 SENDMAIL);
			}
		      /* get next receipient */
		      tmp_pipe=tmp_pipe->nextnode;
		    }
			
		  if (descriptors.total_nodes>0)
		    {
		      trace (TRACE_DEBUG,"insert_messages(): forwarding to %d addresses",
			     descriptors.total_nodes);

		      while (!feof(stdin))
			{
			  usedmem = fread (strblock, sizeof(char), READ_BLOCK_SIZE, stdin);
									
			  if (usedmem>0) /* this happends when a eof occurs */
			    {
			      totalmem=totalmem+usedmem;

			      trace(TRACE_DEBUG,"insert_messages(): "
				    "Sending block size=[%d] total=[%d]",usedmem, totalmem);

			      descriptor_temp = list_getstart(&descriptors);
			      while (descriptor_temp!=NULL)
				{
				  trace (TRACE_DEBUG,"insert_messages(): fprintf now");
				  err = ferror(*((FILE **)(descriptor_temp->data)));
				  trace (TRACE_DEBUG,"insert_messages(): "
					 "ferror reports %d, feof reports %d on descriptor %d",err, 
					 feof(*((FILE **)(descriptor_temp->data))),
				    fileno(*((FILE **)(descriptor_temp->data))));

				   /* if (!err)
				    fwrite (strblock, sizeof(char), usedmem,
					    (FILE *)(descriptor_temp->data));  */
				  fprintf (*((FILE **)(descriptor_temp->data)),"%s",strblock);

				  trace (TRACE_DEBUG,"insert_messages(): wrote data");
				  descriptor_temp=descriptor_temp->nextnode;
				  trace (TRACE_DEBUG,"insert_messages(): fprintf done");
				}
			      /* resetting strlen for strblock */
			      strblock[0]='\0';
			      usedmem = 0;
			    }
			  else 
			    trace (TRACE_DEBUG,
				   "insert_messages(): End of STDIN reached. we're done here");
			}

		      /* done forwarding */
		      trace(TRACE_DEBUG, "insert_messages(): Closing pipes");
		      descriptor_temp = list_getstart(&descriptors);
		      while (descriptor_temp!=NULL)
			{
			  if (descriptor_temp->data!=NULL) 
			    {
					 if (!ferror(*((FILE **)(descriptor_temp->data))))
							{
							fprintf (*((FILE **)(descriptor_temp->data)),"\n.\n"); 
							trace(TRACE_DEBUG,"insert_messages: descriptor %d closed",
									fileno(*((FILE **)(descriptor_temp->data))));
							pclose (*((FILE **)(descriptor_temp->data)));
							}
					 else 
							trace(TRACE_DEBUG,"insert_messages: descriptor already closed",
									fileno(*((FILE **)(descriptor_temp->data))));
			    }
			  else 
			    trace (TRACE_ERROR,"insert_messages(): Huh? "
				   "The descriptor died on me. That's not supposed to happen");
			  descriptor_temp=descriptor_temp->nextnode;
			}
		    }
		  else
		    {	
		      trace (TRACE_ERROR,"insert_message(): "
			     "Something went wrong when building a list "
			     "structure for pipe descriptors");
		    }
		 list_freelist(&descriptors.start); 
		}
	      else	
		{		
		  trace (TRACE_DEBUG,"insert_messages(): Forwarding message via database");
		  while (tmp_pipe!=NULL)
		    {
		      sendmail_command = (char *)malloc(strlen((char *)tmp_pipe->data)+strlen(SENDMAIL)+1); /* add 1 for the space */
				sprintf (sendmail_command,"%s %s",SENDMAIL,(char *)tmp_pipe->data);
				trace (TRACE_DEBUG,"insert_messages(): opening pipe using command [%s]",sendmail_command);
		      sendmail_pipe = popen(sendmail_command,"w");
		      trace (TRACE_DEBUG,"insert_messages(): popen() executed");
				free (sendmail_command);
				
		      if (sendmail_pipe!=NULL)
			{
			  trace (TRACE_DEBUG,"insert_messages(): popen() successfull");

			  /* -2 will send the complete message to sendmail_pipe */
			  fprintf (sendmail_pipe,"%s",tmpbuffer);
			  trace (TRACE_DEBUG,"insert_messages(): sending message from database");
			  db_send_message_special (sendmail_pipe, 
						   *(unsigned long*)tmp->data, -2, tmpbuffer); 
			}
		      else 
			{
			  trace (TRACE_ERROR,"insert_messages(): Could not open pipe to [%s]",
				 SENDMAIL);
			}
		      tmp_pipe=tmp_pipe->nextnode;
		    }
	    }
	 }
	
  trace (TRACE_DEBUG,"insert_messages(): Freeing memory blocks");
  /* memory cleanup */
  if (tmpbuffer!=NULL)
    {
      trace (TRACE_DEBUG,"insert_messages(): tmpbuffer freed");
      free(tmpbuffer);
    }
  trace (TRACE_DEBUG,"insert_messages(): header freed");
  free(header);
  trace (TRACE_DEBUG,"insert_messages(): uniqueid freed");
  free(unique_id);
  trace (TRACE_DEBUG,"insert_messages(): strblock freed");
  free (strblock);
  trace (TRACE_DEBUG,"insert_messages(): insertquery freed");
  free(insertquery);
  trace (TRACE_DEBUG,"insert_messages(): updatequery freed");
  free(updatequery);
  trace (TRACE_DEBUG,"insert_messages(): End of function");
  
  list_freelist(&bounces.start);
  list_freelist(&userids.start);
  list_freelist(&messageids.start);
  list_freelist(&external_forwards.start);
  
  return 0;
}
