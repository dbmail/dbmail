#include "db.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "debug.h"
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "/usr/local/pgsql/include/libpq-fe.h"

#define PNAME "dbmail/insert-test"

extern PGconn *conn;  
extern PGresult *res;

char blk[READ_BLOCK_SIZE];
char header[] = "Test: test\n\n";
char uniqueid[70];

int main()
{
  u64_t msgid;
  int i,j;
  time_t start,stop,cost;

/*
  NB this test showed that the dbase connection is closed after child termination 

  while (1)
    {
      if (!fork())
	{
	  if (db_connect() != 0)
	    printf("could not connect\n");
	  exit(0);
	}
      else
	{
	  usleep(100);
	  while (waitpid(-1, NULL, WNOHANG|WUNTRACED) > 0) ;
	}
    }
*/

  openlog(PNAME, LOG_PID, LOG_MAIL);   /* open connection to syslog */
  configure_debug(TRACE_DEBUG, 1, 0);

  memset(blk, 'x', READ_BLOCK_SIZE);
  blk[READ_BLOCK_SIZE-1] = 0;

  if (db_connect() != 0)
    {
      printf("connection error");
      return -1;
    }

  time(&start);
  printf("Timing connection cost..\n");

  for (i=0; i<20; i++)
    {
      if (fork() == 0)
	{
	  for (j=0; j<2; j++)
	    {
	      db_connect();
	      db_disconnect();
	    }
	  return 0;
	}
    }

  for (i=0; i<20; i++)
    wait(NULL);
  
  time(&stop);
  cost = stop-start;

  time(&start);

  for (i=0; i<20; i++)
    if (fork() == 0)
      return 0;

  for (i=0; i<20; i++)
    wait(NULL);
  
  time(&stop);
  
  printf("building processes took %d seconds\n",(stop-start));
  printf("building connections took %d seconds\n", cost - (stop-start));
  db_disconnect();
  return 0;
  
  time (&start); /* mark the starting time */

  for (i=0; i<2; i++)
    {
      printf("inserting %d...", i);
      msgid = db_insert_message(1, 0, 0);
      db_insert_message_block(header, strlen(header), msgid);
      printf("[%s]\n",blk);
      db_insert_message_block(blk, READ_BLOCK_SIZE-1, msgid);
      db_insert_message_block(blk, READ_BLOCK_SIZE-1, msgid);
      snprintf(uniqueid, 70, "testing%d%lu",i,start);
      db_update_message(msgid, uniqueid, strlen(header)+READ_BLOCK_SIZE+READ_BLOCK_SIZE, 0);
      printf("done\n");
    }

  time(&stop);
  printf("Took %d seconds\n", stop-start);

  db_disconnect();
  return 0;
}

