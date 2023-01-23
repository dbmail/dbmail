/*
 Copyright (C) 2004-2013 NFG Net Facilities Group BV, support@nfg.nl
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


#ifndef  DM_REQUEST_H
#define  DM_REQUEST_H

#include "dbmail.h"

#define T Request_T
typedef struct T *T;

extern T        Request_new(struct evhttp_request *, void *);
extern void     Request_cb(struct evhttp_request *, void *);
extern void     Request_send(T, int, const char *, struct evbuffer *);
extern void     Request_error(T, int, const char *);
extern void     Request_header(T, const char *, const char *);
extern void     Request_setContentType(T, const char *);
extern void     Request_free(T *);

extern uint64_t              Request_getUser(T R);
extern const char *       Request_getController(T);
extern const char *       Request_getId(T);
extern const char *       Request_getMethod(T);
extern const char *       Request_getArg(T);
extern struct evkeyvalq * Request_getPOST(T);
extern struct evkeyvalq * Request_getGET(T);


#undef T

#endif

