/*
 * vut2dbmail.c
 *
 * converts a virtual user table to dbmail entries
 *
 * the input is read from stdin
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "db.h"
#include "auth.h"

#define MAXLINESIZE 1024
#define DEF_MAXMAILSIZE 1024

char line[MAXLINESIZE];
int process_piece(char *left, char *right);

int main()
{
  int i,result;
  char *left, *right, *tmp;

  if (db_connect() != 0) 
    {
      fprintf(stderr, "Could not connect to database\n");
      return 1;
    }

  if (auth_connect() != 0)
    {
      fprintf(stderr, "Could not connect to authentication\n");
      db_disconnect();
      return 1;
    }

  do
    {
      fgets(line, MAXLINESIZE, stdin);

      if (ferror(stdin) || feof(stdin))
	break;

      if (line[0] != '#' && line[0] != '\n' && line[0])
	{
	  line[strlen(line)-1] = 0; /* remove trailing space */

	  /* get left part of entry */
	  for (i=0; line[i] && !isspace(line[i]); i++) ;

	  if (!line[i] || line[i] == '\n')
	    {
	      if (i > 0)
		{
		  fprintf(stderr, "Found [%*s], don't know what to do with it\n", 
			  i-1, line);
		}
	      continue;
	    }

	  line[i] = 0;
	  left = line;

	  while (isspace(line[++i])) ;

	  right = &line[i];

	  do
	    {
	      tmp = strchr(right, ','); /* find delimiter */

	      if (tmp) 
		*tmp = 0; /* end string on delimiter position */

	      if ((result = process_piece(left, right)) < 0)
		{
		  if (result == -1)
		    {
		      fprintf(stderr, "Error processing [%s] [%s]\n",left,right);
		    }
		}

	      if (tmp)
		{
		  right = tmp+1;
		  while (isspace(*right)) right++;
		}
	    } while (tmp && *right);

	}
    } while (!feof(stdin) && !ferror(stdin));

  /* ok everything inserted. 
   *
   * the alias table should be cleaned up now..
   */

  db_disconnect();
  auth_disconnect();
  return 0;
}
	

int process_piece(char *left, char *right)
{
  u64_t useridnr,clientidnr;

  /* check what right contains:
   * username or email address
   */

  if (strchr(right, '@') || strchr(right,'|'))
    {
      /* email
       * add this alias if it doesn't already exist
       */
      
      if (db_addalias_ext(left, right, 0) == -1)
	return -1;
      
      printf("alias [%s] --> [%s] created\n",left,right);
      return 0;
    }
  else
    {
      /* username
       * check if this user exists
       */

      if ((useridnr = auth_user_exists(right)) == -1)
	return -1;

      if (useridnr == 0)
	{
	  /* new user */
	  if ((useridnr = auth_adduser(right, "geheim", "", "0", "0")) == -1)
	    {
	      fprintf(stderr,"Could not add user [%s]\n",right);
	      return -1;
	    }
	}

      /* this user now exists, add alias */
      if ( (clientidnr = auth_getclientid(useridnr)) == -1)
	{
	  fprintf(stderr,"Could not retrieve client id nr for user [%s] [id %llu]\n", 
		  right, useridnr);
	  return -1;
	}

      if (db_addalias(useridnr, left, clientidnr) == -1)
	{
	  fprintf(stderr,"Could not add alias [%s] for user [%s] [id %llu]\n", 
		  left, right, useridnr);
	  return -1;
	}

      printf("alias [%s] --> [%s] created\n",left,right);
      return 0;
    }

}
