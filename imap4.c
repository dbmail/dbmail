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
#include "imaputil.h"
#include "imapcommands.h"
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

  /* init */
  db_connect();

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

      command = cpy;       /* set command */
      i = stridx(cpy,' '); /* find next space */
      if (i == strlen(cpy))
	{
	  /* no arguments present */
	  args = build_args_array("");
	}
      else
	{
	  cpy[i] = '\0';       /* terminated command */
	  cpy = cpy+i+1;       /* cpy points to args now */
	  args = build_args_array(cpy); /* build argument array */
	}

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

      for (i=IMAP_COMM_NONE; i<IMAP_COMM_LAST && strcasecmp(command, IMAP_COMMANDS[i]); i++) ;

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
  db_disconnect();

  fflush(ci->tx);
  shutdown(fileno(ci->tx),SHUT_WR);
  fclose(ci->tx);

  return EOF;
}



