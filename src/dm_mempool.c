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

#include "dbmail.h"
#include "dm_mempool.h"
#include "mpool/mpool.h"

#define THIS_MODULE "mempool"

#define M Mempool_T

#define NEW(x) x = g_malloc0( sizeof(*x) )

struct M {
	mpool_t *pool;
};

M mempool_init(void)
{
	M MP;
	mpool_t *mp = mpool_open(0,0,0,NULL);
	MP = mpool_calloc(mp, 1, sizeof(*MP), NULL);
	MP->pool = mp;
	return MP;
}

void * mempool_pop(M MP, size_t blocksize)
{
	return mpool_alloc(MP->pool, blocksize, NULL);
}

void mempool_push(M MP, void *block, size_t blocksize)
{
	mpool_free(MP->pool, block, blocksize);
}

void mempool_close(M *MP) 
{
	M m = *MP;
	mpool_t *mp = m->pool;
	mpool_close(mp);
	mp = NULL;
}

#undef M
#undef NEW
