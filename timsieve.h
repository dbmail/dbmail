/* $Id$
 * (c) 2000-2002 IC&S, The Netherlands
 * 
 * Copied from lmtp.h, in turn from pop3.h, as a starting point - Aaron Stone, 10/8/03
 * This defines some default messages for timsieved */

#ifndef  _TIMS_H
#define  _TIMS_H

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

#define TIMS_DEF_MAXCONNECT 1500

/* connection */

#define STRT 1
#define AUTH 2

/* allowed tims commands, from tims.c
 * The first four take no arguments;
 * The next four take one argument;
 * The last two take two arguments.
const char *commands [] =
{
	"LOGOUT", "STARTTLS", "CAPABILITY", "LISTSCRIPTS",
	"AUTHENTICATE", "DELETESCRIPT", "GETSCRIPT", "SETACTIVE",
	"HAVESPACE", "PUTSCRIPT"
}; */

#define TIMS_STRT 0		/* lower bound of array - 0 */
#define TIMS_LOUT 0
#define TIMS_STLS 1
#define TIMS_CAPA 2
#define TIMS_LIST 3
#define TIMS_NOARGS 4		/* use with if( cmd < TIMS_NOARGS )... */
#define TIMS_AUTH 4
#define TIMS_DELS 5
#define TIMS_GETS 6
#define TIMS_SETS 7
#define TIMS_ONEARG 8		/* use with if( cmd < TIMS_ONEARG )... */
#define TIMS_SPAC 8
#define TIMS_PUTS 9
#define TIMS_END 10		/* upper bound of array + 1 */

int tims(void *stream, void *instream, char *buffer, char *client_ip,
	 PopSession_t * session);
int tims_handle_connection(clientinfo_t * ci);

#endif
