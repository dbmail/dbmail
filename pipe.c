/* $Id$
 * (c) 2000-2002 IC&S, The Netherlands 
 *
 * Functions for reading the pipe from the MTA */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <time.h>
#include <ctype.h>
#include "db.h"
#include "auth.h"
#include "debug.h"
#include "list.h"
#include "bounce.h"
#include "forward.h"
#include "dbmail.h"
#include "pipe.h"
#include "debug.h"
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include "dbmd5.h"
#include "misc.h"


#define HEADER_BLOCK_SIZE 1024
#define QUERY_SIZE 255
#define MAX_U64_STRINGSIZE 40
#define MAX_COMM_SIZE 512

#define AUTO_NOTIFY_SENDER "autonotify@dbmail"
#define AUTO_NOTIFY_SUBJECT "NEW MAIL NOTIFICATION"

extern struct list smtpItems, sysItems;


int send_notification(const char *to, const char *from, const char *subject);
int send_reply(struct list *headerfields, const char *body);

void create_unique_id(char *target, u64_t messageid)
{
  trace (TRACE_DEBUG,"create_unique_id(): creating id");
  srand((int) ((int) time(NULL) + (int) getpid()) );
  snprintf (target,UID_SIZE,"%s",makemd5( itoa((int) rand() * (int) messageid) ));
  trace (TRACE_DEBUG,"create_unique_id(): created: %s",target);
}
	

char *read_header(u64_t *blksize)
     /* returns <0 on failure */
{
  /* reads incoming pipe until header is found */
  /* we're going to check every DB_READ_BLOCK_SIZE if header is read in memory */

  char *header, *strblock;
  int usedmem=0; 
  int end_of_header=0;
  int allocated_blocks=1;
	
  memtst ((strblock = (char *)my_malloc(READ_BLOCK_SIZE))==NULL);
  memtst ((header = (char *)my_malloc(HEADER_BLOCK_SIZE))==NULL);

  /* resetting */
  memset (strblock, '\0', READ_BLOCK_SIZE);
  memset (header, '\0', HEADER_BLOCK_SIZE);
  
  /* here we will start a loop to read in the message header */
  /* the header will be everything up until \n\n or an EOF of */
  /* in_stream (stdin) */
	
  trace (TRACE_INFO, "read_header(): readheader start\n");

  while ((end_of_header==0) && (!feof(stdin)))
    {
      /* fgets will read until \n occurs */
      strblock = fgets (strblock, READ_BLOCK_SIZE, stdin);
      if (strblock)
	usedmem += (strlen(strblock)+1);
	
      /* If this happends it's a very big header */	
      if (usedmem>(allocated_blocks*HEADER_BLOCK_SIZE))
      {
          /* update block counter */
          allocated_blocks++;
          trace (TRACE_DEBUG,"read_header(): mem current: [%d] reallocated to [%d]",
                  usedmem, allocated_blocks*HEADER_BLOCK_SIZE);
          memtst((header = (char *)realloc(header,allocated_blocks*HEADER_BLOCK_SIZE))==NULL);
      }
      /* now we concatenate all we have to the header */
      if (strblock)
	memtst((header=strcat(header,strblock))==NULL);
		
      /* check if the end of header has occured */
      if (strstr(header,"\n\n")!=NULL)
	{
	  /* we've found the end of the header */
	  trace (TRACE_DEBUG,"read_header(): end header found\n");
	  end_of_header=1;
	}
		
      /* reset strblock to 0 */
      if (strblock)
	memset (strblock,'\0',READ_BLOCK_SIZE);
    }
	
  trace (TRACE_INFO, "read_header(): readheader done");
  trace (TRACE_DEBUG, "read_header(): found header [%s]",header);
  trace (TRACE_DEBUG, "read_header(): header size [%d]",strlen(header));	
  my_free(strblock);
	
  if (usedmem==0)
    {
      my_free(strblock);
      my_free(header);
      *blksize=0;
      trace (TRACE_STOP, "read_header(): not a valid mailheader found\n");
      return 0;
    }
  else
    *blksize=strlen(header);

  trace (TRACE_INFO, "read_header(): function successfull\n");
  return header;
}


/*
 * if users_are_usernames is nonzero, the list *users is supposed to
 * contain actual usernames from the dbmail system (local delivery)
 */
int insert_messages(char *header, u64_t headersize, struct list *users, 
		    struct list *returnpath, int users_are_usernames, char *deliver_to_mailbox, struct list *headerfields)
{
  /* 	this loop gets all the users from the list 
	and check if they're in the database */

  struct element *tmp,*ret_path;
  char *insertquery;
  char *updatequery;
  char *unique_id;
  char *strblock;
  char *domain, *ptr;
  char *tmpbuffer=NULL;
  char *bounce_id;
  size_t usedmem=0, totalmem=0;
  char userid_string[MAX_U64_STRINGSIZE];
  struct list userids;
  struct list messageids;
  struct list external_forwards;
  struct list bounces;
  u64_t temp_message_record_id,userid, bounce_userid;
  int i, this_user;
  int do_auto_notify = 0, do_auto_reply = 0;
  char *reply_body, *notify_address;
  FILE *instream = stdin;
  field_t val;

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

	
  memtst((insertquery = (char *)my_malloc(QUERY_SIZE))==NULL);
  memtst((updatequery = (char *)my_malloc(QUERY_SIZE))==NULL);
  memtst((unique_id = (char *)my_malloc(UID_SIZE))==NULL);

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

      if (!users_are_usernames)
      {
          this_user = auth_check_user((char *)tmp->data,&userids,-1);
          trace (TRACE_DEBUG,"insert_messages(): "
                  "user [%s] found total of [%d] aliases",(char *)tmp->data,
                  userids.total_nodes);

          if (this_user==0) /* we did not find any direct delivers for this user */
          {
              /* I needed to change this because my girlfriend said so
                 and she was actually right. Domain forwards are last resorts
                 if a delivery cannot be found with an existing address then
                 and only then we need to check if there are domain delivery's */

              trace (TRACE_INFO,"insert_messages(): no users found to deliver to. "
                      "Checking for domain forwards");	

              domain=strchr((char *)tmp->data,'@');

              if (domain!=NULL)	/* this should always be the case! */
              {
                  trace (TRACE_DEBUG,"insert_messages(): "
                          "checking for domain aliases. Domain = [%s]",domain);

                  /* checking for domain aliases */
                  auth_check_user(domain,&userids,-1);
                  trace (TRACE_DEBUG,"insert_messages(): "
                          "domain [%s] found total of [%d] aliases",domain,
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
      }
      else
      {
          /* fetch the userid as a numeric string from the dbase */
          userid = auth_user_exists((char*)tmp->data);
          if (userid == -1)
          {
              trace(TRACE_ERROR,"insert_messages(): dbase error checking user [%s]", (char*)tmp->data);
          }
          else if (userid == 0)
          {
              trace(TRACE_ERROR,"insert_messages(): user [%s] does not exist", (char*)tmp->data);
          }
          else
          {
              snprintf(userid_string, MAX_U64_STRINGSIZE, "%llu", userid);
              if (list_nodeadd(&userids, userid_string, strlen(userid_string)+1) == 0)
                  trace(TRACE_FATAL, "insert_messages(): out of memory");

              trace(TRACE_DEBUG, "insert_messages(): added user [%s] id [%s] to delivery list",
                      (char*)tmp->data, userid_string);
          }
      }

      /* get the next taget in list */
      tmp=tmp->nextnode;
  }

  /* get first target userid */
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
	   * we need to do an email address validity test if the first char !| */
	  trace (TRACE_DEBUG,"insert_messages(): no numeric value in deliver_to, "
		 "calling external_forward");

	  /* creating a list of external forward addresses */
	  list_nodeadd(&external_forwards,(char *)tmp->data,strlen(tmp->data)+1);
	}
      else
      {
          /* make the id numeric */
          userid = strtoull((char *)tmp->data, NULL, 10);

	  create_unique_id(unique_id,0); 

          /* create a message record */
          temp_message_record_id = db_insert_message ((u64_t)userid,
						    deliver_to_mailbox, unique_id);

          /* message id is an array of returned message id's
           * all messageblks are inserted for each message id
           * we could change this in the future for efficiency
           * still we would need a way of checking which messageblks
           * belong to which messages */

          if (db_insert_message_block(header, headersize, temp_message_record_id) == -1)
              trace(TRACE_STOP, "insert_messages(): error inserting msgblock [header]\n");

          /* adding this messageid to the message id list */
          list_nodeadd(&messageids,&temp_message_record_id,sizeof(temp_message_record_id));
      }

      /* get next item */	
      tmp=tmp->nextnode;
  }

  trace(TRACE_DEBUG,"insert_messages(): we need to deliver [%ld] "
	"messages to external addresses",
	list_totalnodes(&external_forwards));
	
  
  /* reading rest of the pipe and creating messageblocks 
   * we need to create a messageblk for each messageid */

  trace (TRACE_DEBUG,"insert_messages(): allocating [%d] bytes of memory for readblock",READ_BLOCK_SIZE);

  memtst ((strblock = (char *)my_malloc(READ_BLOCK_SIZE+1))==NULL);
	
	/* first we need to check if we need to deliver into the database */
  if (list_totalnodes(&messageids)>0)
    {
      totalmem = 0; /* reset totalmem counter */

      /* we have local deliveries */ 
      while (!feof(instream))
	{

	  usedmem = fread (strblock, sizeof(char), READ_BLOCK_SIZE, instream);
	  if (ferror(instream))
	    {
	      trace(TRACE_ERROR,"insert_messages(): error on instream: [%s]", strerror(errno));
	    }

	  /* replace all errorneous '\0' by ' ' (space) */
	  for (i=0; i<usedmem; i++)
	    {
	      if (strblock[i] == '\0')
		strblock[i] = ' '; 
	    }


	  /* fread won't do this for us! */	
	  strblock[usedmem]='\0';
			
	  if (usedmem>0) /* usedmem is 0 with an EOF */
	    {
	      totalmem = totalmem + usedmem;
			
	      tmp=list_getstart(&messageids);
	      while (tmp!=NULL)
		{
		  if (db_insert_message_block (strblock, usedmem, *(u64_t *)tmp->data) 
		      == -1)
		    trace(TRACE_STOP, "insert_messages(): error inserting msgblock\n");

		  tmp=tmp->nextnode;
		}
				
	      /* resetting strlen for strblock */
	      strblock[0] = '\0';
	      usedmem = 0;
				
	    }
	  else 
	    trace (TRACE_DEBUG, "insert_messages(): end of instream stream");
		
	
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
	  create_unique_id(unique_id,*(u64_t*)tmp->data); 
	  db_update_message (*(u64_t*)tmp->data,unique_id,totalmem+headersize,0);

	  /* checking size */
	  switch (db_check_sizelimit (totalmem+headersize, *(u64_t*)tmp->data,
				      &bounce_userid))
	    {
	    case 1:
	      trace (TRACE_DEBUG,"insert_messages(): message NOT inserted. Maxmail exceeded");
	      bounce_id = auth_get_userid(&bounce_userid);
	      bounce (header, headersize, bounce_id, BOUNCE_STORAGE_LIMIT_REACHED);
	      my_free (bounce_id);
	      break;

	    case -1:
	      trace (TRACE_ERROR,"insert_messages(): message NOT inserted. dbase error");
	      bounce_id = auth_get_userid(&bounce_userid);
	      bounce (header, headersize, bounce_id, BOUNCE_STORAGE_LIMIT_REACHED);
	      my_free (bounce_id);
	      break;

	    case -2:
	      trace (TRACE_ERROR,"insert_messages(): message NOT inserted. "
		     "Maxmail exceeded AND dbase error");
	      bounce_id = auth_get_userid(&bounce_userid);
	      bounce (header, headersize, bounce_id, BOUNCE_STORAGE_LIMIT_REACHED);
	      my_free (bounce_id);
	      break;

	    case 0:
	      trace (TRACE_MESSAGE,"insert_messages(): message id=%llu, size=%llu is inserted",
		     *(u64_t*)tmp->data, totalmem+headersize);

	      /* message has been succesfully inserted, perform auto-notification & auto-reply */
	      GetConfigValue("AUTO_NOTIFY", &smtpItems, val);
	      if (strcasecmp(val, "yes") == 0)
		do_auto_notify = 1;

	      GetConfigValue("AUTO_REPLY", &smtpItems, val);
	      if (strcasecmp(val, "yes") == 0)
		do_auto_reply = 1;

	      if (do_auto_notify)
		{
		  trace(TRACE_DEBUG, "insert_messages(): starting auto-notification procedure");

		  if (db_get_nofity_address(bounce_userid, &notify_address) != 0)
		    trace(TRACE_ERROR, "insert_messages(): error fetching notification address");
		  else
		    {
		      if (notify_address == NULL)
			trace(TRACE_DEBUG, "insert_messages(): no notification address specified, skipping auto-notify");
		      else
			{
			  trace(TRACE_DEBUG, "insert_messages(): sending notifcation to [%s]", notify_address);
			  send_notification(notify_address, AUTO_NOTIFY_SENDER, AUTO_NOTIFY_SUBJECT);
			  my_free(notify_address);
			}
		    }
		}
	      
	      if (do_auto_reply)
		{
		  trace(TRACE_DEBUG, "insert_messages(): starting auto-reply procedure");

		  if (db_get_reply_body(bounce_userid, &reply_body) != 0)
		    trace(TRACE_ERROR, "insert_messages(): error fetching reply body");
		  else
		    {
		      if (reply_body == NULL || reply_body[0] == '\0')
			trace(TRACE_DEBUG, "insert_messages(): no reply body specified, skipping auto-reply");
		      else
			{
			  send_reply(headerfields, reply_body);
			  my_free(reply_body);
			}
		    }
		}
		  
	      
	      break;
	    }


	  temp_message_record_id=*(u64_t*)tmp->data;
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
          bounce (header, headersize, (char *)tmp->data,BOUNCE_NO_SUCH_USER);
          tmp=tmp->nextnode;	
      }
  }

  /* do we have forward addresses ? */
  if (list_totalnodes(&external_forwards)>0)
    {
      /* sending the message to forwards */
  
      trace (TRACE_DEBUG,"insert_messages(): delivering to external addresses");
  
      ret_path = list_getstart(returnpath);
      
      if (list_totalnodes(&messageids)==0)
	{
	  /* deliver using stdin */
	  pipe_forward (stdin, &external_forwards, ret_path ? ret_path->data : "DBMAIL-MAILER", header, 0);
	}
      else
	{
	  /* deliver using database */
	  tmp = list_getstart(&messageids);
	  pipe_forward (stdin, &external_forwards, ret_path ? ret_path->data: "DBMAIL-MAILER", header, 
			*((u64_t *)tmp->data));
	}
    }
	
  trace (TRACE_DEBUG,"insert_messages(): Freeing memory blocks");

  /* memory cleanup */
  if (tmpbuffer!=NULL)
    {
      trace (TRACE_DEBUG,"insert_messages(): tmpbuffer freed");
      my_free(tmpbuffer);
      tmpbuffer = NULL;
    }
  trace (TRACE_DEBUG,"insert_messages(): header freed");
  my_free(header);

  trace (TRACE_DEBUG,"insert_messages(): uniqueid freed");
  my_free(unique_id);

  trace (TRACE_DEBUG,"insert_messages(): strblock freed");
  my_free (strblock);

  trace (TRACE_DEBUG,"insert_messages(): insertquery freed");
  my_free(insertquery);

  trace (TRACE_DEBUG,"insert_messages(): updatequery freed");
  my_free(updatequery);

  trace (TRACE_DEBUG,"insert_messages(): End of function");
  
  list_freelist(&bounces.start);
  list_freelist(&userids.start);
  list_freelist(&messageids.start);
  list_freelist(&external_forwards.start);
  
  return 0;
}

/*
 * send an automatic notification using sendmai
 */
int send_notification(const char *to, const char *from, const char *subject)
{
  FILE *mailpipe = NULL;
  field_t sendmail;
  int result;

  GetConfigValue("SENDMAIL", &smtpItems, sendmail);
  if (sendmail[0] == '\0')
    trace(TRACE_FATAL, "send_notification(): SENDMAIL not configured (see config file). Stop.");

  trace(TRACE_DEBUG, "send_notification(): found sendmail command to be [%s]", sendmail);

  if (! (mailpipe = popen(sendmail, "w")) )
    {
      trace(TRACE_ERROR, "send_notification(): could not open pipe to sendmail using cmd [%s]", sendmail);
      return 1;
    }

  trace(TRACE_DEBUG, "send_notification(): pipe opened, sending data");

  fprintf(mailpipe, "To: %s\n", to);
  fprintf(mailpipe, "From: %s\n", from);
  fprintf(mailpipe, "Subject: %s\n", subject);
  fprintf(mailpipe, "\n");

  result = pclose(mailpipe);
  trace(TRACE_DEBUG, "send_notification(): pipe closed");

  if (result != 0)
    trace(TRACE_ERROR,"send_notification(): reply could not be sent: sendmail error");

  return 0;
}
  

int send_reply(struct list *headerfields, const char *body)
{
  struct element *el;
  struct mime_record *record;
  char *from = NULL, *to = NULL, *replyto = NULL, *subject = NULL;
  FILE *mailpipe = NULL;
  char comm[MAX_COMM_SIZE];
  field_t sendmail;
  int result;

  GetConfigValue("SENDMAIL", &smtpItems, sendmail);
  if (sendmail[0] == '\0')
    trace(TRACE_FATAL, "send_reply(): SENDMAIL not configured (see config file). Stop.");

  trace(TRACE_DEBUG, "send_reply(): found sendmail command to be [%s]", sendmail);
  
  /* find To: and Reply-To:/From: field */
  el = list_getstart(headerfields);
  
  while (el)
    {
      record = (struct mime_record*)el->data;
      
      if (strcasecmp(record->field, "from") == 0)
	{
	  from = record->value;
	  trace(TRACE_DEBUG, "send_reply(): found FROM [%s]", from);
	}
      else if  (strcasecmp(record->field, "reply-to") == 0)
	{
	  replyto = record->value;
	  trace(TRACE_DEBUG, "send_reply(): found REPLY-TO [%s]", replyto);
	}
      else if  (strcasecmp(record->field, "subject") == 0)
	{
	  subject = record->value;
	  trace(TRACE_DEBUG, "send_reply(): found SUBJECT [%s]", subject);
	}
      else if  (strcasecmp(record->field, "deliver-to") == 0)
	{
	  to = record->value;
	  trace(TRACE_DEBUG, "send_reply(): found TO [%s]", to);
	}

      el = el->nextnode;
    }

  if (!from && !replyto)
    {
      trace(TRACE_ERROR, "send_reply(): no address to send to");
      my_free(sendmail);
      return 0;
    }

  trace(TRACE_DEBUG, "send_reply(): header fields scanned; opening pipe to sendmail");
  snprintf(comm, MAX_COMM_SIZE, "%s %s", sendmail, replyto ? replyto : from);

  if (! (mailpipe = popen(comm, "w")) )
    {
      trace(TRACE_ERROR, "send_reply(): could not open pipe to sendmail using cmd [%s]", comm);
      return 1;
    }

  trace(TRACE_DEBUG, "send_reply(): sending data");
  
  fprintf(mailpipe, "To: %s\n", replyto ? replyto : from);
  fprintf(mailpipe, "From: %s\n", to ? to : "(unknown)");
  fprintf(mailpipe, "Subject: AW: %s\n", subject ? subject : "<no subject>");
  fprintf(mailpipe, "\n");
  fprintf(mailpipe, "%s\n", body ? body : "--");

  result = pclose(mailpipe);
  trace(TRACE_DEBUG, "send_reply(): pipe closed");
  if (result != 0)
    trace(TRACE_ERROR,"send_reply(): reply could not be sent: sendmail error");

  return 0;
}

  
