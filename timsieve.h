/* $Id: timsieve.h 1891 2005-10-03 10:01:21Z paul $

Copyright (C) 2004 Aaron Stone aaron at serendipity dot cx

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
 * Copied from lmtp.h, in turn from pop3.h, 
 * as a starting point - Aaron Stone, 10/8/03
 * This defines some default messages for timsieved */

#ifndef  _TIMS_H
#define  _TIMS_H

#include "dbmail.h"

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

int tims(clientinfo_t *ci, char *buffer, PopSession_t * session);
int tims_handle_connection(clientinfo_t * ci);

#endif
