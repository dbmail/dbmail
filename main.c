/* $Id$
 * main file  */

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
unsigned long headersize;

int main (int argc, char *argv[]) {


  openlog(PNAME, LOG_PID, LOG_MAIL);

  /* first check for commandline options */
  if (argc<2)
    {
      printf ("\nUsage: %s -n [headerfield]   for normal deliveries (default: \"Deliver-To:\" header)\n",argv[0]);
      printf ("       %s -d [addresses]  for delivery without using scanner\n\n",argv[0]);
      return 0;
    }
	
  if (db_connect() < 0) 
    trace(TRACE_FATAL,"main(): database connection failed");

  /* first we need to read the header */
  if ((header=read_header(&headersize))==NULL)
    trace (TRACE_STOP,"main(): read_header() returned an invalid header");
	
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
			if (!mail_adr_list (argv[INDEX_DELIVERY_MODE+1],&users,&mimelist,&users,header,headersize))
				trace (TRACE_STOP,"main(): scanner found no email addresses (scanned for %s)", argv[INDEX_DELIVERY_MODE+1]);
		}
		else
			if (!mail_adr_list ("deliver-to",&users,&mimelist,&users,header,headersize))	
				trace(TRACE_STOP,"main(): scanner found no email addresses (scanned for Deliver-To:)");
    } 

  /* inserting messages into the database */
  insert_messages(header, headersize,&users);
	trace(TRACE_DEBUG,"main(): freeing memory blocks");
  free(header); /* cleanup the header */
  trace (TRACE_DEBUG,"main(): they're all free. we're done.");
  return 0;
}
