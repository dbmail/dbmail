/* $Id$
 * (c) 2000-2001 IC&S, The Netherlands
 *
 * setting up a server.
 *
 */

#ifndef SS_SERVERSERVICE_H
#define SS_SERVERSERVICE_H

#include <stdio.h>

#define SS_BACKLOG 10
#define SS_TIMEOUT 3
#define SS_MAX_CLIENTS 5

enum SS_LOGIN_VALS {  SS_LOGIN_FAIL, SS_LOGIN_OK };

#define SS_USERNAME_LEN 32
#define SS_IPNUM_LEN 16

/* client-info structure */
typedef struct
{
  FILE *rx,*tx;             /* read & transmit file streams */
  char *id;                 /* ptr to client-id string */
  char ip[SS_IPNUM_LEN];    /* client IP-number */
  int  loginStatus;         /* login status */
  void *userData;           /* xtra info (user-definable) */
} ClientInfo;


int   SS_MakeServerSock(const char *ipaddr, const char *port, int default_children);
int   SS_WaitAndProcess(int sock, int default_children, int max_children, int daemonize,
			int (*ClientHandler)(ClientInfo*), int (*Login)(ClientInfo*),
			void (*ClientCleanup)(ClientInfo*));
void  SS_CloseServer(int sock);
char* SS_GetErrorMsg();

#endif

