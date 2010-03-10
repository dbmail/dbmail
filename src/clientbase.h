/*
 Copyright (c) 2004-2010 NFG Net Facilities Group BV support@nfg.nl

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
 
clientbase_t * client_init(client_sock *c);

void ci_cork(clientbase_t *);
void ci_uncork(clientbase_t *);

int ci_starttls(clientbase_t *);

void ci_authlog_init(clientbase_t *, const char *, const char *, const char *);
void ci_write_cb(clientbase_t *);
int ci_write(clientbase_t *, char *, ...);

void ci_read_cb(clientbase_t *);
int ci_read(clientbase_t *, char *, size_t);
int ci_readln(clientbase_t *, char *);

void ci_close(clientbase_t *);

#endif
