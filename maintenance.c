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
    unsigned long unlinked_messageblks;
    
    struct list failed_messageblks;
    struct element *e;
    
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

    printf ("Now checking DBMAIL messageblocks integrity.. ");

    /* this is what we do:
     * First we're checking for loose messageblocks
     * Secondly we're chekcing for loose messages
     * Third we're checking for loose mailboxes 
     */

    list_init (&failed_messageblks);
    
    if (db_icheck_messageblks (&failed_messageblks)==-1)
    {
        printf ("Failed. An error occured. Please check log.\n");
        return -1;
    }
    
    unlinked_messageblks = list_totalnodes (&failed_messageblks);
    
    if (unlinked_messageblks>0)
    {
    printf ("Ok. Found [%lu] unconnected messageblks:\n",unlinked_messageblks);
        e = list_getstart (&failed_messageblks);
        
        while (e)
        {
            printf ("%s", (char *)e->data);
            e = e->nextnode;
        }
        
        printf ("\n");
    }
    else 
        printf ("Ok. Found 0 unconnected messageblks.\n");
        
printf ("Maintenance done.\n");
        
return 0;
}
