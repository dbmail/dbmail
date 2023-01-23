/*
 Copyright (c) 2010-2013 NFG Net Facilities Group BV support@nfg.nl
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
 * ADT interface for Sorted Set
 *
 * optimized for insertion and removal of items to and from the
 * Universe
 */


#ifndef DM_SSET_H
#define DM_SSET_H

#define T Sset_T

typedef struct T *T;

extern T               Sset_new(int (*cmp)(const void *, const void *), size_t, void (*free)(void *));
extern int             Sset_has(T, const void *); 
extern void            Sset_add(T, const void *);
extern int             Sset_len(T);
extern void            Sset_del(T, const void *);
extern void            Sset_map(T, int (*func)(void *, void *), void *);
extern void            Sset_free(T *);

extern T               Sset_or(T, T); // a + b
extern T               Sset_and(T, T); // a * b
extern T               Sset_not(T, T); // a - b
extern T               Sset_xor(T, T); // a / b

#undef T

#endif
