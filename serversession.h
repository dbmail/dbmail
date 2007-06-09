/*
 Copyright (C) 2007 Aaron Stone  aaron@serendipity.cx

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

#ifndef  _SERVERSESSION_H
#define  _SERVERSESSION_H

#include "dbmail.h"


/* max_in_buffer defines the maximum number of bytes that are allowed to be
 * in the incoming buffer */
#define MAX_IN_BUFFER 255


/**
 * \function session_error
 *
 * report an error
 * \param session current session
 * \param stream stream to right to
 * \param formatstring format string
 * \param ... values to fill up formatstring
 */
int session_error(ServiceInfo_t *service, SessionInfo_t *session,
	       const char *formatstring, ...);// PRINTF_ARGS(3, 4);

void session_init(ServiceInfo_t *service, SessionInfo_t *session);
void session_reset(ServiceInfo_t *service, SessionInfo_t *session);
void session_free(ServiceInfo_t *service, SessionInfo_t *session);
int session_handle_connection(ServiceInfo_t *service, clientinfo_t *ci);

#endif
