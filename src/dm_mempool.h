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
#ifndef DM_MEMPOOL_H
#define DM_MEMPOOL_H
/*
 * provide simple type specific memory pools of pre-allocated structs
 */
#define T Mempool_T

typedef struct T *T;

extern T      mempool_new(size_t, size_t);
extern void * mempool_pop(T);
extern void   mempool_push(T, void *);
extern void   mempool_free(T *);

#undef T

#endif
