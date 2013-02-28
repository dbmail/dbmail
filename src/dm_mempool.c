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
#define POOL_LOCK(a) if (pthread_mutex_lock(&(a))) { perror("pthread_mutex_lock failed"); }
#define POOL_UNLOCK(a) if (pthread_mutex_unlock(&(a))) { perror("pthread_mutex_unlock failed"); }

#define fail_unless(a) assert(a)

struct M {
	pthread_mutex_t lock;
	mpool_t *pool;
};

M mempool_open(void)
{
	M MP;
	mpool_t *pool = NULL;
	static gboolean env_mpool = FALSE;
	static gboolean use_mpool = FALSE;

	if (! env_mpool) {
		char *dm_pool = getenv("DM_POOL");
		if (MATCH(dm_pool, "yes"))
			use_mpool = FALSE;
		env_mpool = TRUE;
	}
	if (use_mpool)
		pool = mpool_open(0,0,0,NULL);
	else
		pool = NULL;

	MP = mpool_alloc(pool, sizeof(*MP), NULL);
	
	if (pthread_mutex_init(&MP->lock, NULL)) {
		perror("pthread_mutex_init failed");
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
	POOL_LOCK(MP->lock);
	void *block = mpool_calloc(MP->pool, 1, blocksize, &error);
	POOL_UNLOCK(MP->lock);
	if (error != MPOOL_ERROR_NONE)
		TRACE(TRACE_ERR, "%s", mpool_strerror(error));
	return block;
}

void * mempool_resize(M MP, void *block, size_t oldsize, size_t newsize)
{
	int error;
	POOL_LOCK(MP->lock);
	void *newblock = mpool_resize(MP->pool, block, oldsize, newsize, &error);
	POOL_UNLOCK(MP->lock);
	if (error != MPOOL_ERROR_NONE)
		TRACE(TRACE_ERR, "%s", mpool_strerror(error));
	assert (error == MPOOL_ERROR_NONE);
	return newblock;
}

void mempool_push(M MP, void *block, size_t blocksize)
{
	int error;
	POOL_LOCK(MP->lock);
	if ((error = mpool_free(MP->pool, block, blocksize)) != MPOOL_ERROR_NONE)
		TRACE(TRACE_ERR, "%s", mpool_strerror(error));

	fail_unless(error == MPOOL_ERROR_NONE);
	POOL_UNLOCK(MP->lock);
}

void mempool_stats(M MP)
{
	unsigned int page_size;
        unsigned long num_alloced, user_alloced, max_alloced, tot_alloced;
	mpool_stats(MP->pool, &page_size, &num_alloced, &user_alloced,
			&max_alloced, &tot_alloced);
	TRACE(TRACE_DEBUG, "[%p] page_size: %u num: %lu user: %lu "
			"max: %lu tot: %lu", MP->pool, 
			page_size, num_alloced, user_alloced,
			max_alloced, tot_alloced);
}

void mempool_close(M *MP) 
{
	int error;
	M mp = *MP;
	pthread_mutex_t lock = mp->lock;
	POOL_LOCK(lock);
	mpool_t *pool = mp->pool;
	if (pool) {
		mempool_stats(mp);
		if ((error = mpool_close(pool)) != MPOOL_ERROR_NONE)
			TRACE(TRACE_ERR, "%s", mpool_strerror(error));
	} else {
		free(mp);
	}
	POOL_UNLOCK(lock);
	pthread_mutex_destroy(&lock);
	mp = NULL;
}

#undef M
