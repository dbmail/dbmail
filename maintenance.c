/* $Id$
 * (c) 2000-2001 IC&S, The Netherlands
 *
 * This is the dbmail housekeeping program. 
 *	It checks the integrity of the database and does a cleanup of all
 *	deleted messages. */

#include "maintenance.h"
#include "dbmysql.h"

int main()
{
	unsigned long deleted_messages;
	unsigned long messages_set_to_delete;

	openlog(PNAME, LOG_PID, LOG_MAIL);
	
	setvbuf(stdout,0,_IONBF,0);
	printf ("*** dbmail-maintenance ***\n");
	
	printf ("Opening connection to the database... ");

	if (db_connect()==-1)
	{
		printf ("Failed. An error occured. Please check log.\n");
		return -1;
	}
	
	printf ("Ok. Connected\n");

	printf ("Deleting messages with DELETE status... ");
	deleted_messages=db_deleted_purge();
	if (deleted_messages==-1)
	{
		printf ("Failed. An error occured. Please check log.\n");
		return -1;
	}
	printf ("Ok. [%lu] messages deleted.\n",deleted_messages);
	
	printf ("Setting DELETE status for deleted messages... ");
	messages_set_to_delete= db_set_deleted ();
	if (messages_set_to_delete==-1)
	{
		printf ("Failed. An error occured. Please check log.\n");
		return -1;
	}
	printf ("Ok. [%lu] messages set for deletion.\n",messages_set_to_delete);

	printf ("Maintenance done.\n");
	
	return 0;
}
