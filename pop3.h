/* $Id$
 * (c) 2000-2001 IC&S, The Netherlands
 * 
 * this defines some default messages for POP3 */

#ifndef  _POP3_H
#define  _POP3_H

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
#include "config.h"


/* processes */

#define MAXCHILDREN 5
#define DEFAULT_CHILDREN 5 

/* connection */

#define PORT 110  
#define BACKLOG 10

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

/* all virtual_ definitions are session specific
 * when a RSET occurs all will be set to the real values */

struct message
	{
		unsigned long msize;
		unsigned long messageid;
		unsigned long realmessageid;
		char uidl[UID_SIZE];
		/* message status :
		 * 000 message is new, never touched 
		 * 001 message is read
		 * 002 message is deleted by user 
		 * ----------------------------------
		 * The server additionally uses:
		 * 003 message is deleted by sysop
		 * 004 message is ready for final deletion */
		
		unsigned long messagestatus;
		unsigned long virtual_messagestatus;
	};

struct session
	{
		unsigned long totalsize;
		unsigned long virtual_totalsize; 
		unsigned long totalmessages;
		unsigned long virtual_totalmessages;
		struct list messagelst;
		int validated;
	};

int pop3 (void *stream, char *buffer);
#endif
