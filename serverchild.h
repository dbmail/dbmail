/*
 * serverchild.h
 *
 * function prototypes for the children of the main server process;
 * the children will be responsible for handling client connections.
 */

#ifndef SERVERCHILD_H
#define SERVERCHILD_H

#include <sys/types.h>
#include <signal.h>
#include "clientinfo.h"

typedef struct 
{
  int maxConnect;
  int listenSocket;
  int resolveIP;
  int timeout;
  char *timeoutMsg;
  int (*ClientHandler)(clientinfo_t *);
} ChildInfo_t;

void ChildSigHandler(int sig, siginfo_t *info, void *data);
int CheckChildAlive(pid_t pid);
int SetChildSigHandler();
pid_t CreateChild(ChildInfo_t *info);


#endif
