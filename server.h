/*
  $Id$
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
 * server.h
 *
 * data type defintions & function prototypes for main server program.
 */

#ifndef _SERVER_H
#define _SERVER_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define IPLEN 32
#define BACKLOG 16

#include <signal.h>
#include "dbmail.h"
#include "clientinfo.h"

typedef struct {
	int listenSocket;
	int nChildren;
	int childMaxConnect;
	int timeout;
	char ip[IPLEN];
	int port;
	int resolveIP;
	char *timeoutMsg;
	field_t serverUser, serverGroup;
	int (*ClientHandler) (clientinfo_t *);
} serverConfig_t;

int CreateSocket(serverConfig_t * conf);
int StartServer(serverConfig_t * conf);

#endif
