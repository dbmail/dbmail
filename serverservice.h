/* $Id$
 * serverservice.h
 *
 * setting up a server.
 *
 * (c)2001 IC&S
 */

#ifndef SS_SERVERSERVICE_H
#define SS_SERVERSERVICE_H

#include <stdio.h>

enum SS_SIGNAL_HANDLING_MODE { SS_CATCH_KILL, SS_IGNORE_KILL };

#define SS_BACKLOG 10
#define SS_TIMEOUT 3
#define SS_MAX_CLIENTS 32

enum SS_LOGIN_VALS {  SS_LOGIN_FAIL, SS_LOGIN_OK };

#define SS_USERNAME_LEN 32
#define SS_IPNUM_LEN 16

/* client-info structure */
typedef struct
{
  FILE *rx,*tx;             /* read & transmit file streams */
  int  fd;                  /* file descriptor of connection */
  char *id;                 /* ptr to client-id string */
  char ip[SS_IPNUM_LEN];    /* client IP-number */
  int  loginStatus;         /* login status */
  void *userData;           /* xtra info (user-definable) */
} ClientInfo;


int   SS_MakeServerSock(const char *ipaddr, const char *port, int sighandmode);
int   SS_WaitAndProcess(int sock, int (*ClientHandler)(ClientInfo*), int (*Login)(ClientInfo*));
void  SS_CloseServer(int sock);
char* SS_GetErrorMsg();

#endif

