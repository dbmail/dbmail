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

