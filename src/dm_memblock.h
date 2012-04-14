 /*

 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
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

/*
 * dm_memblock.h
 *
 * definitions & declarations for an interface that makes function calls
 * similar to those using FILE*'s possible but now no files but memory blocks
 * are used.
 */

#ifndef DM_MEMBLOCK_H
#define DM_MEMBLOCK_H

#define T Mem_T
typedef struct T *T;

extern T Mem_open(void);

extern void Mem_close  (T *M);
extern int  Mem_write  (T M, const void *data, int size);
extern int  Mem_read   (T M, void *data, int size);
extern int  Mem_seek   (T M, long offset, int whence);
extern void Mem_rewind (T M);

#undef T

#endif
