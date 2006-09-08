/*
 Copyright (C) 2006 Aaron Stone aaron@serendipity.cx

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

/* $Id: serverparent.h 2199 2006-07-18 11:07:53Z paul $
 * serverparent.h
 *
 * Function prototypes for the top parent code common to all daemons.
 */

#ifndef SERVERPARENT_H
#define SERVERPARENT_H

#include "dbmail.h"

void serverparent_showhelp(const char *service, const char *greeting);
int serverparent_getopt(serverConfig_t *config, const char *service, int argc, char *argv[]);
int serverparent_mainloop(serverConfig_t *config, const char *service);

#endif
