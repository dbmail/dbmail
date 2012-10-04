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

#define THIS_MODULE "mempool"

#define T Mempool_T

#define NEW(x) x = g_malloc0( sizeof(*x) )
#define M_LOCK(a) if (pthread_mutex_lock(&(a))) { perror("pthread_mutex_lock failed"); }
#define M_UNLOCK(a) if (pthread_mutex_unlock(&(a))) { perror("pthread_mutex_unlock failed"); }

struct T {
	pthread_mutex_t lock;
	size_t size;
	size_t blocksize;
	GAsyncQueue *queue;
};


T mempool_new(size_t size, size_t blocksize)
{
	T M;
	assert(size > 0);
	void *data;
	NEW(M);

	if (pthread_mutex_init(&M->lock, NULL)) {
		perror("pthread_mutex_init failed");
		g_free(M);
		return NULL;
	}

	M->size = size;
	M->blocksize = blocksize;
	M->queue = g_async_queue_new();
	for (size_t i=0; i<M->size; i++) {
		data = g_malloc0(blocksize);
		g_async_queue_push(M->queue, data);
	}
	return M;
}

void * mempool_pop(T M)
{
	void *data = g_async_queue_try_pop(M->queue);
	if (! data) {
		M_LOCK(M->lock);
		for (size_t i = 0; i<M->size; i++) {
			data = g_malloc0(M->blocksize);
			g_async_queue_push(M->queue, data);
		}
		M->size *= 2;
		M_UNLOCK(M->lock);
		data = g_async_queue_pop(M->queue);
	}
	return data;
}

void mempool_push(T M, void *block)
{
	g_async_queue_push(M->queue, block);
}

void mempool_free(T *M)
{
	T p = *M;
	void *data;
	M_LOCK(p->lock);
	data = g_async_queue_try_pop(p->queue);
	while (data) {
		g_free(data);
		data = g_async_queue_try_pop(p->queue);
	}
	g_async_queue_unref(p->queue);
	M_UNLOCK(p->lock);
	pthread_mutex_destroy(&p->lock);
	g_free(p);
	p = NULL;
}


#undef T
