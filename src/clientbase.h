/*
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
 */

#ifndef DM_CLIENTBASE_H
#define DM_CLIENTBASE_H

#include "dbmail.h"
 
ClientBase_T * client_init(client_sock *);

int    ci_starttls(ClientBase_T *);
void   ci_cork(ClientBase_T *);
void   ci_uncork(ClientBase_T *);
void   ci_authlog_init(ClientBase_T *, const char *, const char *, const char *);

void   ci_read_cb(ClientBase_T *);
void   ci_write_cb(ClientBase_T *);

int    ci_read(ClientBase_T *, char *, size_t);
int    ci_readln(ClientBase_T *, char *);
int    ci_write(ClientBase_T *, char *, ...);

size_t ci_wbuf_len(ClientBase_T *);

void   ci_close(ClientBase_T *);

#endif
