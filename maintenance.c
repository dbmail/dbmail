/* $Id$
 * (c) 2000-2001 IC&S, The Netherlands
 *
 * This is the dbmail housekeeping program. 
 *	It checks the integrity of the database and does a cleanup of all
 *	deleted messages. */

#include "maintenance.h"
#include "dbmysql.h"
#include "debug.h"
#include "unistd.h"

int main(int argc, char *argv[])
{
  int should_fix = 0,opt;
  int nlost,i;
  unsigned long *lostlist;

  unsigned long deleted_messages;
  unsigned long messages_set_to_delete;
    
  openlog(PNAME, LOG_PID, LOG_MAIL);
	
  setvbuf(stdout,0,_IONBF,0);
  printf ("*** dbmail-maintenance ***\n");
	
  /* get options */
  opterr = 0; /* suppress error message from getopt() */
  while ((opt = getopt(argc, argv, "f")) != -1)
    {
      switch (opt)
	{
	case 'f':
	  should_fix = 1;
	  break;

	default:
	  /*printf("unrecognized option [%c], continuing...\n",optopt);*/
	}
    }

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

  /* first part */
  if (db_icheck_messageblks(&nlost, &lostlist) < 0)
    {
      printf ("Failed. An error occured. Please check log.\n");
      return -1;
    }
    
  if (nlost > 0)
    {
      printf ("Ok. Found [%d] unconnected messageblks:\n", nlost);
      
      for (i=0; i<nlost; i++)
	{
	  if (should_fix == 0)
	    printf("%lu ", lostlist[i]);
	  else
	    {
	      if (db_delete_messageblk(lostlist[i]) < 0)
		printf("Warning: could not delete messageblock #%lu. Check log.\n",lostlist[i]);
	      else
		printf("%lu (removed from dbase)\n",lostlist[i]);
	    }
	}
        
      printf ("\n");
      if (should_fix == 0)
	{
	  printf("Try running dbmail-maintenance with the '-f' option "
		 "in order to fix these problems\n\n");
	}
    }
  else 
    printf ("Ok. Found 0 unconnected messageblks.\n");

  my_free(lostlist);


  /* second part */
  if (db_icheck_messages(&nlost, &lostlist) < 0)
    {
      printf ("Failed. An error occured. Please check log.\n");
      return -1;
    }
    
  if (nlost > 0)
    {
      printf ("Ok. Found [%d] unconnected messages:\n", nlost);
      
      for (i=0; i<nlost; i++)
	{
	  if (should_fix == 0)
	    printf("%lu ", lostlist[i]);
	  else
	    {
	      if (db_delete_message(lostlist[i]) < 0)
		printf("Warning: could not delete message #%lu. Check log.\n",lostlist[i]);
	      else
		printf("%lu (removed from dbase)\n",lostlist[i]);
	    }
	}
        
      printf ("\n");
      if (should_fix == 0)
	{
	  printf("Try running dbmail-maintenance with the '-f' option "
		 "in order to fix these problems\n\n");
	}
    }
  else 
    printf ("Ok. Found 0 unconnected messages.\n");
        
  my_free(lostlist);


  /* third part */
  if (db_icheck_mailboxes(&nlost, &lostlist) < 0)
    {
      printf ("Failed. An error occured. Please check log.\n");
      return -1;
    }
    
  if (nlost > 0)
    {
      printf ("Ok. Found [%d] unconnected mailboxes:\n", nlost);
      
      for (i=0; i<nlost; i++)
	{
	  if (should_fix == 0)
	    printf("%lu ", lostlist[i]);
	  else
	    {
	      if (db_delete_mailbox(lostlist[i]) < 0)
		printf("Warning: could not delete mailbox #%lu. Check log.\n",lostlist[i]);
	      else
		printf("%lu (removed from dbase)\n",lostlist[i]);
	    }
	}
        
      printf ("\n");
      if (should_fix == 0)
	{
	  printf("Try running dbmail-maintenance with the '-f' option "
		 "in order to fix these problems\n\n");
	}
    }
  else 
    printf ("Ok. Found 0 unconnected mailboxes.\n");
        
  my_free(lostlist);



  printf ("Maintenance done.\n");
        
  return 0;
}

