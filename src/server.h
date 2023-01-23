/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2004-2013 NFG Net Facilities Group BV support@nfg.nl
 Copyright (c) 2014-2019 Paul J Stevens, The Netherlands, support@nfg.nl
 Copyright (c) 2020-2023 Alan Hicks, Persistent Objects Ltd support@p-o.co.uk

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

#ifndef DM_SERVER_H
#define DM_SERVER_H

#include "dbmail.h"

#define BLOCK(a) \
        { \
                int flags; \
                if ( (flags = fcntl(a, F_GETFL, 0)) < 0) \
                        perror("F_GETFL"); \
                flags &= ~O_NONBLOCK; \
                if (fcntl(a, F_SETFL, flags) < 0) \
                        perror("F_SETFL"); \
        }


#define UNBLOCK(a) \
        { \
                int flags; \
                if ( (flags = fcntl(a, F_GETFL, 0)) < 0) \
                        perror("F_GETFL"); \
                flags |= O_NONBLOCK; \
                if (fcntl(a, F_SETFL, flags) < 0) \
                        perror("F_SETFL"); \
        }

int StartCliServer(ServerConfig_T * conf);
int server_run(ServerConfig_T *conf);

void dm_queue_push(void *cb, void *session, void *data);
void dm_queue_drain(void);
void dm_queue_heartbeat(void);

void dm_thread_data_push(gpointer session, gpointer cb_enter, gpointer cb_leave, gpointer data);
void dm_thread_data_sendmessage(gpointer data);

void server_showhelp(const char *service, const char *greeting);
int server_getopt(ServerConfig_T *config, const char *service, int argc, char *argv[]);
int server_mainloop(ServerConfig_T *config, const char *servicename);
pid_t server_daemonize(ServerConfig_T *conf);
void server_http(ServerConfig_T *conf);

#endif
