/* $Id$
 * (c) 2000-2002 IC&S, The Netherlands
 * 
 * main file for dbmail-smtp  */

#include "config.h"
#include "main.h"
#include "pipe.h"
#include "list.h"
#include "auth.h"
#include <string.h>
#include <stdlib.h>

#define MESSAGEIDSIZE 100
#define NORMAL_DELIVERY 1
#define SPECIAL_DELIVERY 2

#define INDEX_DELIVERY_MODE 1

/* syslog */
#define PNAME "dbmail/smtp"

struct list mimelist; 	        /* raw unformatted mimefields and values */
struct list users; 	  	/* list of email addresses in message */

struct list sysItems, smtpItems; /* config item lists */

char *configFile = "dbmail.conf";

/* set up database login data */
extern field_t _db_host;
extern field_t _db_db;
extern field_t _db_user;
extern field_t _db_pass;


int mode;			/* how should we process */
  
char *header = NULL;
char *deliver_to_mailbox = NULL;
u64_t headersize;

int main (int argc, char *argv[]) 
{
  struct list returnpath; /* returnpath (should aways be just 1 hop */
  int users_are_usernames = 0,i;
  u64_t dummyidx=0,dummysize=0;

  openlog(PNAME, LOG_PID, LOG_MAIL);

  /* first check for commandline options */
  if (argc<2)
    {
      printf ("\nUsage: %s -n [headerfield]   for normal deliveries "
	      "(default: \"deliver-to\" header)\n",argv[0]);
      printf ("       %s -m \"mailbox\" -u [username] for delivery to mailbox (name)\n"
              ,argv[0]);
      printf ("       %s -d [addresses]  for delivery without using scanner\n",argv[0]);
      printf ("       %s -u [usernames]  for direct delivery to users\n\n",argv[0]);
      return 0;
    }

  ReadConfig("DBMAIL", configFile, &sysItems);
  ReadConfig("SMTP", configFile, &smtpItems);
  SetTraceLevel(&smtpItems);
  GetDBParams(_db_host, _db_db, _db_user, _db_pass, &sysItems);

  if (db_connect() < 0) 
    trace(TRACE_FATAL,"main(): database connection failed");

  list_init(&users);
  list_init(&mimelist);

  /* first we need to read the header */
  if ((header=read_header(&headersize))==NULL)
    trace (TRACE_STOP,"main(): read_header() returned an invalid header");


  /* parse the list and scan for field and content */
  if (mime_readheader(header, &dummyidx, &mimelist, &dummysize) < 0)
    trace(TRACE_STOP,"main(): fatal error creating MIME-header list\n");

  list_init(&returnpath);

  /* parse returnpath from header */
  mail_adr_list ("Return-Path",&returnpath,&mimelist);
  if (returnpath.total_nodes == 0)
    mail_adr_list("From",&returnpath,&mimelist);

  
  /* we need to decide what delivery mode we're in */
  if (strcmp ("-d", argv[INDEX_DELIVERY_MODE])==0)
    {
      trace (TRACE_INFO,"main(): using SPECIAL_DELIVERY to email addresses");

      /* mail_adr_list_special will take the command line 
       * email addresses and use those addresses for this message 
       * delivery */
      
      if (mail_adr_list_special(INDEX_DELIVERY_MODE+1,argc, argv,&users)==0)
	trace(TRACE_STOP,"main(): could not find any addresses");
    }
  else if ( (strcmp (argv[INDEX_DELIVERY_MODE],"-m")==0) )
  {
    if (argc>4)
      {
	if (strcmp (argv[3],"-u")!=0)
	  {
	    printf ("\nError: When using the mailbox delivery option,"
		    " you should specify a username\n\n");
	    return 0;
          }
      }
    else
      {
	printf ("\nError: Mailbox delivery needs a -u clause to specify"
		" a user that should be used for delivery\n\n");
	return 0;
      }

    trace (TRACE_INFO,"main(): using SPECIAL_DELIVERY to mailbox");
    
    if (list_nodeadd(&users, argv[4], strlen(argv[4])+1) == 0)
      {
	trace (TRACE_STOP, "main(): out of memory");
	return 1;
      }

    users_are_usernames = 1;
    deliver_to_mailbox = argv[2];
    
  }
  else if (strcmp("-u", argv[INDEX_DELIVERY_MODE])==0)
    {
      trace (TRACE_INFO,"main(): using SPECIAL_DELIVERY to usernames");

      /* build a list of usernames as supplied on the command line */
      for (i=INDEX_DELIVERY_MODE+1; argv[i]; i++)
	{
          if (list_nodeadd(&users, argv[i], strlen(argv[i]) + 1) == 0)
	    {
              trace(TRACE_STOP, "main(): out of memory");
              return 1;
	    }
	}

      users_are_usernames = 1;
    }
  else
    {
      trace (TRACE_INFO,"main(): using NORMAL_DELIVERY");

      /* parse for destination addresses */
      if (argc>2) 
	{
	  trace (TRACE_DEBUG, "main(): scanning for [%s]",argv[INDEX_DELIVERY_MODE+1]);
	  if (mail_adr_list(argv[INDEX_DELIVERY_MODE+1],&users,&mimelist) !=0)
	    trace (TRACE_STOP,"main(): scanner found no email addresses (scanned for %s)", 
		   argv[INDEX_DELIVERY_MODE+1]);
	}
      else
	if (mail_adr_list ("deliver-to",&users,&mimelist) != 0)
	  trace(TRACE_STOP,"main(): scanner found no email addresses (scanned for Deliver-To:)");
    } 
  
  /* inserting messages into the database */
  insert_messages(header, headersize, &users, &returnpath, users_are_usernames,
		  deliver_to_mailbox, &mimelist);
  trace(TRACE_DEBUG,"main(): freeing memory blocks");
  
  trace (TRACE_DEBUG,"main(): they're all free. we're done.");
  
  db_disconnect();

  return 0;
}
