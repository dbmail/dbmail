/*
 * serverservice.h
 *
 * header-file voor het opzetten v/e server.
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

typedef struct
{
  FILE *rx,*tx;
  int  fd;
  char *id;
  char ip[SS_IPNUM_LEN];
  int  loginStatus;
} ClientInfo;


int   SS_MakeServerSock(const char *ipaddr, const char *port, int sighandmode);
int   SS_WaitAndProcess(int sock, int (*ClientHandler)(ClientInfo*), int (*Login)(ClientInfo*));
void  SS_CloseServer(int sock);
char* SS_GetErrorMsg();

#endif

