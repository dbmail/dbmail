/* $Id$
 * imapd.c
 * 
 * main prg for imap daemon
 */

#include <stdio.h>
#include "imap4.h"
#include "serverservice.h"
#include "debug.h"

#define PNAME "dbmail/imap4"

int main(int argc, char *argv[])
{
  int sock;

  /* check arguments */
  if (argc < 3)
    {
      printf("Usage: imapd <ip nr> <port>\n");
      return 0;
    }

  /* open logs */
  openlog(PNAME, LOG_PID, LOG_MAIL);

  /* open socket */
  sock = SS_MakeServerSock(argv[1], argv[2], SS_CATCH_KILL);
  if (sock == -1)
    {
      trace(TRACE_FATAL, "IMAPD: Error creating serversocket: %s\n", SS_GetErrorMsg());
      return 1;
    }


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
