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
#define P_LOCK(a) if (pthread_mutex_lock(&(a))) { perror("pthread_mutex_lock failed"); }
#define P_UNLOCK(a) if (pthread_mutex_unlock(&(a))) { perror("pthread_mutex_unlock failed"); }

struct T {
	pthread_mutex_t lock;
	size_t size;
	size_t blocksize;
	GAsyncQueue *queue;
};


T mempool_new(size_t size, size_t blocksize)
{
	T P;
	assert(size > 0);
	void *data;
	NEW(P);

	if (pthread_mutex_init(&P->lock, NULL)) {
		perror("pthread_mutex_init failed");
		g_free(P);
		return NULL;
	}

	P->size = size;
	P->blocksize = blocksize;
	P->queue = g_async_queue_new();
	for (size_t i=0; i<P->size; i++) {
		data = g_malloc0(blocksize);
		g_async_queue_push(P->queue, data);
	}
	return P;
}

void * mempool_pop(T P)
{
	void *data = g_async_queue_try_pop(P->queue);
	if (! data) {
		P_LOCK(P->lock);
		for (size_t i = 0; i<P->size; i++) {
			data = g_malloc0(P->blocksize);
			g_async_queue_push(P->queue, data);
		}
		P->size *= 2;
		P_UNLOCK(P->lock);
		data = g_async_queue_pop(P->queue);
	}
	return data;
}

void mempool_push(T P, void *block)
{
	assert(sizeof(block) == P->blocksize);
	g_async_queue_push(P->queue, block);
}

void mempool_free(T *P)
{
	T p = *P;
	void *data;
	P_LOCK(p->lock);
	data = g_async_queue_try_pop(p->queue);
	while (data) {
		g_free(data);
		data = g_async_queue_try_pop(p->queue);
	}
	g_async_queue_unref(p->queue);
	P_UNLOCK(p->lock);
	pthread_mutex_destroy(&p->lock);
	g_free(p);
	p = NULL;
}


#undef T
