/* $Id$
 * main file  */

#include "config.h"
#include "main.h"

#define MESSAGEIDSIZE 100
#define NORMAL_DELIVERY 1
#define SPECIAL_DELIVERY 2

#define INDEX_DELIVERY_MODE 1

/* syslog */
#define PNAME "dbmail/smtp"

struct list mimelist; 	/* raw unformatted mimefields and values */
struct list users; 	  	/* list of email addresses in message */

int mode;					/* how should we process */

int main (int argc, char *argv[]) {

	char *header;
	unsigned long headersize;

	openlog(PNAME, LOG_PID, LOG_MAIL);

	/* first check for commandline options */
	if (argc<2)
		{
			printf ("\nUsage: %s -n              for normal deliveries (scanner scans Deliver-To: header)\n",argv[0]);
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
		if (mail_adr_list_special(INDEX_DELIVERY_MODE+1,argc, argv)==0)
			trace(TRACE_STOP,"main(): could not find any addresses");
		}
	else 
		{
		trace (TRACE_INFO,"main(): using NORMAL_DELIVERY");
		/* parse the list and scan for field and content */
		mime_list(header,headersize);
		/* parse for destination addresses */
		
		if (!mail_adr_list())
				trace(TRACE_STOP,"main(): scanner found no email addresses");
		} 

 	/* inserting messages into the database */
	insert_messages(header, headersize);

	free(header); /* cleanup the header */
	return 0;
}
