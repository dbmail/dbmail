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
 * (c) 2000-2002 IC&S, The Netherlands */

#ifndef _MISC_H
#define _MISC_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <grp.h>
#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>
#include "db.h"
#include "debug.h"
#include "list.h"

/**
   \brief drop process privileges. Change change euid and egid to
   uid and gid of newuser and newgroup
   \param newuser user to change to
   \param newgroup group to change to
   \return 
        - -1 on error
	-  0 on success
*/
int drop_privileges (char *newuser, char *newgroup);

/**
 * \brief convert integer to string (length 42, long enough for 
 * 128 bit integer)
 * \param i the integer
 * \return string
 */
char * itoa (int i);
/**
 * \brief create a unique id for a message (used for pop, stored per message)
 * \param target target string. Length should be UID_SIZE 
 * \param message_idnr message_idnr of message
 */
void create_unique_id(char *target, u64_t message_idnr);

/**
 * \brief create a timestring with the current time.
 * \param timestring an allocated timestring object.
 */
void create_current_timestring(timestring_t *timestring);
#endif
