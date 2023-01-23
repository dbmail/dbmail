/*
 Copyright (c) 2004-2013 NFG Net Facilities Group BV support@nfg.nl
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

#include "dbmail.h"
#include "dm_mempool.h"
#include "mpool/mpool.h"

#define THIS_MODULE "mempool"

#define M Mempool_T

#define fail_unless(a) assert(a)

struct M {
	pthread_mutex_t lock;
	mpool_t *pool;
};

M mempool_open(void)
{
	M MP;
	mpool_t *pool = NULL;
	static gboolean env_mpool = false;
	static gboolean use_mpool = false;

	if (! env_mpool) {
		char *dm_pool = getenv("DM_POOL");
		if (MATCH(dm_pool, "yes"))
			use_mpool = true;
		env_mpool = true;
	}
	if (use_mpool)
		pool = mpool_open(0,0,0,NULL);
	else
		pool = NULL;

	MP = mpool_alloc(pool, sizeof(*MP), NULL);
	
	if (pthread_mutex_init(&MP->lock, NULL)) {
		perror("pthread_mutex_init failed");
		mpool_free(pool, MP, sizeof(*MP));
		if (pool)
			mpool_close(pool);
		return NULL;
	}

	MP->pool = pool;
	return MP;
}

void * mempool_pop(M MP, size_t blocksize)
{
	int error;
	PLOCK(MP->lock);
	void *block = mpool_calloc(MP->pool, 1, blocksize, &error);
	PUNLOCK(MP->lock);
	if (error != MPOOL_ERROR_NONE)
		TRACE(TRACE_ERR, "%s", mpool_strerror(error));
	return block;
}

void * mempool_resize(M MP, void *block, size_t oldsize, size_t newsize)
{
	int error;
	PLOCK(MP->lock);
	void *newblock = mpool_resize(MP->pool, block, oldsize, newsize, &error);
	PUNLOCK(MP->lock);
	if (error != MPOOL_ERROR_NONE)
		TRACE(TRACE_ERR, "%s", mpool_strerror(error));
	assert (error == MPOOL_ERROR_NONE);
	return newblock;
}

void mempool_push(M MP, void *block, size_t blocksize)
{
	int error;
	PLOCK(MP->lock);
	if ((error = mpool_free(MP->pool, block, blocksize)) != MPOOL_ERROR_NONE)
		TRACE(TRACE_ERR, "%s", mpool_strerror(error));

	fail_unless(error == MPOOL_ERROR_NONE);
	PUNLOCK(MP->lock);
}

void mempool_stats(M MP)
{
	unsigned int page_size;
        unsigned long num_alloced, user_alloced, max_alloced, tot_alloced;
	mpool_stats(MP->pool, &page_size, &num_alloced, &user_alloced,
			&max_alloced, &tot_alloced);
	TRACE(TRACE_DEBUG, "[%p] page_size: %u num: %" PRIu64 " user: %" PRIu64 " "
			"max: %" PRIu64 " tot: %" PRIu64 "", MP->pool, 
			page_size, (uint64_t)num_alloced, (uint64_t)user_alloced,
			(uint64_t)max_alloced, (uint64_t)tot_alloced);
}

void mempool_close(M *MP) 
{
	int error;
	M mp = *MP;
	pthread_mutex_t lock = mp->lock;
	PLOCK(lock);
	mpool_t *pool = mp->pool;
	if (pool) {
		mempool_stats(mp);
		if ((error = mpool_close(pool)) != MPOOL_ERROR_NONE)
			TRACE(TRACE_ERR, "%s", mpool_strerror(error));
	} else {
		free(mp);
	}
	PUNLOCK(lock);
	pthread_mutex_destroy(&lock);
	mp = NULL;
}

#undef M
