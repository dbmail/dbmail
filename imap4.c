/* $Id$
 * imap4.c
 *
 * implements an IMAP 4 rev 1 server.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include "imap4.h"
#include "serverservice.h"
#include "debug.h"
#include "dbmysql.h"

#define MAX_LINESIZE 1024

/* consts */
const char AcceptedChars[] = 
"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
"!@#$%^&*()-=_+`~[]{}\\|'\" ;:,.<>/? \n\r";

const char AcceptedTagChars[] = 
"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
"!@#$%^&+()-=_`~[]{}\\|'\" ;:,.<>/? ";

const char *IMAP_COMMANDS[] = 
{
  "", "login", "authenticate", "select", "list", "logout"
};


/* utility functions */
int stridx(const char *s, char ch);
int checkchars(const char *s);
int checktag(const char *s);
void clarify_data(char *str);
char **build_args_array(const char *s);


/* prototypes */
void _ic_login(char *tag, char **args, ClientInfo *ci);
void _ic_authenticate(char *tag, char **args, ClientInfo *ci);
void _ic_select(char *tag, char **args, ClientInfo *ci);
void _ic_list(char *tag, char **args, ClientInfo *ci);
void _ic_logout(char *tag, char **args, ClientInfo *ci);


enum IMAP_COMMAND_TYPES { IMAP_COMM_NONE, IMAP_COMM_LOGIN, IMAP_COMM_AUTH, IMAP_COMM_SELECT, 
			  IMAP_COMM_LIST, IMAP_COMM_LOGOUT, IMAP_COMM_LAST };


const IMAP_COMMAND_HANDLER imap_handler_functions[] = 
{
  NULL, _ic_login, _ic_authenticate, _ic_select, _ic_list, _ic_logout
};




/* 
 * standard login for SS_WaitAndProcess(): always success
 */
int imap_login(ClientInfo *ci)
{
  /* make sure we read/write in line-buffered mode */
  setlinebuf(ci->rx);
  setlinebuf(ci->tx);

  /* add userdata */
  ci->userData = malloc(sizeof(imap_userdata_t));
  if (!ci->userData)
    {
      /* out of mem */
      trace(TRACE_MESSAGE,"IMAP: not enough memory.");
      ci->loginStatus = SS_LOGIN_FAIL;
      return SS_LOGIN_FAIL;
    }

  ((imap_userdata_t*)ci->userData)->state = IMAPCS_NON_AUTHENTICATED;

  ci->loginStatus = SS_LOGIN_OK;
  return SS_LOGIN_OK;
}


/*
 * Main handling procedure
 */
int imap_process(ClientInfo *ci)
{
  char line[MAX_LINESIZE];
  char *tag,*cpy,**args,*command;
  int i;

  do
    {
      /* read command line */
      fgets(line, MAX_LINESIZE, ci->rx);
      if (!checkchars(line))
	{
	  /* foute tekens ingetikt */
	  fprintf(ci->tx, "* BAD Input contains invalid characters\n");
	  continue;
	}

      /* clarify data a little */
      clarify_data(line);

      if (!(*line))
	{
	  fprintf(ci->tx, "* BAD No tag specified\n");
	  continue;
	}
	  
      /* read tag & command */
      cpy = line;

      i = stridx(cpy,' '); /* find next space */
      if (i == strlen(cpy))
	{
	  fprintf(ci->tx, "* BAD No command specified\n");
	  continue;
	}

      tag = cpy;           /* set tag */
      cpy[i] = '\0';
      cpy = cpy+i+1;       /* cpy points to command now */

      i = stridx(cpy,' '); /* find next space */
      command = cpy;       /* set command */
      cpy[i] = '\0';       /* if no space found this will not harm */
      cpy = cpy+i+1;       /* cpy points to args now */

      /* build a 2 dimensional NULL-terminated array for the arguments */
      args = build_args_array(cpy);
      if (!args)
	{
	  trace(TRACE_MESSAGE, "Not enough memory while building up argument array.");
	  fprintf(ci->tx,"* BAD Internal error: out of memory\n");
	  continue;
	}

      /* check tag */
      if (!checktag(tag))
	{
	  fprintf(ci->tx, "* BAD Invalid tag specified\n");
	  continue;
	}

      for (i=IMAP_COMM_NONE; strcasecmp(command, IMAP_COMMANDS[i]) && i<IMAP_COMM_LAST; i++) ;

      if (i <= IMAP_COMM_NONE || i >= IMAP_COMM_LAST)
	{
	  /* unknown command */
	  fprintf(ci->tx,"* BAD command not recognized\n");
	  continue;
	}

      (*imap_handler_functions[i])(tag, args, ci);
      fflush(ci->tx); /* write! */

      for (i=0; args[i]; i++) free(args[i]);
      free(args);

    } while (i != IMAP_COMM_LOGOUT);

  /* cleanup */
  fflush(ci->tx);
  shutdown(fileno(ci->tx),SHUT_WR);
  fclose(ci->tx);

  return EOF;
}


/*
 * COMMAND-HANDLING FUNCTIONS
 */


/*
 * _ic_login()
 *
 * Performs login-request handling.
 */
void _ic_login(char *tag, char **args, ClientInfo *ci)
{
  imap_userdata_t *ud = (imap_userdata_t*)ci->userData;
  unsigned long userid;

  if (ud->state != IMAPCS_NON_AUTHENTICATED)
    {
      fprintf(ci->tx,"%s BAD LOGIN command received in invalid state\n",tag);
      return;
    }

  if (!args[0] || !args[1])
    {
      /* error: need 2 args */
      fprintf(ci->tx,"%s BAD missing argument(s) to LOGIN\n",tag);
      return;
    }

  if (args[3])
    {
      /* error: >2 args */
      fprintf(ci->tx,"%s BAD too many arguments to LOGIN\n",tag);
      return;
    }

  userid = db_validate(args[0], args[1]);

  if (userid == -1)
    {
      /* a db-error occurred */
      trace(TRACE_MESSAGE,"IMAP: db-validate error while validating user %s (pass %s).",args[0],args[1]);
      fprintf(ci->tx,"* BAD internal db error validating user\n");
      return;
    }

  if (userid == 0)
    {
      /* validation failed: invalid user/pass combination */
      fprintf(ci->tx, "%s NO login rejected\n",tag);
      return;
    }

  /* login ok */
  /* update client info */
  ud->userid = userid;
  ud->state = IMAPCS_AUTHENTICATED;

  fprintf(ci->tx,"%s OK user %s logged in\n",tag,args[0]);
  return;
}

void _ic_authenticate(char *tag, char **args, ClientInfo *ci)
{

}

void _ic_select(char *tag, char **args, ClientInfo *ci) {}
void _ic_list(char *tag, char **args, ClientInfo *ci) {}
void _ic_logout(char *tag, char **args, ClientInfo *ci) {}


/*
 * UTILITY FUNCTIONS
 */

/*
 * build_args_array()
 *
 * builds an dimensional array of strings containing arguments based upon 
 * a series of arguments passed as a single string.
 * Parentheses have special meaning:
 * '(body (all header))' will result in the following array:
 * [0] = '('
 * [1] = 'body'
 * [2] = '('
 * [3] = 'all'
 * [4] = 'header'
 * [5] = ')'
 * [6] = ')'
 *
 * parentheses loose their special meaning if inside (double)quotation marks;
 * data should be 'clarified' (see clarify_data() function below)
 *
 * The returned array will be NULL-terminated.
 * Will return NULL upon errors.
 */

char **build_args_array(const char *s)
{
  char **args;
  char *scpy;
  int nargs=0,inquote=0,i,quotestart,currarg;
  
  scpy = (char*)malloc(sizeof(char)*strlen(s));
  if (!scpy)
    return NULL;

  /* copy original to scpy */
  strcpy(scpy,s);

  /* now replace all delimiters by \0 */
  for (i=0,inquote=0; i<strlen(s); i++)
    {
      if (scpy[i] == '"')
	{
	  if ((i>0 && scpy[i-1]!='\\') || i==0)
	    {
	      /* toggle in-quote flag */
	      inquote ^= 1;
	    }
	}

      if ((scpy[i] == ' ' || scpy[i] == '(' || scpy[i] == ')') && !inquote)
	{
	  scpy[i] = '\0';
	}
    }

  /* count the arguments */
  for (i=0,nargs=0; i<strlen(s); i++)
    {
      if (!scpy[i])
	{
	  /* check for ( or ) in original string */
	  if (s[i] == '(' || s[i] == ')')
	    nargs++;
	      
	  continue;
	}

      if (scpy[i] == '"')
	{
	  if ((i>0 && scpy[i-1]!='\\') || i==0)
	    {
	      /* toggle in-quote flag */
	      inquote ^= 1;
	      if (inquote)
		nargs++;
	    }
	}
      else
	{
	  if (!inquote)
	    {
	      /* at an argument now, proceed to end (before next NULL char) */
	      while (scpy[i] && i<strlen(s)) i++;
	      i--;
	      nargs++;
	    }
	}
    }

  /* alloc memory */
  args = (char**)malloc((nargs+1) * sizeof(char *));
  if (!args)
    {
      /* out of mem */
      return NULL;
    }

  /* single out the arguments */
  currarg = 0;
  for (i=0; i<strlen(s); i++)
    {
      if (!scpy[i])
	{
	  /* check for ( or ) in original string */
	  if (s[i] == '(' || s[i] == ')')
	    {
	      /* add parenthesis */
	      /* alloc mem */
	      args[currarg] = (char*)malloc(sizeof(char) * 2);

	      if (!args[currarg])
		{
		  /* out of mem */
		  /* free currently allocated mem */
		  for (i=0; i<currarg; i++)
		    free(args[i]);
	      
		  free(args);
		  return NULL;
		}

	      args[currarg][0] = s[i];
	      args[currarg][1] = '\0';
	      currarg++;
	    }
	  continue;
	}

      if (scpy[i] == '"')
	{
	  if ((i>0 && s[i-1]!='\\') || i==0)
	    {
	      /* toggle in-quote flag */
	      inquote ^= 1;
	      if (inquote)
		{
		  /* just started the quotation, remember idx */
		  quotestart = i;
		}
	      else
		{
		  /* alloc mem */
		  args[currarg] = (char*)malloc(sizeof(char) * (i-quotestart+1+1));
		  if (!args[currarg])
		    {
		      /* out of mem */
		      /* free currently allocated mem */
		      for (i=0; i<currarg; i++)
			free(args[i]);

		      free(args);
		      return NULL;
		    }

		  /* copy quoted string */
		  memcpy(args[currarg], &s[quotestart], sizeof(char)*(i-quotestart+1));
		  args[currarg][i-quotestart+1] = '\0'; 
		  currarg++;
		}
	    }
	}
      else if (!inquote)
	{
	  /* at an argument now, save & proceed to end (before next NULL char) */
	  /* alloc mem */
	  args[currarg] = (char*)malloc(sizeof(char) * (strlen(&scpy[i])+1) );
	  if (!args[currarg])
	    {
	      /* out of mem */
	      /* free currently allocated mem */
	      for (i=0; i<currarg; i++)
		free(args[i]);
	      
	      free(args);
	      return NULL;
	    }

	  /* copy arg */
	  memcpy(args[currarg], &scpy[i], sizeof(char)*(strlen(&scpy[i])+1) );
	  currarg++;

	  while (scpy[i] && i<strlen(s)) i++;
	  i--;
	}
    }

  args[currarg] = NULL; /* terminate array */
  return args;
}


/*
 * clarify_data()
 *
 * replaces all multiple spaces by a single one except for quoted spaces;
 * removes leading and trailing spaces and a single trailing newline (if present)
 */
void clarify_data(char *str)
{
  int startidx,i,inquote,endidx;

  /* remove leading spaces */
  for (i=0; str[i] == ' '; i++) ;
  memmove(str, &str[i], sizeof(char) * (strlen(&str[i])+1)); /* add one for \0 */

  /* remove trailing spaces */
  endidx = strlen(str)-1;
  if (endidx >= 0 && str[endidx] == '\n')
    endidx--;

  for (i=endidx; i>0 && str[i] == ' '; i--) ;
  if (i == 0)
    {
      /* empty string remains */
      *str = '\0';
      return;
    }

  str[i+1] = '\0';
  

  /* scan for multiple spaces */
  inquote = 0;
  for (i=0; i < strlen(str); i++)
    {
      if (str[i] == '"')
	{
	  if ((i > 0 && str[i-1]!='\\') || i == 0)
	    {
	      /* toggle in-quote flag */
	      inquote ^= 1;
	    }
	}

      if (str[i] == ' ' && !inquote)
	{
	  for (startidx = i; str[i] == ' '; i++);

	  if (i-startidx > 1)
	    {
	      /* multiple non-quoted spaces found --> remove 'm */
	      memmove(&str[startidx+1], &str[i], sizeof(char) * (strlen(&str[i])+1));
	      /* update i */
	      i = startidx+1;
	    }
	}
    }
}      
     

/*
 * retourneert de idx v/h eerste voorkomen van ch in s,
 * of strlen(s) als ch niet voorkomt
 */
int stridx(const char *s, char ch)
{
  int i;

  for (i=0; s[i] && s[i] != ch;  i++) ;

  return i;
}


/*
 * checkchars()
 *
 * performs a check to see if the read data is valid
 * returns 0 if invalid, 1 otherwise
 */
int checkchars(const char *s)
{
  int i;

  for (i=0; s[i]; i++)
    {
      if (stridx(AcceptedChars, s[i]) == strlen(AcceptedChars))
	{
	  /* wrong char found */
	  return 0;
	}
    }
  return 1;
}


/*
 * checktag()
 *
 * performs a check to see if the read data is valid
 * returns 0 if invalid, 1 otherwise
 */
int checktag(const char *s)
{
  int i;

  for (i=0; s[i]; i++)
    {
      if (stridx(AcceptedTagChars, s[i]) != strlen(AcceptedTagChars))
	{
	  /* wrong char found */
	  return 0;
	}
    }
  return 1;
}
