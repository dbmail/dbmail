#include "db.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "debug.h"
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
  int i;
  time_t start,stop;

  openlog(PNAME, LOG_PID, LOG_MAIL);   /* open connection to syslog */
  configure_debug(TRACE_d, 1, 0);

  memset(blk, 'x', READ_BLOCK_SIZE);
  blk[READ_BLOCK_SIZE-1] = 0;

  if (db_connect() != 0)
    {
      printf("connection error");
      return -1;
    }
	
  
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

