/* $Id$
 * (c) 2000-2002 IC&S, The Netherlands
 * 
 * Copied from pop3.h as a starting point - Aaron Stone, 4/14/03
 * This defines some default messages for LMTP */

#ifndef  _LMTP_H
#define  _LMTP_H

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

#define LMTP_DEF_MAXCONNECT 1500

/* connection */

#define STRT 1
#define LHLO 2
#define DATA 3
#define BIT8 4
#define BDAT 5

/* allowed lmtp commands, from lmtp.c
const char *commands [] =
{
	"LHLO", "QUIT", "RSET", "DATA", "MAIL",
	"VRFY", "EXPN", "HELP", "NOOP", "RCPT"
}; */

#define LMTP_STRT 0 /* lower bound of array - 0 */
#define LMTP_LHLO 0
#define LMTP_QUIT 1
#define LMTP_RSET 2
#define LMTP_DATA 3 
#define LMTP_MAIL 4
#define LMTP_VRFY 5
#define LMTP_EXPN 6
#define LMTP_HELP 7
#define LMTP_NOOP 8
#define LMTP_RCPT 9
#define LMTP_END 10 /* upper bound of array + 1 */

int lmtp(void *stream, void *instream, char *buffer, char *client_ip, PopSession_t *session);
int lmtp_handle_connection(clientinfo_t *ci);

/*
 * Enhanced Status Codes
 *     From RFC 1893
 *
 * Top level codes:
 * 2.X.X     Success
 * 4.X.X     Persistent Transient Failure
 * 5.X.X     Permanent Failure
 *
 * Second the Third level codes:
 * Address Status
 * X.1.0     Other address status
 * X.1.1     Bad destination mailbox address
 * X.1.2     Bad destination system address
 * X.1.3     Bad destination mailbox address syntax
 * X.1.4     Destination mailbox address ambiguous
 * X.1.5     Destination mailbox address valid
 * X.1.6     Mailbox has moved
 * X.1.7     Bad sender's mailbox address syntax
 * X.1.8     Bad sender's system address
 * Mailbox Status
 * X.2.0     Other or undefined mailbox status
 * X.2.1     Mailbox disabled, not accepting messages
 * X.2.2     Mailbox full
 * X.2.3     Message length exceeds administrative limit.
 * X.2.4     Mailing list expansion problem
 * Mail System Status
 * X.3.0     Other or undefined mail system status
 * X.3.1     Mail system full
 * X.3.2     System not accepting network messages
 * X.3.3     System not capable of selected features
 * X.3.4     Message too big for system
 * Network and Routing Status
 * X.4.0     Other or undefined network or routing status
 * X.4.1     No answer from host
 * X.4.2     Bad connection
 * X.4.3     Routing server failure
 * X.4.4     Unable to route
 * X.4.5     Network congestion
 * X.4.6     Routing loop detected
 * X.4.7     Delivery time expired
 * Mail Delivery Protocol Status
 * X.5.0     Other or undefined protocol status
 * X.5.1     Invalid command
 * X.5.2     Syntax error
 * X.5.3     Too many recipients
 * X.5.4     Invalid command arguments
 * X.5.5     Wrong protocol version
 * Message Content or Message Media Status
 * X.6.0     Other or undefined media error
 * X.6.1     Media not supported
 * X.6.2     Conversion required and prohibited
 * X.6.3     Conversion required but not supported
 * X.6.4     Conversion with loss performed
 * X.6.5     Conversion failed
 * Security or Policy Status
 * X.7.0     Other or undefined security status
 * X.7.1     Delivery not authorized, message refused
 * X.7.2     Mailing list expansion prohibited
 * X.7.3     Security conversion required but not possible
 * X.7.4     Security features not supported
 * X.7.5     Cryptographic failure
 * X.7.6     Cryptographic algorithm not supported
 * X.7.7     Message integrity failure
 */

/* Help */
static const char * const LMTP_HELP_TEXT[] = {
/* LMTP_STRT */
  "214-This is DBMail-LMTP.\r\n"
  "214-The following commands are supported:\r\n"
  "214-LHLO, RSET, NOOP, QUIT, HELP.\r\n"
  "214-VRFY, EXPN, MAIL, RCPT, DATA.\r\n"
  "214-For more information about a command:\r\n"
  "214 Use HELP <command>.\r\n"
/* LMTP_LHLO */ ,
  "214-The LHLO command begins a client/server\r\n"
  "214-dialogue. The commands MAIL, RCPT and DATA\r\n"
  "214-may only be issued after a successful LHLO.\r\n"
  "214 Syntax: LHLO [your hostname]\r\n"
/* LMTP_DATA */ ,
  "214-The LHLO command begins a client/server\r\n"
  "214-dialogue. The commands MAIL, RCPT and DATA\r\n"
  "214-may only be issued after a successful LHLO.\r\n"
  "214 Syntax: LHLO [your hostname]\r\n"
/* LMTP_RSET */ ,
  "214-The LHLO command begins a client/server\r\n"
  "214-dialogue. The commands MAIL, RCPT and DATA\r\n"
  "214-may only be issued after a successful LHLO.\r\n"
  "214 Syntax: LHLO [your hostname]\r\n"
/* LMTP_QUIT */ ,
  "214-The LHLO command begins a client/server\r\n"
  "214-dialogue. The commands MAIL, RCPT and DATA\r\n"
  "214-may only be issued after a successful LHLO.\r\n"
  "214 Syntax: LHLO [your hostname]\r\n"
/* LMTP_NOOP */ ,
  "214-The LHLO command begins a client/server\r\n"
  "214-dialogue. The commands MAIL, RCPT and DATA\r\n"
  "214-may only be issued after a successful LHLO.\r\n"
  "214 Syntax: LHLO [your hostname]\r\n"
/* LMTP_HELP */ ,
  "214-The LHLO command begins a client/server\r\n"
  "214-dialogue. The commands MAIL, RCPT and DATA\r\n"
  "214-may only be issued after a successful LHLO.\r\n"
  "214 Syntax: LHLO [your hostname]\r\n"
/* For good measure. */ ,
  NULL
};

#endif
