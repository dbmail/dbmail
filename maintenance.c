/* $Id$
 * (c) 2000-2001 IC&S, The Netherlands
 *
 * This is the dbmail housekeeping program. 
 *	It checks the integrity of the database and does a cleanup of all
 *	deleted messages. */

#include "maintenance.h"
#include "dbmysql.h"
#include "debug.h"
#include <unistd.h>
#include <time.h>

#define LEN 30

void find_time(char *timestr, const char *timespec);



int main(int argc, char *argv[])
{
  int should_fix = 0, check_integrity = 0, check_iplog = 0;
  int show_help=0, purge_deleted=0, set_deleted=0;
  int do_nothing=1;

  char timespec[LEN],timestr[LEN];
  char *trace_level,*trace_syslog,*trace_verbose;
  int new_level = 2, new_trace_syslog = 1, new_trace_verbose = 0;

  int opt;
  int nlost,i;
  unsigned long *lostlist;

  unsigned long deleted_messages;
  unsigned long messages_set_to_delete;
    
  openlog(PNAME, LOG_PID, LOG_MAIL);
	
  setvbuf(stdout,0,_IONBF,0);
  printf ("*** dbmail-maintenance ***\n");
	
  /* get options */
  opterr = 0; /* suppress error message from getopt() */
  while ((opt = getopt(argc, argv, "fil:phd")) != -1)
    {
      switch (opt)
	{
	case 'h':
	  show_help = 1;
	  do_nothing = 0;
	  break;

	case 'p':
	  purge_deleted = 1;
	  do_nothing = 0;
	  break;

	case 'd':
	  set_deleted = 1;
	  do_nothing = 0;
	  break;

	case 'f':
	  check_integrity = 1;
	  should_fix = 1;
	  do_nothing = 0;
	  break;

	case 'i':
	  check_integrity = 1;
	  do_nothing = 0;
	  break;

	case 'l':
	  check_iplog = 1;
	  do_nothing = 0;
	  if (optarg)
	    strncpy(timespec, optarg, LEN);
	  else
	    timespec[0] = 0;

	  timespec[LEN] = 0;
	  break;

	default:
	  /*printf("unrecognized option [%c], continuing...\n",optopt);*/
	}
    }

  if (show_help)
    {
      printf("\ndbmail maintenance utility\n\n");
      printf("Performs maintenance tasks on the dbmail-databases\n");
      printf("Use: dbmail-maintenance -[fiphdl]\n");
      printf("See the man page for more info\n\n");
      return 0;
    }


  if (do_nothing)
    {
      printf("Ok. Nothing requested, nothing done. "
	     "Try adding a command-line option to perform maintenance.\n");
      return 0;
    }

  printf ("Opening connection to the database... ");

  if (db_connect()==-1)
    {
      printf ("Failed. An error occured. Please check log.\n");
      return -1;
    }

  printf ("Ok. Connected\n");

	
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

  if (purge_deleted)
    {
      printf ("Deleting messages with DELETE status... ");
      deleted_messages=db_deleted_purge();
      if (deleted_messages==-1)
	{
	  printf ("Failed. An error occured. Please check log.\n");
	  db_disconnect();
	  return -1;
	}
      printf ("Ok. [%lu] messages deleted.\n",deleted_messages);
    }
	

  if (set_deleted)
    {
      printf ("Setting DELETE status for deleted messages... ");
      messages_set_to_delete= db_set_deleted ();
      if (messages_set_to_delete==-1)
	{
	  printf ("Failed. An error occured. Please check log.\n");
	  db_disconnect();
	  return -1;
	}
      printf ("Ok. [%lu] messages set for deletion.\n",messages_set_to_delete);
    }


  if (check_integrity)
    {
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
	  db_disconnect();
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
      printf ("Now checking DBMAIL message integrity.. ");

      if (db_icheck_messages(&nlost, &lostlist) < 0)
	{
	  printf ("Failed. An error occured. Please check log.\n");
	  db_disconnect();
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
      printf ("Now checking DBMAIL mailbox integrity.. ");

      if (db_icheck_mailboxes(&nlost, &lostlist) < 0)
	{
	  printf ("Failed. An error occured. Please check log.\n");
	  db_disconnect();
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
    }

  if (check_iplog)
    {
      find_time(timestr, timespec);
      printf("Cleaning up IP log... ");

      if (timestr[0] == 0)
	{
	  printf("Failed. Invalid argument [%s] specified\n",timespec);
	  db_disconnect();
	  return -1;
	}

      if (db_cleanup_iplog(timestr) < 0)
	{
	  printf("Failed. Please check the log.\n");
	  db_disconnect();
	  return -1;
	}
      
      printf("Ok. All entries before [%s] have been removed.\n",timestr);
    }
  

  printf ("Maintenance done.\n\n");
        
  db_disconnect();
  return 0;
}


/* 
 * makes a date/time string: YYYY-MM-DD HH:mm:ss
 * based on current time minus timespec
 * timespec contains: <n>h<m>m for a timespan of n hours, m minutes
 * hours or minutes may be absent, not both
 *
 * upon error, timestr[0] = 0
 */
void find_time(char *timestr, const char *timespec)
{
  time_t td;
  struct tm tm;
  int min=-1,hour=-1;
  long tmp;
  char *end;

  time(&td);              /* get time */
 
  timestr[0] = 0;
  if (!timespec)
    return;

  /* find first num */
  tmp = strtol(timespec, &end, 10);
  if (!end)
    return;

  if (tmp < 0)
    return;

  switch (*end)
    {
    case 'h':
    case 'H':
      hour = tmp;
      break;
      
    case 'm':
    case 'M':
      hour = 0;
      min = tmp;
      if (end[1]) /* should end here */
	return;

      break;

    default:
      return;
    }


  /* find second num */
  if (timespec[end-timespec+1])
    {
      tmp = strtol(&timespec[end-timespec+1], &end, 10);
      if (end)
	{
	  if ((*end != 'm' && *end != 'M') || end[1])
	    return;

	  if (tmp < 0)
	    return;

	  if (min >= 0) /* already specified minutes */
	    return;

	  min = tmp;
	}
    }

  if (min < 0) 
    min = 0;

  /* adjust time */
  td -= (hour * 3600L + min * 60L);
  
  tm = *localtime(&td);   /* get components */
  strftime(timestr, LEN, "%G-%m-%d %H:%M:%S", &tm);

  return;
}





