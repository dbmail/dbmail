 /*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2004-2009 NFG Net Facilities Group BV support@nfg.nl

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
 * memblock.c
 *
 * implementations of functions declared in memblock.h
 */

#include <glib.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include "dm_memblock.h"

#define MEMBLOCK_SIZE (512ul*1024ul)
#define MAX_ERROR_SIZE 128

#define T Mem_T

#define NEW(x) x = g_malloc0( sizeof(*x) )

struct MemBlock_t {
	struct MemBlock_t *nextblk, *prevblk;
	char data[MEMBLOCK_SIZE];
};

typedef struct MemBlock_t *MemBlock_t;

struct T {
	int nblocks;
	long eom;		/* eom = end-of-mem; relative to lastblk */
	long mpos;		/* mpos = mem-position; relative to currblk */
	MemBlock_t firstblk, currblk, lastblk;
};

/* internal use only */
static int Mem_grow(T M);

/*
 * mopen()
 *
 * opens a mem-structure
 */
T Mem_open()
{
	T M;
	MemBlock_t B;

	NEW(M);
	NEW(B);

	M->firstblk = B;
	M->lastblk = B;
	M->currblk = B;
	M->nblocks = 1;

	return M;
}


/*
 * mclose()
 *
 * closes a mem structure
 *
 */
void Mem_close(T *M)
{
	assert(M && *M);
	MemBlock_t tmp, next;

	tmp = (*M)->firstblk;
	while (tmp) {
		next = tmp->nextblk;	/* save address */
		g_free(tmp);
		tmp = next;
	}

	g_free(*M);
	*M = NULL;

	return;
}


/*
 * mwrite()
 *
 * writes size bytes of data to the memory associated with m
 */
int Mem_write(T M, const void *data, int size)
{
	long left;

	assert(M);
	assert(data);
	assert(size > 0);

	left = MEMBLOCK_SIZE - M->mpos;

	if (size <= left) {
		/* entire fit */
		memmove(&M->currblk->data[M->mpos], data, size);
		M->mpos += size;

		if (size == left) {
			/* update */
			if (M->currblk == M->lastblk) {
				if (!Mem_grow(M)) {
					M->mpos--;
					M->eom = M->mpos;
					return size - 1;
				}
			}

			M->currblk = M->currblk->nextblk;
			M->mpos = 0;
		}

		if (M->currblk == M->lastblk && M->mpos > M->eom)
			M->eom = M->mpos;

		return size;
	}

	/* copy everything that can be placed */
	memmove(&M->currblk->data[M->mpos], data, left);
	M->mpos += left;

	if (M->currblk == M->lastblk) {
		/* need a new block */
		if (!Mem_grow(M))
			return left;

		M->eom = 0;
	}

	M->currblk = M->currblk->nextblk;	/* advance current block */
	M->mpos = 0;

	return left + Mem_write(M, &((char *) data)[left], size - left);
}


/*
 * mread()
 *
 * reads up to size bytes from m into data
 *
 * returns the number of bytes actually read
 */
int Mem_read(T M, void *data, int size)
{
	long left;
	assert(M);
	assert(data);
	assert(size >=0);

	if (M->lastblk == M->currblk)
		left = M->eom - M->mpos;
	else
		left = MEMBLOCK_SIZE - M->mpos;

	if (left <= 0) return 0;

	if (size < left) {
		/* entire fit */
		memmove(data, &M->currblk->data[M->mpos], size);
		M->mpos += size;

		return size;
	}

	/* copy everything that can be placed */
	memmove(data, &M->currblk->data[M->mpos], left);
	M->mpos += left;

	if (M->currblk == M->lastblk) {
		/* no more data */
		return left;
	}

	M->currblk = M->currblk->nextblk;	/* advance current block */
	M->mpos = 0;

	return left + Mem_read(M, &((char *) data)[left], size - left);
}


/*
 * mseek()
 *
 * moves the current pos in m offset bytes according to whence:
 * SEEK_SET seek from the beginning
 * SEEK_CUR seek from the current pos
 * SEEK_END seek from the end
 *
 * returns 0 on succes, -1 on error
 */
int Mem_seek(T M, long offset, int whence)
{
	long left;

	assert(M);

	switch (whence) {
	case SEEK_SET:
		M->currblk = M->firstblk;
		M->mpos = 0;

		if (offset <= 0) return 0;

		return Mem_seek(M, offset, SEEK_CUR);

	case SEEK_CUR:
		if (offset == 0) return 0;

		if (offset > 0) {
			left = MEMBLOCK_SIZE - M->mpos;
			if (offset >= left) {
				if (M->currblk == M->lastblk) {
					M->mpos = M->eom;
					return 0;
				}

				M->currblk = M->currblk->nextblk;
				M->mpos = 0;
				return Mem_seek(M, offset - left, SEEK_CUR);
			} else {
				M->mpos += offset;

				if (M->currblk == M->lastblk && M->mpos > M->eom)
					M->mpos = M->eom;

				return 0;
			}
		} else {
			/* offset < 0, walk backwards */
			left = -M->mpos;

			if (offset <= left) {
				if (M->currblk == M->firstblk) {
					M->mpos = 0;
					return 0;
				}

				M->currblk = M->currblk->prevblk;
				M->mpos = MEMBLOCK_SIZE;
				return Mem_seek(M, offset - left, SEEK_CUR);
			} else {
				M->mpos += offset;	/* remember: offset<0 */
				return 0;
			}
		}

	case SEEK_END:
		M->currblk = M->lastblk;
		M->mpos = M->eom;

		if (offset >= 0)
			return 0;

		return Mem_seek(M, offset, SEEK_CUR);

	default:
		return -1;
	}

	return 0;
}


/*
 * mrewind()
 *
 * equivalent to mseek(m, 0, SEEK_SET)
 */
void Mem_rewind(T M)
{
	Mem_seek(M, 0, SEEK_SET);
}


/* 
 * Mem_grow()
 * adds a block to m
 * returns 0 on failure, 1 on succes
 */
int Mem_grow(T M)
{
	MemBlock_t B;

	assert(M);

	NEW(B);

	B->prevblk = M->lastblk;
	M->lastblk->nextblk = B;
	M->lastblk = B;
	M->nblocks++;

	return 1;
}

