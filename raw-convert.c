/* 
 * this program traverses a directory tree and executes
 * dbmail conversion on each file.
 *
 * data is written to files (up to MAX_BLKFILESIZE each) and then pasted into the
 * (postgresql) dbase using "COPY FROM"
 *
 * A mailsystem of up to MAX_MSGBLKFILES * MAX_BLKFILESIZE can be converted.
 *
 * NOTE: MAX_BLKFILESIZE is not real strict; a new file is opened after the message which
 *       causes the file to be larger than this limit is inserted so normally the actual file
 *       size will be a bit larger (up to a few megabytes usually).
 *       Note as well that it could cause a BUG if a LARGE mailmessage is inserted and the UNIX
 *       filesizelimit is exceeded (at present def's, it would require for that particular msg to
 *       be about 2GB in size..).
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <time.h>
#include <unistd.h>
#include "db.h"
#include "dbmailtypes.h"
#include "debug.h"
#include <regex.h>

#define MAX_LINESIZE 1024
#define UID_SIZE 70
#define MAX_MSGBLKFILES 16
#define MAX_BLKFILESIZE (2ul*1024ul*1024ul)

#define USER_FILENAME "users.data"
#define MBOX_FILENAME "mailboxes.data"
#define MSGS_FILENAME "messages.data"
#define MSGBLKS_FILENAME "messageblocks.data"

#define DEFAULT_PASSWD "default"

const char *mbox_delimiter_pattern = "^From .*  ";
char blk[READ_BLOCK_SIZE + MAX_LINESIZE + 1];

/* FILE pointers */
FILE *userfile, *mailboxfile;
FILE *msgsfile, *msgblksfile[MAX_MSGBLKFILES];

int currblkfile;

/* record counters */
/* these are supposed to be valid all the time */
u64_t useridnr;          /* the unique user ID */
u64_t mboxidnr;          /* the mailbox ID. At present, for each user only INBOX is created (--> useridnr==mboxidnr) */
u64_t msgidnr;           /* the unique ID for each message */ 
u64_t msgblkidnr;        /* the unique ID for each message block */


/* syslog */
#define PNAME "dbmail/raw-convertor"

char *getusername (char *path);
int traverse (char *path);
int process_mboxfile(char *file);
int init_files();
int add_user(const char *uname);
int add_msg(u64_t size, u64_t rfcsize);
int start_blk();
int close_blk(u64_t blksize);
int add_line(const char *line);



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

  if (init_files() != 0)
    {
      printf("Error initializing files\n");
      return 1;
    }

  time (&start); /* mark the starting time */
  result = traverse (argv[1]);
  time (&stop); /* mark the ending time */

  printf ("Conversion started @  %s", ctime(&start));
  printf ("Conversion finished @ %s", ctime(&stop));

  if (result != 0)
    {
      printf("Error occured, exiting...\n");
      return result;
    }

  printf("Inserting into dbase...");
  fflush(stdout);

  /* open dbase connections */
  if (db_connect() != 0)
    {
      printf("Error opening dbase connections\n");
      return -1;
    }

  db_disconnect();
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

  n = scandir (path, &namelist, 0, alphasort);

  if (n < 0)
    {
      printf ("file %s\n",path);
      username = getusername(path);
      printf ("username %s\n", username);
	   
      printf("creating user...");
      add_user(username);
      printf("Ok id [%llu]\n", useridnr);

      printf("converting mailbox...");
      fflush(stdout);
      n = process_mboxfile(path);
      if (n != 0)
	printf("Warning: error converting mailbox\n");
      else
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


int process_mboxfile(char *file)
{
  regex_t preg;
  int result;
  FILE *infile;
  int in_msg, header_passed;
  unsigned len,newlines;
  int blk_opened, msg_opened;
  u64_t size, blksize;
  char saved;
  char line[MAX_LINESIZE];

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
  size = 0;
  newlines = 0;
  blk_opened = 0;
  msg_opened = 0;
  blksize = 0;

  while (!feof(infile) && !ferror(infile))
    {
      if (fgets(line, MAX_LINESIZE, infile) == 0)
	break;

      /* check if this is an mbox delimiter */
      if (regexec(&preg, line, 0, NULL, 0) == 0)
	{
	  if (blk_opened)
	    {
	      close_blk(blksize); /* update & end message */
	      blk_opened = 0;
	    }
	  
	  if (msg_opened)
	    {
	      /* add the message data after all the blocks have been inserted */
	      add_msg(size, size+newlines);
	      msg_opened = 0;
	    }

	  /* start new message */
	  header_passed = 0;
	  size = 0;
	  newlines = 0;
	  msg_opened = 0;
	  blk_opened = 0;
	  blksize = 0;
	}
      else
	{
	  if (!msg_opened)
	    {
	      /* go on and really open this message */
	      start_blk();
	      msg_opened = 1;
	      blk_opened = 1;
	    }

	  newlines++;
	  if (header_passed == 0)
	    {
	      /* we're still reading the header */
	      len = strlen(line);
	      blksize += len;
	      add_line(line);

	      if (strcmp(line, "\n") == 0)
		{
		  close_blk(blksize);
		  size += blksize;
		  blksize = 0;
		  
		  header_passed = 1;
		  blk_opened = 0;
		}
	    }
	  else
	    {
	      /* this is body data */
	      if (!blk_opened)
		{
		  start_blk();
		  blk_opened = 1;
		  blksize = 0;
		}

	      len = strlen(line);
	      blksize += len;
	      
	      if (blksize >= READ_BLOCK_SIZE)
		{
		  /* write block up to READ_BLOCK_SIZE */
		  saved = line[blksize - READ_BLOCK_SIZE];
		  line[blksize - READ_BLOCK_SIZE] = 0;
		  
		  add_line(line);
		  close_blk(READ_BLOCK_SIZE);
		  size += blksize;
		  blk_opened = 0;

		  line[blksize - READ_BLOCK_SIZE] = saved;
		  blksize -= READ_BLOCK_SIZE;
		}
	      else
		{
		  /* write this line */
		  add_line(line);
		}
	    }
	}
    }

  if (blk_opened)
    {
      close_blk(blksize);
      size += blksize;
      blk_opened = 0;
    }

  if (msg_opened)
    {
      /* add the message data after all the blocks have been inserted */
      add_msg(size, size+newlines);
      msg_opened = 0;
    }

  fclose(infile);
  return 0;
}


int init_files()
{
  userfile = fopen(USER_FILENAME, "w");
  mailboxfile = fopen(MBOX_FILENAME, "w");
  msgsfile = fopen(MSGS_FILENAME, "w");
  msgblksfile[0] = fopen(MSGBLKS_FILENAME"00", "w");

  if (!userfile || !mailboxfile || !msgsfile || !msgblksfile[0])
    return -1;

  currblkfile = 0;

  useridnr = 0;
  mboxidnr = 0;
  msgidnr = 0; 
  msgblkidnr = 0;
  
  return 0;
}


/* adds a user and a INBOX */
int add_user(const char *uname)
{
  useridnr++;
  mboxidnr++;

  /* layout: useridnr userid passwd clientidnr maxmailsize enctype */
  fprintf(userfile, "%llu\t%s\t%s\t0\t10000000\t\n", useridnr, uname, DEFAULT_PASSWD);

  /* layout: mboxidnr owneridnr name
     seen/answered/deleted/flagged/recent/draft -flag 
     no-inferiors no-select permission is_subscribed */

  fprintf(mailboxfile, "%llu\t%llu\t%s\t"
	  "0\t0\t0\t0\t0\t0\t"
	  "0\t0\t2\t1\n", mboxidnr, useridnr, "INBOX");

  if (ferror(userfile) || ferror(mailboxfile))
    return -1;

  return 0;
}



/* add a msg */
int add_msg(u64_t size, u64_t rfcsize)
{
  char uniqueid[UID_SIZE];
  char timestr[30];
  time_t td;
  struct tm tm;

  time(&td);              /* get time */
  tm = *localtime(&td);   /* get components */
  strftime(timestr, sizeof(timestr), "%G-%m-%d %H:%M:%S", &tm);

  snprintf(uniqueid, UID_SIZE, "%lluA%lu", useridnr, time(NULL));

  msgidnr++;

  /* layout: msgidnr mboxidnr msgsize 
     seen/answered/deleted/flagged/recent/draft -flag 
     uniqueID internaldate
     status rfcsize
   */
  fprintf(msgsfile, "%llu\t%llu\t%llu\t"
	  "0\t0\t0\t0\t1\t0\t"
	  "%s\t%s\t"
	  "000\t%llu\n", msgidnr, mboxidnr, size, uniqueid, timestr, rfcsize);

  return (ferror(msgsfile)) ? -1 : 0;
}
	  

/* start a new blk: write blkidnr, msgidnr
 * note that the message id is still from the previous msg, so
 * use msgidnr+1 here.
 * This is because the message record is added AFTER all the blocks have been inserted
 * (that way we immediately have the message/rfc size) and is only when add_msg() is called
 * that the msgID will be incremented.
 */
int start_blk()
{
  msgblkidnr++;

  fprintf(msgblksfile[currblkfile], "%llu\t%llu\t", msgblkidnr, msgidnr+1);

  return (ferror(msgblksfile[currblkfile])) ? -1 : 0;
}


/*
 * closes a msg blk record 
 * opens a new msgblkfile if the file size limit is exceeded
 */
int close_blk(u64_t blksize)
{
  char newfname[] = MSGBLKS_FILENAME"XX";

  /* terminate previous field & put blksize, terminate record */
  fprintf(msgblksfile[currblkfile], "\t%llu\n", blksize);

  if (ferror(msgblksfile[currblkfile]))
    return -1;

  /* open the next file if necessary */
  if (ftell(msgblksfile[currblkfile]) >= MAX_BLKFILESIZE)
    {
      fclose(msgblksfile[currblkfile]);
      currblkfile++;
      sprintf(newfname, "%s%02d", MSGBLKS_FILENAME, currblkfile);
      
      if ( (msgblksfile[currblkfile] = fopen(newfname, "w")) == NULL)
	return -1;
    }

  return 0;
}


/* 
 * escapes and adds this line to the current msgblkfile
 * line should not be larger than MAX_LINESIZE
 */
int add_line(const char *line)
{
  char escaped_line[MAX_LINESIZE*2 +1];
  int i,j;

  /* escape data */
  /* ASCII 0x08 - 0x0D (\b, \t, \n, \v, \f, \r) and backslash will be escaped */
  for (i=0,j=0; line[i]; i++)
    {
      if ( (line[i] >= '\b' && line[i] <= '\r') || line[i] == '\\')
	escaped_line[j++] = '\\'; /*  put escape */
 
      escaped_line[j++] = line[i]; /* copy data */
    }
    
  escaped_line[j] = 0; /* terminate */
  
  fprintf(msgblksfile[currblkfile], "%s", escaped_line);

  return (ferror(msgblksfile[currblkfile])) ? -1 : 0;
}

