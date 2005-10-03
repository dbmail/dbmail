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

/* $Id: pop3.h 1796 2005-06-07 12:15:32Z paul $
 * 
 * this defines some default messages for POP3 */

#ifndef  _POP3_H
#define  _POP3_H

#include "dbmail.h"

/* processes */
#define MAXCHILDREN 5
#define DEFAULT_CHILDREN 5
#define POP3_DEF_MAXCONNECT 1500

/* connection */

/** 
 * all POP3 commands */
typedef enum {
	POP3_QUIT,
	POP3_USER,
	POP3_PASS,
	POP3_STAT,
	POP3_LIST,
	POP3_RETR,
	POP3_DELE,
	POP3_NOOP,
	POP3_LAST,
	POP3_RSET,
	POP3_UIDL,
	POP3_APOP,
	POP3_AUTH,
	POP3_TOP,
	POP3_CAPA,
} Pop3Cmd_t;

/**
 * \brief handle a client command
 * \param stream output stream (connected to client)
 * \param buffer command buffer
 * \param client_ip ip address of client (used for APOP)
 * \param session pointer to current Pop Session
 * \return 
 *    -1 on error
 *     0 on QUIT (client command)
 *     1 on success
 */
int pop3(clientinfo_t *ci, char *buffer, PopSession_t * session);

/**
 * \brief handles connection and calls pop command handler
 * \brief ci pointer to a clientinfo_t struct which is filled out by the
 *        function
 * \return 0
 */
int pop3_handle_connection(clientinfo_t * ci);

#endif
