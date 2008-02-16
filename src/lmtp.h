/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl

 This program is free software; you can redistribute it and/or 
 modify it under the terms of the GNU General Public License 
 as published by the Free Software Foundation; either 
 version 2 of the License, or (at your option) any later 
 version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*
 * 
 * Copied from pop3.h as a starting point - Aaron Stone, 4/14/03
 * This defines some default messages for LMTP */

#ifndef  _LMTP_H
#define  _LMTP_H

#include "dbmail.h"

/* processes */

#define MAXCHILDREN 5
#define DEFAULT_CHILDREN 5

#define LMTP_DEF_MAXCONNECT 1500

/* connection */

#define LMTP_STRT 0		/* lower bound of array - 0 */
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
#define LMTP_END 10		/* upper bound of array + 1 */

int lmtp(ClientSession_t *session);

/* Help */
static const char *const LMTP_HELP_TEXT[] = {
/* LMTP_LHLO 0 */
	"214-The LHLO command begins a client/server\r\n"
	    "214-dialogue. The commands MAIL, RCPT and DATA\r\n"
	    "214-may only be issued after a successful LHLO.\r\n"
	    "214 Syntax: LHLO [your hostname]\r\n"
/* LMTP_QUIT 1 */ ,
	"214-The QUIT command begins a client/server\r\n"
	    "214-dialogue. The commands MAIL, RCPT and DATA\r\n"
	    "214-may only be issued after a successful LHLO.\r\n"
	    "214 Syntax: LHLO [your hostname]\r\n"
/* LMTP_RSET 2 */ ,
	"214-The RSET command begins a client/server\r\n"
	    "214-dialogue. The commands MAIL, RCPT and DATA\r\n"
	    "214-may only be issued after a successful LHLO.\r\n"
	    "214 Syntax: LHLO [your hostname]\r\n"
/* LMTP_DATA 3 */ ,
	"214-The DATA command begins a client/server\r\n"
	    "214-dialogue. The commands MAIL, RCPT and DATA\r\n"
	    "214-may only be issued after a successful LHLO.\r\n"
	    "214 Syntax: LHLO [your hostname]\r\n"
/* LMTP_MAIL 4 */ ,
	"214-The MAIL command begins a client/server\r\n"
	    "214-dialogue. The commands MAIL, RCPT and DATA\r\n"
	    "214-may only be issued after a successful LHLO.\r\n"
	    "214 Syntax: LHLO [your hostname]\r\n"
/* LMTP_VRFY 5 */ ,
	"214-The VRFY command begins a client/server\r\n"
	    "214-dialogue. The commands MAIL, RCPT and DATA\r\n"
	    "214-may only be issued after a successful LHLO.\r\n"
	    "214 Syntax: LHLO [your hostname]\r\n"
/* LMTP_EXPN 6 */ ,
	"214-The EXPN command begins a client/server\r\n"
	    "214-dialogue. The commands MAIL, RCPT and DATA\r\n"
	    "214-may only be issued after a successful LHLO.\r\n"
	    "214 Syntax: LHLO [your hostname]\r\n"
/* LMTP_HELP 7 */ ,
	"214-The HELP command begins a client/server\r\n"
	    "214-dialogue. The commands MAIL, RCPT and DATA\r\n"
	    "214-may only be issued after a successful LHLO.\r\n"
	    "214 Syntax: LHLO [your hostname]\r\n"
/* LMTP_NOOP 8 */ ,
	"214-The NOOP command begins a client/server\r\n"
	    "214-dialogue. The commands MAIL, RCPT and DATA\r\n"
	    "214-may only be issued after a successful LHLO.\r\n"
	    "214 Syntax: LHLO [your hostname]\r\n"
/* LMTP_RCPT 9 */ ,
	"214-The RCPT command begins a client/server\r\n"
	    "214-dialogue. The commands MAIL, RCPT and DATA\r\n"
	    "214-may only be issued after a successful LHLO.\r\n"
	    "214 Syntax: LHLO [your hostname]\r\n"
/* LMTP_END 10 */ ,
	"214-This is DBMail-LMTP.\r\n"
	    "214-The following commands are supported:\r\n"
	    "214-LHLO, RSET, NOOP, QUIT, HELP.\r\n"
	    "214-VRFY, EXPN, MAIL, RCPT, DATA.\r\n"
	    "214-For more information about a command:\r\n"
	    "214 Use HELP <command>.\r\n"
/* For good measure... */ ,
	NULL
};

#endif
