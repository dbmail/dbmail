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
 * Functions for reading the pipe from the MTA */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "db.h"
#include "auth.h"
#include "debug.h"
#include "list.h"
#include "bounce.h"
#include "forward.h"
#include "sort.h"
#include "dbmail.h"
#include "pipe.h"
#include "debug.h"
#include "misc.h"
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

/* Must be at least 998 or 1000 by RFC's */
#define MAX_LINE_SIZE 1024

#define DBMAIL_DELIVERY_USERNAME "__@!internal_delivery_user!@__"
#define DBMAIL_TEMPMBOX "INBOX"

extern struct list smtpItems, sysItems;

static int resolve_address(char *address, struct list *userids, struct list *forwards, struct list *bounces);
static int store_message_temp(FILE *instream, char *header, u64_t headersize, u64_t *temp_message_idnr);

/* 
 * Send an automatic notification using sendmail
 */
static int send_notification(const char *to, const char *from, const char *subject)
{
  FILE *mailpipe = NULL;
  char *sendmail_command = NULL;
  field_t sendmail;
  int result;

  GetConfigValue("SENDMAIL", &smtpItems, sendmail);
  if (sendmail[0] == '\0')
    trace(TRACE_FATAL, "send_notification(): SENDMAIL not configured (see config file). Stop.");

  trace(TRACE_DEBUG, "send_notification(): found sendmail command to be [%s]", sendmail);

  
  sendmail_command = (char *)my_malloc(strlen((char *)(to))+
        strlen(sendmail)+2); /* +2 for extra space and \0 */
  if (!sendmail_command)
    {
      trace(TRACE_ERROR,"send_notification(): out of memory");
      return -1;
    }

  trace (TRACE_DEBUG,"send_notification(): allocated memory for"
         " external command call");
  sprintf (sendmail_command, "%s %s",sendmail, to);

  trace (TRACE_INFO,"send_notification(): opening pipe to command "
          "%s",sendmail_command);


  if (! (mailpipe = popen(sendmail_command, "w")) )
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
  

/*
 * Send an automatic reply using sendmail
 */
static int send_reply(struct list *headerfields, const char *body)
{
  struct element *el;
  struct mime_record *record;
  char *from = NULL, *to = NULL, *replyto = NULL, *subject = NULL;
  FILE *mailpipe = NULL;
  char *send_address;
  char *escaped_send_address;
  char comm[MAX_COMM_SIZE]; /**< command sent to sendmail (needs to escaped) */
  field_t sendmail;
  int result;
  unsigned int i,j;

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
      return 0;
    }

  
  trace(TRACE_DEBUG, "send_reply(): header fields scanned; opening pipe to sendmail");
  send_address = replyto ? replyto : from;
  /* allocate a string twice the size of send_address */
  escaped_send_address = (char*) my_malloc(strlen((send_address) + 1) 
					   * 2 * sizeof(char));
  i = 0;
  j = 0;
  /* get all characters from send_address, and escape every ' */
  while (i < (strlen(send_address) + 1)) {
       if (send_address[i] == '\'')
	    escaped_send_address[j++] = '\\';
       escaped_send_address[j++] = send_address[i++];
  }
  snprintf(comm, MAX_COMM_SIZE, "%s '%s'", sendmail, escaped_send_address);   

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

  
/* Yeah, RAN. That's Reply And Notify ;-) */
static int execute_auto_ran(u64_t useridnr, struct list *headerfields)
{
  field_t val;
  int do_auto_notify = 0, do_auto_reply = 0;
  char *reply_body = NULL;
  char *notify_address = NULL;

  /* message has been succesfully inserted, perform auto-notification & auto-reply */
  GetConfigValue("AUTO_NOTIFY", &smtpItems, val);
  if (strcasecmp(val, "yes") == 0)
      do_auto_notify = 1;

  GetConfigValue("AUTO_REPLY", &smtpItems, val);
  if (strcasecmp(val, "yes") == 0)
     do_auto_reply = 1;

  if (do_auto_notify)
    {
      trace(TRACE_DEBUG, "execute_auto_ran(): starting auto-notification procedure");

      if (db_get_notify_address(useridnr, &notify_address) != 0)
          trace(TRACE_ERROR, "execute_auto_ran(): error fetching notification address");
      else
        {
          if (notify_address == NULL)
              trace(TRACE_DEBUG, "execute_auto_ran(): no notification address specified, skipping");
          else
            {
              trace(TRACE_DEBUG, "execute_auto_ran(): sending notifcation to [%s]", notify_address);
              send_notification(notify_address, AUTO_NOTIFY_SENDER, AUTO_NOTIFY_SUBJECT);
              my_free(notify_address);
            }
        }
    }
        
  if (do_auto_reply)
    {
      trace(TRACE_DEBUG, "execute_auto_ran(): starting auto-reply procedure");

      if (db_get_reply_body(useridnr, &reply_body) != 0)
          trace(TRACE_ERROR, "execute_auto_ran(): error fetching reply body");
      else
        {
          if (reply_body == NULL || reply_body[0] == '\0')
              trace(TRACE_DEBUG, "execute_auto_ran(): no reply body specified, skipping");
          else
            {
              send_reply(headerfields, reply_body);
              my_free(reply_body);
            }
        }
    }

  return 0;
}
            
        
/* Here's the real *meat* of this source file!
 *
 * Function: insert_messages()
 * What we get:
 *   - A pointer to the incoming message stream
 *   - The header of the message 
 *   - A list of destination addresses / useridnr's
 *   - The default mailbox to delivery to
 *
 * What we do:
 *   - Read in the rest of the message
 *   - Store the message to the DBMAIL user
 *   - Process the destination addresses into lists:
 *     - Local useridnr's
 *     - External forwards
 *     - No such user bounces
 *   - Store the local useridnr's
 *     - Run the message through each user's sorting rules
 *     - Potentially alter the delivery:
 *       - Different mailbox
 *       - Bounce
 *       - Reply with vacation message
 *       - Forward to another address
 *     - Check the user's quota before delivering
 *       - Do this *after* their sorting rules, since the
 *         sorting rules might not store the message anyways
 *   - Send out the no such user bounces
 *   - Send out the external forwards
 *   - Delete the temporary message from the database
 */
int insert_messages(FILE *instream, char *header, u64_t headersize,
    struct list *users, struct list *errusers, struct list *returnpath,
    int users_are_usernames UNUSED, char *deliver_to_mailbox, struct list *headerfields)
{
  struct element *tmp, *ret_path;
  struct list userids;
  struct list forwards;
  struct list bounces;
  u64_t tmpmsgidnr, msgsize = 0;
  u64_t useridnr;
  int err;

  /* Initialize several lists */
  list_init(&userids);
  list_init(&forwards);
  list_init(&bounces);

  /* Get the first target address */
  tmp = list_getstart(users);
  while (tmp!=NULL)
  {
      trace(TRACE_ERROR, "insert_messages(): resolving address [%s]", (char *)tmp->data );
      /* Search for where this thing is gonna go */
      resolve_address((char *)tmp->data, &userids, &forwards, &bounces);
      /* Get the next taget in list */
      tmp=tmp->nextnode;
  }

  /* Read in the rest of the stream and store it into a temporary message */
  switch (store_message_temp(instream, header, headersize, &tmpmsgidnr))
  {
    case -1:
      /* ABORT! BAIL OUT! WE'RE IN BIG TROUBLE HERE! */
      trace(TRACE_ERROR, "insert_messages(): failed to store temporary message." );
      return -1;
    default:
      trace(TRACE_DEBUG, "insert_messages(): temporary msgidnr is [%llu]", tmpmsgidnr );
      break;
  }
  
  /* Reverse any existing nodes to prevent "12348765" */
  errusers->start = list_reverse(errusers->start);

  /* Get first target userid */
  tmp = list_getstart(&userids);
  while (tmp != NULL)
    { 
      /* Cast the ID from the list */
      useridnr = *(u64_t *)tmp->data;

      trace(TRACE_ERROR, "%s, %s: calling sort_and_deliver for useridnr [%llu]",
          __FILE__, __FUNCTION__, useridnr);

      /* The error reporting really needs to be more verbose
       * and for goodness sake, no magic numbers! Define these!
       * */
      err = sort_and_deliver(tmpmsgidnr, header, headersize, msgsize, useridnr, deliver_to_mailbox);
      trace(TRACE_ERROR, "insert_messages(): sort_and_deliver returned with code [%d]", err);
      switch (err)
        {
          case 1:
            /* Indicate success */
            list_nodeadd(errusers, &err, sizeof(int));
            break;
          case 0:
            /* Indicate failure */
            list_nodeadd(errusers, &err, sizeof(int));
            break;
          default:
            /* Assume a failure */
            list_nodeadd(errusers, &err, sizeof(int));
            break;
        }
            
      /* Automatic reply and notification */
      execute_auto_ran(useridnr, headerfields);

      /* Get next item */ 
      tmp=tmp->nextnode;
    }

  trace(TRACE_DEBUG,"insert_messages(): we need to bounce [%ld] "
      "messages for non existent users", list_totalnodes(&bounces));
  
  /* handle all bounced messages */
  if (list_totalnodes(&bounces)>0)
    {
      /* bouncing invalid messages */
      tmp=list_getstart(&bounces);
      while (tmp!=NULL)
        { 
          bounce(header, (char *)tmp->data, BOUNCE_NO_SUCH_USER);
          tmp=tmp->nextnode;  
        }
    }

  trace(TRACE_DEBUG,"insert_messages(): we need to deliver [%ld] "
      "messages to external addresses", list_totalnodes(&forwards));
  
  /* do we have forward addresses ? */
  if (list_totalnodes(&forwards)>0)
    {
      /* sending the message to forwards */
  
      trace (TRACE_DEBUG,"insert_messages(): delivering to external addresses");
  
      ret_path = list_getstart(returnpath);
      
      /* deliver using database */
      forward (tmpmsgidnr, &forwards, (ret_path ? ret_path->data : "DBMAIL-MAILER"), header, headersize);
    }

  trace (TRACE_DEBUG,"insert_messages(): deleting temporary message from database");
  /* Contrived use of this function.
   * It was written for imapcommands.c, not general use :-\
   * */
  /* db_set_msgflag(tmpmsgidnr, tmpmailbox, blah[2]=1, IMAPFA_ADD); */

  /* There, just get rid of it already! */
  db_delete_message(tmpmsgidnr);
  
  trace (TRACE_DEBUG,"insert_messages(): Freeing memory blocks");

  /* Don't free what we did not allocate!
  trace (TRACE_DEBUG,"insert_messages(): header freed");
  my_free(header);
  */

  trace (TRACE_DEBUG,"insert_messages(): End of function");
  
  list_freelist(&bounces.start);
  list_freelist(&userids.start);
  list_freelist(&forwards.start);
  
  return 0;
}

/* Automate the grunt work of figuring
 * out where this 'address' really leads!
 * */
static int resolve_address(char *address, struct list *userids, struct list *forwards, struct list *bounces)
{
  u64_t userid;
  int this_user = 0, this_domain = 0;
  char *domain = NULL;

  /* FIXME: This is all wrong, and needs to be updated! */

  /* See if the given is a numeric string from the database */
  switch (auth_user_exists(address, &userid))
    {
      case -1:
      {
        /* An error occurred */
        trace(TRACE_ERROR,"%s, %s: error checking user [%s]",
            __FILE__, __FUNCTION__, address);
        break;
      }
      case 1:
      {
        if (list_nodeadd(userids, &userid, sizeof(u64_t)) == 0)
            trace(TRACE_FATAL, "%s, %s: out of memory",
                __FILE__, __FUNCTION__);
     
        trace(TRACE_DEBUG, "%s, %s: added user [%s] id [%llu] to delivery list",
            __FILE__, __FUNCTION__, address, userid);
        break;
      }
      /* The address needs to be looked up */
      default:
      {
        this_user = auth_check_user_ext(address, userids, forwards, -1);
        trace(TRACE_DEBUG, "%s, %s: user [%s] found total of [%d] aliases",
            __FILE__, __FUNCTION__, address, this_user);
     
        /* No aliases found for this user */
        if (this_user == 0)
          {
            /* I needed to change this because my girlfriend said so
               and she was actually right. Domain forwards are last resorts
               if a delivery cannot be found with an existing address then
               and only then we need to check if there are domain delivery's */
   
            trace(TRACE_INFO,"%s, %s: no users found to deliver to. "
                "Checking for domain forwards",
                __FILE__, __FUNCTION__);  
   
            domain = strchr(address, '@');
     
            /* This should always be the case! */
            if (domain != NULL)
              {
                trace(TRACE_DEBUG, "%s, %s: "
                    "checking for domain aliases. Domain = [%s]",
                    __FILE__, __FUNCTION__, domain);
   
                /* Checking for domain aliases */
                this_domain = auth_check_user_ext(domain, userids, forwards, -1);
                trace(TRACE_DEBUG,"%s, %s: "
                    "domain [%s] found total of [%d] aliases",
                    __FILE__, __FUNCTION__, domain, this_domain);
              }
          }
   
        /* user does not exists in aliases tables
           so bounce this message back with an error message */
        if (this_user == 0 && this_domain == 0)
          {
            /* still no effective deliveries found, create bouncelist */
            list_nodeadd(bounces, address, strlen(address)+1);
          }
      }
    }
  return 0;
}

  
/* Read from insteam until eof, and store to the
 * dedicated dbmail user account. Later, we'll
 * read the message back for forwarding and 
 * sorting for local users before db_copymsg()'ing
 * it into their own mailboxes.
 *
 * returns a message id number, or -1 on error.
 * */
static int store_message_temp(FILE *instream, char *header, u64_t headersize, u64_t *temp_message_idnr)
{
  int myeof=0;
  u64_t msgidnr=0;
  size_t i=0, usedmem=0, totalmem=0, linemem=0;
  char *strblock=NULL, *tmpline=NULL;
  char unique_id[UID_SIZE];
  u64_t messageblk_idnr;
  u64_t user_idnr;
  int result;

  result = auth_user_exists(DBMAIL_DELIVERY_USERNAME, &user_idnr);
  if (result < 0) {
	  trace(TRACE_ERROR, "%s,%s: unable to find user_idnr for user "
		"[%s]\n", __FILE__, __FUNCTION__, DBMAIL_DELIVERY_USERNAME);
	  return -1;
  }
  if (result == 0) {
	  trace(TRACE_ERROR, "%s,%s: unable to find user_idnr for user "
		"[%s]. Make sure this system user is in the database!\n",
		__FILE__, __FUNCTION__, DBMAIL_DELIVERY_USERNAME);
	  return -1;
  }
  
  create_unique_id(unique_id, user_idnr); 

  /* create a message record */
  switch (db_insert_message(user_idnr, DBMAIL_TEMPMBOX, 
			    CREATE_IF_MBOX_NOT_FOUND, unique_id, &msgidnr))
  {
    case -1:
      trace(TRACE_ERROR, "store_message_temp(): returned -1, aborting");
      return -1;
  }

  switch (db_insert_message_block(header, headersize, msgidnr, &messageblk_idnr))
  {
    case -1:
      trace(TRACE_ERROR, "store_message_temp(): error inserting msgblock [header]");
      return -1;
  }

  trace (TRACE_DEBUG,"store_message_temp(): allocating [%d] bytes of memory for readblock", READ_BLOCK_SIZE);

  memtst ((strblock = (char *)my_malloc(READ_BLOCK_SIZE+1))==NULL);
  memtst ((tmpline= (char *)my_malloc(MAX_LINE_SIZE+1))==NULL);
  
  while ((!feof(instream) && (!myeof)) || (linemem != 0))
    {
      /* Copy the line that didn't fit before */
      if (linemem > 0)
        {
          strncpy(strblock, tmpline, linemem);
	  usedmem += linemem;

          /* Resetting strlen for tmpline */
          tmpline[0] = '\0';
          linemem = 0;
        }

      /* We want to fill up each block if possible,
       * unless of course we're at the end of the file */
      while (!feof(instream) && (usedmem + linemem < READ_BLOCK_SIZE))
        {
          fgets(tmpline, MAX_LINE_SIZE, instream);
          linemem = strlen(tmpline);

          if (ferror(instream))
            {
              trace(TRACE_ERROR,"store_message_temp(): error on instream: [%s]", strerror(errno));
              /* FIXME: Umm, don't we need to free a few things?! */
              return -1;
            }
     
          /* This should be the one and only valid
           * end to a message over SMTP/LMTP...
           * FIXME: If there's a compatibility problem, it's probably here! */
          if (strcmp(tmpline, ".\r\n") == 0)
            {
              /* This is the end of the message! */
              myeof = 1;
	      /* make sure that the function stops by setting linemem to zero
	       */
	      linemem = 0;
              break;
            }
          else
            {
              /* See if the line fits into this block */
              if (usedmem + linemem < READ_BLOCK_SIZE)
                {
                  strncpy(strblock+usedmem, tmpline, linemem);
                  usedmem += linemem;

                  /* Resetting strlen for tmpline */
                  tmpline[0] = '\0';
                  linemem = 0;
                }

              /* Don't need an else, see above this while loop for more */
            }
        }

      /* replace all errorneous '\0' by ' ' (space) */
      for (i = 0; i < usedmem; i++)
        {
          if (strblock[i] == '\0')
            {
              strblock[i] = ' '; 
            }
        }

      /* fread won't do this for us! */ 
      strblock[usedmem] = '\0';
        
      if (usedmem > 0) /* usedmem is 0 with an EOF */
        {
          totalmem += usedmem;
        
          switch (db_insert_message_block(strblock, usedmem, msgidnr, &messageblk_idnr))
          {
            case -1:
              trace(TRACE_STOP, "store_message_temp(): error inserting msgblock");
              return -1;
          }
        }

      /* resetting strlen for strblock */
      strblock[0] = '\0';
      usedmem = 0;
    }

  trace (TRACE_DEBUG, "store_message_temp(): end of instream");
/* This shouldn't be freed because it is a local array, not a pointer...
  trace (TRACE_DEBUG, "store_message_temp(): uniqueid freed");
  my_free(unique_id);
  */

  trace (TRACE_DEBUG, "store_message_temp(): tmpline freed");
  my_free (tmpline);

  trace (TRACE_DEBUG, "store_message_temp(): strblock freed");
  my_free (strblock);

  db_update_message(msgidnr, unique_id, totalmem+headersize, 0);

  /* Pass the message id out to the caller. */
  *temp_message_idnr = msgidnr;

  return 0;
}

