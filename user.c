/* $Id$
 * This is the dbmail-user program
 * It makes adding users easier */

#include "user.h"

int main(int argc, char *argv[])
{
	unsigned long useridnr;
	int i;
	
	openlog(PNAME, LOG_PID, LOG_MAIL);
	
	setvbuf(stdout,0,_IONBF,0);
	
	printf ("\n*** dbmail-adduser ***\n");
	
	if (argc<5)
	{
		printf ("Usage:     %s username password clientid maxmail [aliases]\n",argv[0]);
		printf ("           maxmail is in bytes (25000000 = 25 Mb mailquota), 0 is unlimited\n");
		printf ("Example:   %s foo secret 0 0 foo@bar.net foo@bar.org\n\n",argv[0]);
		return 0;
	}

	printf ("Opening connection to the database... ");

	if (db_connect()==-1)
	{
		printf ("Failed. Could not connect to database (check log)\n");
		return -1;
	}
	
	printf ("Ok. Connected\n");
	
	printf ("Adding user %s with password %s, %s bytes mailbox limit and clientid %s...",
			argv[1], argv[2], argv[3], argv[4]);

	useridnr = db_adduser (argv[1],argv[2],argv[3],argv[4]);
	
	if (useridnr == -1)
	{
		printf ("Failed\n\nCheck logs for details\n\n");
		return -1;
	}
	
	printf ("Ok, user added id [%d]\n",useridnr);
	
	for (i = 5; i<argc; i++)
	{
		printf ("Adding alias %s...",argv[i]);
		if (db_addalias(useridnr,argv[i])==-1)
			printf ("Failed\n");
		else
			printf ("Ok, added\n");
	}
		
	printf ("adduser done\n");

	return 0;
}
