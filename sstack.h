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

/* $Id$
 * (c) 2000-2002 IC&S, The Netherlands
 *
 * sstack.h
 *
 * implementation of a simple stack; memory allocation
 * for the elements is NOT implemented
 */

#ifndef SSTACK_H
#define SSTACK_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef NULL
#define NULL 0
#endif


typedef struct
{
  int type;
  unsigned size;
  void *data;
} stack_elem_t;

typedef struct 
{
  unsigned max_size;
  unsigned sp;
  stack_elem_t **data;
} stack_t;


int           StackAlloc(stack_t *s, unsigned max);
void          StackFree(stack_t *s);
void          StackReset(stack_t *s);
stack_elem_t* StackPop(stack_t *s) ;
int           StackPush(stack_t *s, stack_elem_t *d);
stack_elem_t* StackGet(stack_t *s, unsigned idx);
stack_elem_t* StackTop(stack_t *s);
unsigned      StackSize(stack_t *s);
void          StackReverse(stack_t *s);
  
#endif

