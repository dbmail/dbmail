/*
 * memblock.h
 *
 * definitions & declarations for an interface that makes function calls
 * similar to those using FILE*'s possible but now no files but memory blocks
 * are used.
 */

#ifndef _MEMBLOCK_H
#define _MEMBLOCK_H

#define _MEMBLOCK_SIZE (128ul*1024ul)

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

MEM* mopen();
void mclose(MEM **m);
int  mwrite(const void *data, int size, MEM *m);
int  mread(void *data, int size, MEM *m);
int  mseek(MEM *m, long offset, int whence);
long mtell(MEM *m);
void mrewind(MEM *m);
void mreset(MEM *m);
char* merror();

#endif
