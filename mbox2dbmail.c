/*
 * mbox2dbmail.c
 *
 * conversion tool which reads an mbox file for a specific user
 * and stores it into the dbmail tables.
 *
 * The file is read from stdin, the user is specified on the command line.
 */

#include <stdio.h>
#include <sys/types.h>
#include <regex.h>
#include <stdlib.h>

#define MAX_LINESIZE 1024
#define SMTP_INJECTOR "./dbmail-smtp -u "

const char *mbox_delimiter_pattern = "^From .*@.*  ";

int main(int argc, char *argv[])
{
  regex_t preg;
  int result;
  int in_msg;
  char line[MAX_LINESIZE],cmdstr[MAX_LINESIZE];
  FILE *smtp = 0;
  unsigned long long uid;

  if ((result = regcomp(&preg, mbox_delimiter_pattern, REG_NOSUB)) != 0)
    {
      fprintf(stderr,"Regex compilation failed.\n");
      return 1;
    }

  if (argc >= 2)
    {
      /* user ID specified as an argument */
      snprintf(cmdstr, MAX_LINESIZE, "%s %s", SMTP_INJECTOR, argv[1]);
    }
  else
    {
      /* first line should be user ID */
      if (fgets(line, MAX_LINESIZE, stdin) == 0)
	{
	  fprintf(stderr, "Error reading from stdin\n");
	  return -1;
	}
      
      uid = strtoull(line, NULL, 10);
      snprintf(cmdstr, MAX_LINESIZE, "%s %llu", SMTP_INJECTOR, uid);
    }
  in_msg = 0;

  while (!feof(stdin) && !ferror(stdin))
    {
      if (fgets(line, MAX_LINESIZE, stdin) == 0)
	break;

      /* check if this is a mbox delimiter */
      if (regexec(&preg, line, 0, NULL, 0) == 0)
	{
	  if (!in_msg)
	    {
	      /* ok start of a new msg */
	      /* this code will only be reached if it concerns the first msg */
	      if ((smtp = popen(cmdstr, "w")) == 0)
		{
		  perror("Error opening pipe");
		  break;
		}
	      
	      in_msg = 1;
	    }
	  else
	    {
	      /* close current pipe */
	      pclose(smtp);

	      /* open new pipe */
	      if ((smtp = popen(cmdstr, "w")) == 0)
		{
		  perror("Error opening pipe");
		  break;
		}
	    }
	}
      else
	{
	  /* write data to pipe */
	  if (smtp)
	    fputs(line, smtp);
	  else
	    {
	      fprintf(stderr,"Tried to write to an unopened pipe!\n");
	      return 1;
	    }
	}
    }


  return 0;
}



