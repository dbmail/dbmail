/* $Id$ 
 * (c) 2000-2002 IC&S, The Netherlands
 *
 * sstack.c
 *
 * function implementations of sstack.
 */

#include "sstack.h"
#include <stdlib.h>


/*
 * alloc memory for STACK (pointers)
 */
int StackAlloc(stack_t *s, unsigned max)
{

  s->data = (stack_elem_t**)malloc(sizeof(stack_elem_t*) * max);

  if (s->data == NULL)
    return 0;

  s->max_size = max;
  s->sp = 0;
  return 1;
}


/*
 * free mem
 */
void StackFree(stack_t *s)
{
  free(s->data);
  s->data = NULL;
  s->sp = 0;
  s->max_size = 0;
}


/*
 * empty stack
 */
void StackReset(stack_t *s) 
{ 
  s->sp = 0; 
}


/*
 * pop
 */
stack_elem_t* StackPop(stack_t *s) 
{ 
  return (s->sp) ? s->data[--(s->sp)] : NULL;
}


/*
 * push
 */
int StackPush(stack_t *s, stack_elem_t *d)
{
  if (s->sp < s->max_size)
    {
      s->data[s->sp++] = d;
      return 1;
    }
  return 0;
}


/*
 * retrieve an element, index idx
 */
stack_elem_t* StackGet(stack_t *s, unsigned idx)
{ 
  return (idx < s->sp) ? s->data[idx] : NULL;
}


/*
 * get top-element 
 */
stack_elem_t* StackTop(stack_t *s)
{ 
  return (s->sp > 0) ? s->data[s->sp-1] : NULL;
}


/*
 * retrieve stacksize
 */
unsigned StackSize(stack_t *s)
{
  return s->sp;
}


/* reverse the stack */
void StackReverse(stack_t *s)
{
  stack_elem_t *tmp;
  unsigned i;

  for (i=0; i<(s->sp/2); i++)
    {
      tmp = s->data[i];
      s->data[i] = s->data[s->sp - i - 1];
      s->data[s->sp - i - 1] = tmp;
    }
}
  

