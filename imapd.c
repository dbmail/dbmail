/* $Id$
 * imapd.c
 * 
 * main prg for imap daemon
 */

#include <stdio.h>
#include "imap4.h"
#include "serverservice.h"
#include "debug.h"
#include "misc.h"
#include "dbmysql.h"

#define PNAME "dbmail/imap4"
#define IMAP_DEF_PORT "143"

int main(int argc, char *argv[])
{
  int sock;
  char *newuser,*newgroup,*port;

  /* check arguments */
  if (argc < 2)
    {
      printf("Usage: imapd <ip nr>\n");
      return 0;
    }

  /* open logs */
  openlog(PNAME, LOG_PID, LOG_MAIL);

  /* open db connection */
  if (db_connect() != 0)
    trace(TRACE_FATAL, "IMAPD: cannot connect to dbase\n");

  /* open socket */
  port = db_get_config_item("IMAPD_BIND_PORT",CONFIG_EMPTY);
  if (!port)
    sock = SS_MakeServerSock(argv[1], IMAP_DEF_PORT);
  else
    sock = SS_MakeServerSock(argv[1], port);

  free(port);
  port = NULL;

  if (sock == -1)
    {
      db_disconnect();
      trace(TRACE_FATAL, "IMAPD: Error creating serversocket: %s\n", SS_GetErrorMsg());
      return 1;
    }
  
  /* drop priviledges */
  newuser = db_get_config_item("IMAPD_EFFECTIVE_USER",CONFIG_MANDATORY);
  newgroup = db_get_config_item("IMAPD_EFFECTIVE_GROUP",CONFIG_MANDATORY);

  if ((newuser!=NULL) && (newgroup!=NULL))
  {
    if (drop_priviledges (newuser, newgroup) != 0)
      trace(TRACE_FATAL,"IMAPD: could not set uid %s, gid %s\n",newuser,newgroup);
    
    free(newuser);
    free(newgroup);
    newuser = NULL;
    newgroup = NULL;
  }
  else
    {
      db_disconnect();
      trace(TRACE_FATAL,"IMAPD: newuser and newgroup should not be NULL\n");
    }

  db_disconnect();

  /* get started */
  trace(TRACE_MESSAGE, "IMAPD: server ready to run\n");
  if (SS_WaitAndProcess(sock, imap_process, imap_login) == -1)
    {
      trace(TRACE_FATAL,"IMAPD: Fatal error while processing clients: %s\n",SS_GetErrorMsg());
      return 1;
    }

  SS_CloseServer(sock);

  return 0; 
}

