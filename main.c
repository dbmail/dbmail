/* $Id$
 * (c) 2000-2001 IC&S, The Netherlands
 * 
 * main file for dbmail-smtp  */

#include "config.h"
#include "main.h"
#include "pipe.h"

#define MESSAGEIDSIZE 100
#define NORMAL_DELIVERY 1
#define SPECIAL_DELIVERY 2

#define INDEX_DELIVERY_MODE 1

/* syslog */
#define PNAME "dbmail/smtp"

struct list mimelist; 	/* raw unformatted mimefields and values */
struct list users; 	  	/* list of email addresses in message */

int mode;					/* how should we process */
  
char *header;
char *trace_level, *trace_syslog, *trace_verbose;
int new_level = 2, new_trace_syslog = 1, new_trace_verbose = 0;
unsigned long headersize;

int main (int argc, char *argv[]) {

struct list returnpath; /* returnpath (should aways be just 1 hop */

  openlog(PNAME, LOG_PID, LOG_MAIL);

  /* first check for commandline options */
  if (argc<2)
    {
      printf ("\nUsage: %s -n [headerfield]   for normal deliveries (default: \"deliver-to\" header)\n",argv[0]);
      printf ("       %s -d [addresses]  for delivery without using scanner\n\n",argv[0]);
      return 0;
    }
	
  if (db_connect() < 0) 
    trace(TRACE_FATAL,"main(): database connection failed");

  /* reading settings */
  
  trace_level = db_get_config_item("TRACE_LEVEL", CONFIG_EMPTY);
  trace_syslog = db_get_config_item("TRACE_TO_SYSLOG", CONFIG_EMPTY);
  trace_verbose = db_get_config_item("TRACE_VERBOSE", CONFIG_EMPTY);

  if (trace_level)
    {
      new_level = atoi(trace_level);
      my_free(trace_level);
      trace_level = NULL;
    }

  if (trace_syslog)
    {
      new_trace_syslog = atoi(trace_syslog);
      my_free(trace_syslog);
      trace_syslog = NULL;
    }

  if (trace_verbose)
    {
      new_trace_verbose = atoi(trace_verbose);
      my_free(trace_verbose);
      trace_verbose = NULL;
    }

  configure_debug(new_level, new_trace_syslog, new_trace_verbose);
 

  /* first we need to read the header */
  if ((header=read_header(&headersize))==NULL)
    trace (TRACE_STOP,"main(): read_header() returned an invalid header");


  list_init(&returnpath);
  /* parse returnpath from header */
  mail_adr_list ("Return-Path",&returnpath,&mimelist,&returnpath,header,headersize);
  
  /* we need to decide what delivery mode we're in */
  if (strcmp ("-d",argv[INDEX_DELIVERY_MODE])==0)
    {
      trace (TRACE_INFO,"main(): using SPECIAL_DELIVERY");
      /* mail_adr_list_special will take the command line 
       * email addresses and use those addresses for this message 
       * delivery */
      if (mail_adr_list_special(INDEX_DELIVERY_MODE+1,argc, argv,&users)==0)
	trace(TRACE_STOP,"main(): could not find any addresses");
    }
  else 
    {
      trace (TRACE_INFO,"main(): using NORMAL_DELIVERY");
      /* parse the list and scan for field and content */
      if (mime_list(header, &mimelist) == -1)
	trace(TRACE_STOP,"main(): fatal error creating MIME-header list\n");

      /* parse for destination addresses */
      if (argc>2) 
	{
	  trace (TRACE_DEBUG, "main(): scanning for [%s]",argv[INDEX_DELIVERY_MODE+1]);
	  if (mail_adr_list (argv[INDEX_DELIVERY_MODE+1],&users,&mimelist,&users,header,headersize) != 0)
	    trace (TRACE_STOP,"main(): scanner found no email addresses (scanned for %s)", argv[INDEX_DELIVERY_MODE+1]);
	}
      else
	if (!mail_adr_list ("deliver-to",&users,&mimelist,&users,header,headersize) != 0)	
	  trace(TRACE_STOP,"main(): scanner found no email addresses (scanned for Deliver-To:)");
    } 

  /* inserting messages into the database */
  insert_messages(header, headersize,&users, &returnpath);
	trace(TRACE_DEBUG,"main(): freeing memory blocks");
  trace (TRACE_DEBUG,"main(): they're all free. we're done.");
  return 0;
}
