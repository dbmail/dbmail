/* $Id$
 * Functions for reading the pipe from the MTA */

#include "config.h"
#include "pipe.h"

#define READ_BLOCK_SIZE 524288		/* be carefull, MYSQL has a limit */
#define HEADER_BLOCK_SIZE 1024
#define QUERY_SIZE 255
/* including the possible all escape strings blocks */

extern struct list users;

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
	/* we're going to check every READ_BLOCK_SIZE if header is read in memory */

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
		strblock=fgets(strblock,READ_BLOCK_SIZE,stdin);
	
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

int insert_messages(char *firstblock, unsigned long headersize)
{
	 /* 	this loop gets all the users from the list 
	 	and check if they're in the database
		firstblock is the header which was already read */
	
	struct element *tmp;
	char *insertquery;
	char *updatequery;
	char *unique_id;
	char *strblock;
	size_t usedmem=0, totalmem=0;
	struct list userids;
	struct list messageids;
	unsigned long temp;

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
	
	tmp=list_getstart(&users);

	
	while (tmp!=NULL)
		{
		/* loops all mailusers and adds them to the list */
		/* db_check_user(): returns a list with longs containing 
		 * userid's */
		db_check_user((char *)tmp->data,&userids);
		trace (TRACE_DEBUG,"insert_messages(): user [%s] found total of [%d] aliases",(char *)tmp->data,
			userids.total_nodes);
		tmp=tmp->nextnode;
		}
		
	tmp=list_getstart(&userids);

	while (tmp!=NULL)
		{	
		/* traversing list with userids and creating a message for each userid */
		trace (TRACE_DEBUG,"insert_messages(): -----> debug tmp is [%d],nextnode is [%d]",
				tmp,tmp->nextnode);
		sprintf(insertquery,"INSERT INTO message(useridnr,messagesize,status,unique_id) VALUES (%lu,0,0,\" \")",
			*(unsigned long*)tmp->data);

		trace (TRACE_DEBUG,"insert_messages(): executing query [%s]",insertquery);
		
		/* message id is an array of returned message id's
		 * all messageblks are inserted for each message id
		 * we could change this in the future for efficiency
		 * still we would need a way of checking which messageblks
		 * belong to which messages */
		
		db_query(insertquery);
		temp=db_insert_result();

		/* adding this messageid to the message id list */
		list_nodeadd(&messageids,&temp,sizeof(temp));
		
		/* adding the first header block per user */
		db_insert_message_block (firstblock,temp);
			
		tmp=tmp->nextnode;
		}

	/* reading rest of the pipe and creating messageblocks 
	 * we need to create a messageblk for each messageid */

	trace (TRACE_DEBUG,"insert_messages(): allocating [%d] memory for readblock",READ_BLOCK_SIZE);

	memtst ((strblock = (char *)malloc(READ_BLOCK_SIZE))==NULL);
	
	/* here we'll loop until we've read all what's left in the buffer 
	 * fread is used here because we want large blocks */

   while (!feof(stdin))
		{
		/* strblock=fgets(strblock,READ_BLOCK_SIZE,stdin); */
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

			/* resetting strlen for strblock */
			strblock[0]='\0';
			usedmem = 0;
			}
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
			sprintf(updatequery,
				"UPDATE message SET messagesize=%lu, unique_id=\"%s\" where messageidnr=%lu",
				totalmem+headersize,unique_id,*(unsigned long*)tmp->data);
			trace (TRACE_MESSAGE,"insert_messages(): message id=%lu, size=%lu is inserted",
				*(unsigned long*)tmp->data, totalmem+headersize);
			db_query(updatequery);
			tmp=tmp->nextnode;
			}
	
		free(unique_id);
		free (strblock);
		free(insertquery);
		free(updatequery);
		return 0;
}
