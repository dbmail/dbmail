/*
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
 */

#ifndef _CLIENTBASE_H
#define _CLIENTBASE_H

#include "dbmail.h"
 
clientbase_t * client_init(int socket, struct sockaddr_in *caddr, SSL *ssl);
int ci_starttls(clientbase_t *self);
int ci_write(clientbase_t *self, char * msg, ...);
void ci_read_cb(clientbase_t *self);
int ci_read(clientbase_t *self, char *buffer, size_t n);
int ci_readln(clientbase_t *self, char * buffer);
void ci_close(clientbase_t *self);

#endif
