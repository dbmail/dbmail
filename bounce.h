/*
 Copyright (C) 1999-2003 IC & S  dbmail@ic-s.nl

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

/* $Id$ 
 * (c) 2000-2002 IC&S, The Netherlands
 */

/**
 * \file bounce.h
 * \brief the function(s) in this file are used for bouncing messages
 * that cannot be delivered.
 * 
 */


#ifndef _BOUNCE_H
#define _BOUNCE_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include "debug.h"

/** different reasons for bouncing a message */
typedef enum {
     BOUNCE_NO_SUCH_USER,        /**< no such user in database */
     BOUNCE_STORAGE_LIMIT_REACHED/**< user exceeds mail quotum */
} bounce_reason_t;

/**
 * \brief bounce a message. This sends a message back to the mail inserting
 *        program (e.g. postfix) notifying it of the failure of the 
 *        delivery of a message
 * \param header header of original message
 * \param destination_address address that the original message was directed
 *        at.
 * \param reason reason why message is bounced
 * \return
 *     - -1 on error
 *     -  0 on success
 * \bug this function uses popen to open a shell. The input to this shell is
 * not checked, which should be done to make sure only valid data is sent
 * to the shell.
 */
int bounce (char *header, char *destination_address,
	    bounce_reason_t reason);

#endif
