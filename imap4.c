/* $Id$
 * imap4.c
 *
 * implements an IMAP 4 rev 1 server.
 */

#include "imap4.h"
#include "serverservice.h"
#include <stdio.h>

#define MAX_LINESIZE 1024

/* consts */
const char AcceptedChars[] = 
"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
"!@#$%^&*()-=_+`~[]{}\\|'\" ;:,.<>/? ";


/* utility functions */
int stridx(const char *s, char ch);
int checkchars(const char *s);


/* prototypes */
IMAP_COMMAND_HANDLER _ic_login, _ic_authenticate, _ic_select, _ic_list, _ic_logout;


const IMAP_COMMAND_HANDLER imap_handler_functions[] = 
{
  _ic_login, _ic_authenticate, _ic_select, _ic_list, _ic_logout
};


/* 
 * standard login for SS_WaitAndProcess(): always success
 */
int imap_login(ClientInfo *ci)
{
  /* make sure we read/write in line-buffered mode */
  setlinebuf(ci->rx);
  setlinebuf(ci->tx);

  return 1;
}


/*
 * Main handling procedure
 */
int imap_process(ClientInfo *ci)
{
  char line[MAX_LINESIZE];
  char *tag;
  int command,i,tagIdx;
  char **args;

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

      /* read tag & command */

      /* skip whitespace */
      i = 0;
      while (line[i] && line[i] == ' ') i++;
      tagIdx = i;

      if (!line[tagIdx])
	{
	  fprintf(ci->tx, "* BAD No command specified\n");
	  continue;
	}

      tag = &line[tagIdx];

      if (!checktag(tag))
	{
	  fprintf(ci->tx, "* BAD Invalid tag specified\n");
	  continue;
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
