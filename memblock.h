/*
 Copyright (C) 1999-2003 IC & S  dbmail@ic-s.nl

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
 * memblock.h
 *
 * definitions & declarations for an interface that makes function calls
 * similar to those using FILE*'s possible but now no files but memory blocks
 * are used.
 */

#ifndef _MEMBLOCK_H
#define _MEMBLOCK_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define _MEMBLOCK_SIZE (512ul*1024ul)

struct memblock
{
  char data[_MEMBLOCK_SIZE];
  struct memblock *nextblk,*prevblk;
};

typedef struct memblock memblock_t;

struct MEM_TYPE
{
  int nblocks;
  long mpos,eom; /* eom = end-of-mem; these positions are relative to
		  * currblk (mpos) and lastblk (eom)
		  */ 

  memblock_t *firstblk,*currblk,*lastblk;
};
  
typedef struct MEM_TYPE MEM;

MEM* mopen(void);
void mclose(MEM **m);
int  mwrite(const void *data, int size, MEM *m);
int  mread(void *data, int size, MEM *m);
int  mseek(MEM *m, long offset, int whence);
long mtell(MEM *m);
void mrewind(MEM *m);
void mreset(MEM *m);
char* merror(void);

#endif
