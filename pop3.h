/* $Id$
 * (c) 2000-2002 IC&S, The Netherlands
 * 
 * this defines some default messages for POP3 */

#ifndef  _POP3_H
#define  _POP3_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>

#include "misc.h"
#include "list.h"
#include "debug.h"
#include "dbmail.h"
#include "dbmailtypes.h"
#include "clientinfo.h"

/* processes */

#define MAXCHILDREN 5
#define DEFAULT_CHILDREN 5 

#define POP3_DEF_MAXCONNECT 1500

/* connection */

#define AUTHORIZATION 1
#define TRANSACTION 2
#define UPDATE 3

#define POP3_STRT 0
#define POP3_QUIT 0
#define POP3_USER 1
#define POP3_PASS 2
#define POP3_STAT 3 
#define POP3_LIST 4
#define POP3_RETR 5
#define POP3_DELE 6
#define POP3_NOOP 7
#define POP3_LAST 8
#define POP3_RSET 9
#define POP3_UIDL 10
#define POP3_APOP 11
#define POP3_AUTH 12
#define POP3_TOP 13
#define POP3_END 14

int pop3 (void *stream, char *buffer, char *client_ip, PopSession_t *session);
int pop3_handle_connection (clientinfo_t *ci);

#endif
