/* 
 * server.h
 *
 * data type defintions & function prototypes for main server program.
 */

#ifndef _SERVER_H
#define _SERVER_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define IPLEN 32
#define BACKLOG 16

#include <signal.h>
#include "dbmail.h"
#include "clientinfo.h"

typedef struct
{
  int listenSocket;
  int nChildren;
  int childMaxConnect;
  int timeout;
  char ip[IPLEN];
  int port;
  int resolveIP;
  char *timeoutMsg;
  field_t serverUser, serverGroup;
  int (*ClientHandler)(clientinfo_t *);
} serverConfig_t;

int CreateSocket(serverConfig_t *conf);
void ParentSigHandler(int sig, siginfo_t *info, void *data);
int StartServer(serverConfig_t *conf);  

#endif





