/* 
 * this program traverses a directory tree and executes
 * dbmail conversion on each file.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <time.h>
#include <unistd.h>
#include "db.h"
#include "auth.h"
#include "dbmailtypes.h"
#include "debug.h"
#include <regex.h>

#define MAX_LINESIZE 1024
#define UID_SIZE 70

const char *mbox_delimiter_pattern = "^From .*  ";
char blk[READ_BLOCK_SIZE + MAX_LINESIZE + 1];

/* syslog */
#define PNAME "dbmail/uni-one-convertor"

char *getusername (char *path);
int traverse (char *path);
int process_mboxfile(char *file, u64_t userid);



int main (int argc, char* argv[])
{
  time_t start;
  time_t stop;
  int result;

  if (argc < 2)
    {
      printf ("Error, traverse need a directory as argument\n");
      return -1;
    }

  openlog(PNAME, LOG_PID, LOG_MAIL);   /* open connection to syslog */
  configure_debug(TRACE_ERROR, 1, 0);

  /* open dbase connections */
  if (db_connect() != 0 || auth_connect() != 0)
    {
      printf("Error opening dbase connections\n");
      return -1;
    }


  time (&start); /* mark the starting time */
  result = traverse (argv[1]);
  time (&stop); /* mark the ending time */

  printf ("Conversion started @  %s", ctime(&start));
  printf ("Conversion finished @ %s", ctime(&stop));

  return result;
}



char *getusername (char *path)
{
  int i;
  char *tmp;
	
  i = strlen (path);
  tmp = path+i;
	
  while ( (tmp!=path) && (*tmp!='/'))
    tmp--;

  return tmp+1;
}


int traverse (char *path)
{
  char newpath [1024];
  char *username;
  struct dirent **namelist;
  int n;
  u64_t userid;

  n = scandir (path, &namelist, 0, alphasort);

  if (n < 0)
    {
      printf ("file %s\n",path);
      username = getusername(path);
      printf ("username %s\n", username);
	   
      printf("creating user...");
      userid = auth_adduser(username, "default", "", "0", "10M");
      if (userid != -1 && userid != 0)
	{
	  printf("Ok id [%llu]\n", userid);
	  printf("converting mailbox...");
	  fflush(stdout);
	  n = process_mboxfile(path, userid);
	  if (n != 0)
	    {
	      
	    }
	}  
	  
	
      printf ("done :)\n");
    }
  else
    {
      while (n--)
        {
	  if ((strcmp(namelist[n]->d_name,"..")!=0) &&
	      (strcmp(namelist[n]->d_name,".")!=0))
            {
	      sprintf (newpath,"%s/%s",path, namelist[n]->d_name);
	      traverse (newpath);
            }
	  free (namelist[n]);
        }
      free(namelist);
    }
  return 0;
}


int process_mboxfile(char *file, u64_t userid)
{
  regex_t preg;
  int result;
  FILE *infile;
  int in_msg, header_passed;
  char newunique[UID_SIZE];
  unsigned cnt,newlines,len;
  u64_t msgid=0, size;
  
  if ((result = regcomp(&preg, mbox_delimiter_pattern, REG_NOSUB)) != 0)
    {
      trace(TRACE_ERROR,"Regex compilation failed.");
      return -1;
    }

  if ( (infile = fopen(file, "r")) == 0)
    {
      
      trace(TRACE_ERROR,"Could not open file [%s]", infile);
      return -1;
    }

  in_msg = 0;
  cnt = 0;
  newlines = 0;
  size = 0;

  while (!feof(infile) && !ferror(infile))
    {
      if (fgets(&blk[cnt], MAX_LINESIZE, infile) == 0)
	break;

      /* check if this is an mbox delimiter */
      if (regexec(&preg, &blk[cnt], 0, NULL, 0) == 0)
	{
	  if (!in_msg)
	    in_msg = 1; /* ok start of a new msg */
	  else
	    {
	      /* update & end message */
	      db_insert_message_block(blk, cnt, msgid);

	      snprintf(newunique, UID_SIZE, "%lluA%lu", userid, time(NULL));
	      db_update_message(msgid, newunique, size+cnt, size+cnt+newlines);
	      trace(TRACE_ERROR, "message [%llu] inserted, [%u] bytes", msgid, size+cnt);
	    }

	  /* start new message */
	  msgid = db_insert_message(userid, 0, 0);
	  header_passed = 0;
	  cnt = 0;
	  newlines = 0;
	  size = 0;
	}
      else
	{
	  newlines++;
	  
	  if (header_passed == 0)
	    {
	      /* we're still reading the header */
	      len = strlen(&blk[cnt]);
	      if (strcmp(&blk[cnt], "\n") == 0)
		{
		  db_insert_message_block(blk, cnt, msgid);
		  header_passed = 1;
		  size += cnt;
		}

	      cnt += len;
	    }
	  else
	    {
	      /* this is body data */
	      len = strlen(&blk[cnt]);
	      cnt += len;
	      
	      if (cnt >= READ_BLOCK_SIZE-1)
		{
		  /* write block */
		  db_insert_message_block(blk, READ_BLOCK_SIZE, msgid);
		  memmove(blk, &blk[cnt], cnt - READ_BLOCK_SIZE);
		  size += cnt;
		  cnt = 0;
		}
	    }
	}
    }

  fclose(infile);
  return 0;
}
