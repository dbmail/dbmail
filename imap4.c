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

const char AcceptedMailboxnameChars[] = 
"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-=/ ";

const char *IMAP_COMMANDS[] = 
{
  "", "capability", "noop", "logout", 
  "authenticate", "login", 
  "select", "examine", "create", "delete", "rename", "subscribe", "unsubscribe", 
  "list", "lsub", "status", "append", 
  "check", "close", "expunge", "search", "fetch", "store", "copy", "uid",
  "***NOMORE***"
};


enum IMAP_COMMAND_TYPES { IMAP_COMM_NONE, 
			  IMAP_COMM_CAPABILITY, IMAP_COMM_NOOP, IMAP_COMM_LOGOUT, 
			  IMAP_COMM_AUTH, IMAP_COMM_LOGIN, 
			  IMAP_COMM_SELECT, IMAP_COMM_EXAMINE, IMAP_COMM_CREATE,
			  IMAP_COMM_DELETE, IMAP_COMM_RENAME, IMAP_COMM_SUBSCRIBE,
			  IMAP_COMM_UNSUBSCRIBE, IMAP_COMM_LIST, IMAP_COMM_LSUB,
			  IMAP_COMM_STATUS, IMAP_COMM_APPEND,
			  IMAP_COMM_CHECK, IMAP_COMM_CLOSE, IMAP_COMM_EXPUNGE,
			  IMAP_COMM_SEARCH, IMAP_COMM_FETCH, IMAP_COMM_STORE,
			  IMAP_COMM_COPY, IMAP_COMM_UID,
			  IMAP_COMM_LAST };


const IMAP_COMMAND_HANDLER imap_handler_functions[] = 
{
  NULL, 
  _ic_capability, _ic_noop, _ic_logout, 
  _ic_authenticate, _ic_login, 
  _ic_select, _ic_examine, _ic_create, _ic_delete, _ic_rename, 
  _ic_subscribe, _ic_unsubscribe, _ic_list, _ic_lsub, _ic_status, _ic_append, 
  _ic_check, _ic_close, _ic_expunge, _ic_search, _ic_fetch, _ic_store, _ic_copy, _ic_uid,
  NULL
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
      trace(TRACE_MESSAGE,"IMAPD: not enough memory.");
      ci->loginStatus = SS_LOGIN_FAIL;
      return SS_LOGIN_FAIL;
    }

  memset(ci->userData, 0, sizeof(imap_userdata_t));
  ((imap_userdata_t*)ci->userData)->state = IMAPCS_NON_AUTHENTICATED;

  /* greet user */
  fprintf(ci->tx,"* OK dbmail imap (protocol version 4r1) server %s ready to run\n",
	  IMAP_SERVER_VERSION);
  fflush(ci->tx);


  /* done */
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
  int i,done,result;

  /* init */
  if (db_connect() != 0)
    {
      /* could not connect */
      trace(TRACE_MESSAGE, "IMAPD: Connection to dbase failed.\n");
      fprintf(ci->tx, "* BAD could not connect to dbase\n");
      fprintf(ci->tx, "BYE try again later\n");

      fflush(ci->tx);
      shutdown(fileno(ci->tx),SHUT_WR);
      fclose(ci->tx);
      
      return EOF;
    }

  done = 0;
  args = NULL;

  /* start main loop */
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
	  trace(TRACE_MESSAGE, "IMAPD: Not enough memory while building up argument array.");
	  fprintf(ci->tx,"* BAD Internal error: out of memory\n");

	  /* free used memory */
	  for (i=0; args[i]; i++) 
	    free(args[i]);
	  free(args);
	  args = NULL;

	  continue;
	}

      /* check tag */
      if (!checktag(tag))
	{
	  fprintf(ci->tx, "* BAD Invalid tag specified\n");
	  
	  /* free used memory */
	  for (i=0; args[i]; i++) 
	    free(args[i]);
	  free(args);
	  args = NULL;

	  continue;
	}

      for (i=IMAP_COMM_NONE; i<IMAP_COMM_LAST && strcasecmp(command, IMAP_COMMANDS[i]); i++) ;

      if (i <= IMAP_COMM_NONE || i >= IMAP_COMM_LAST)
	{
	  /* unknown command */
	  fprintf(ci->tx,"* BAD command not recognized\n");

	  /* free used memory */
	  for (i=0; args[i]; i++) 
	    free(args[i]);
	  free(args);
	  args = NULL;

	  continue;
	}

      trace(TRACE_MESSAGE, "IMAPD: Executing command %s...\n",IMAP_COMMANDS[i]);

      result = (*imap_handler_functions[i])(tag, args, ci);
      if (result == -1)
	{
	  /* fatal error occurred, kick this user */
	  done = 1;
	}

      if (result == 0 && i == IMAP_COMM_LOGOUT)
	done = 1;

      fflush(ci->tx); /* write! */

      trace(TRACE_MESSAGE, "IMAPD: Finished command %s\n",IMAP_COMMANDS[i]);

      for (i=0; args[i]; i++) 
	free(args[i]);
      free(args);
      args = NULL;

    } while (!done);

  /* cleanup */
  db_disconnect();

  /* say bye! */
  fprintf(ci->tx,"%s OK completed\n",tag);
  trace(TRACE_MESSAGE,"IMAP: Closing connection for client from IP %s\n",ci->ip);

  fflush(ci->tx);
  shutdown(fileno(ci->tx),SHUT_WR);
  fclose(ci->tx);

  return EOF;
}



