/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2005-2006 NFG Net Facilities Group BV support@nfg.nl

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

#include "dbmail.h"
 
#define UNBLOCK(a) \
        { \
                int flags; \
                if ( (flags = fcntl(a, F_GETFL, 0)) < 0) \
                        perror("F_GETFL"); \
                flags |= O_NONBLOCK; \
                if (fcntl(a, F_SETFL, flags) < 0) \
                        perror("F_SETFL"); \
        }

int StartCliServer(serverConfig_t * conf);
int server_run(serverConfig_t *conf);

void dm_queue_drain(clientbase_t *client);

void disconnect_all(void);
void server_showhelp(const char *service, const char *greeting);
int server_getopt(serverConfig_t *config, const char *service, int argc, char *argv[]);
int server_mainloop(serverConfig_t *config, const char *service, const char *servicename);
pid_t server_daemonize(serverConfig_t *conf);

#endif
