/*
  
 Copyright (c) 2004-2012 NFG Net Facilities Group BV support@nfg.nl

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

#ifndef DM_CACHE_H
#define DM_CACHE_H

#include "dm_stream.h"

#define T Cache_T

typedef struct T *T;

extern T        Cache_new(void);
extern void     Cache_clear(T C, uint64_t message_idnr);
extern uint64_t Cache_update(T C, DbmailMessage *message);
extern uint64_t Cache_get_size(T C, uint64_t message_idnr);
extern void     Cache_get_mem(T C, uint64_t message_idnr, Stream_T);
extern void     Cache_unref_mem(T C, uint64_t message_idnr, Stream_T *);
extern void     Cache_free(T *C);

#undef T
#endif
