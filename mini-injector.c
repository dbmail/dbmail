/*
 * mini-injector.c
 *
 * Code for the SMTP injector program.
 *
 * optimized for speed:
 * - no quotum checks are performed
 * - user is specified by user ID (numeric)
 * - no scanning is performed
 * - user ID is not checked
 */

#include "db.h"
#include "debug.h"
#include "list.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_LINESIZE 1024
#define UID_SIZE 70

/* syslog */
#define PNAME "dbmail/mini-smtp-injector"

char blk[READ_BLOCK_SIZE];

unsigned process_header(char *hdrdata, u64_t *newlines);


int main(int argc, char *argv[])
{
  u64_t uid, msgid;
  unsigned len;
  u64_t size,newlines=0;
  char newunique[UID_SIZE];

  openlog(PNAME, LOG_PID, LOG_MAIL);   /* open connection to syslog */
  configure_debug(TRACE_ERROR, 1, 0);  /* do not spill time on reading settings */

  /* check command-line options */
  if (argc != 2)
    {
      printf ("\nUsage: %s <user id nr>\n", argv[0]);
      return 0;
    }

  /* open dbase connections */
  if (db_connect() != 0)
    {
      trace(TRACE_FATAL,"main(): could not connect to dbases");
      return -1;
    }

  uid = strtoull(argv[1], NULL, 10);

  if ((len = process_header(blk, &newlines)) == -1)
    {
      db_disconnect();
      trace(TRACE_FATAL, "main(): error processing header");
      return -1;
    }
  
  msgid = db_insert_message(uid, 0, 0);        /* create message */
  db_insert_message_block(blk, len, msgid);    /* insert the header */

  /* read data & insert */
  size = 0;
  while (!feof(stdin) && !ferror(stdin))
    {
      len = fread(blk, sizeof(char), READ_BLOCK_SIZE-1, stdin);
      blk[len] = 0; /* terminate */
      
      if (db_insert_message_block(blk, len, msgid) == -1)
	{
	  db_disconnect();
	  trace(TRACE_FATAL, "main(): error inserting message block");
	  return -1;
	}
	  
      size += len;
      newlines++;
    }

  /* update message */
  snprintf(newunique, UID_SIZE, "%lluA%lu", uid, time(NULL));
  db_update_message(msgid, newunique, size, size+newlines);

  trace(TRACE_ERROR,"message [%llu] inserted, size [%llu]", msgid, size);

  /* cleanup */
  db_disconnect();

  return 0;
}


/*
 * process_header()
 *
 * Reads in a mail header (from stdin, reading is done until '\n\n' is encountered).
 * If field != NULL scanning is done for delivery on that particular field.
 *
 * Data is saved in hdrdata which should be capable of holding at least READ_BLOCK_SIZE characters.
 *
 * returns data cnt on success, -1 on failure
 */
unsigned process_header(char *hdrdata, u64_t *newlines)
{
  char *line;
  unsigned cnt = 0;
  *newlines = 0;

  while (!feof(stdin) && !ferror(stdin) && cnt < READ_BLOCK_SIZE)
    {
      line = &hdrdata[cnt]; /* write directly to hdrdata */
      fgets(line, READ_BLOCK_SIZE - cnt, stdin);
      (*newlines)++;
      if (strcmp(line, "\n") == 0)
	break;

      cnt += strlen(line);
    }
	
  return cnt;
}



