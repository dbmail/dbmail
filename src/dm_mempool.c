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

#define M Mempool_T
#define B Bucket_T

#define NEW(x) x = g_malloc0( sizeof(*x) )
#define M_LOCK(a) if (pthread_mutex_lock(&(a))) { perror("pthread_mutex_lock failed"); }
#define M_UNLOCK(a) if (pthread_mutex_unlock(&(a))) { perror("pthread_mutex_unlock failed"); }

#define INIT_BUCKETSIZE 64

typedef struct B *B;

struct B {
	pthread_mutex_t lock;
	size_t size;
	size_t blocksize;
	GAsyncQueue *queue;
};


B bucket_new(size_t size, size_t blocksize)
{
	B MB;
	assert(size > 0);
	void *data;
	NEW(MB);

	if (pthread_mutex_init(&MB->lock, NULL)) {
		perror("pthread_mutex_init failed");
		g_free(MB);
		return NULL;
	}

	MB->size = size;
	MB->blocksize = blocksize;
	MB->queue = g_async_queue_new();
	for (size_t i=0; i<MB->size; i++) {
		data = g_malloc0(blocksize);
		g_async_queue_push(MB->queue, data);
	}
	return MB;
}

void * bucket_pop(B MB)
{
	void *data = g_async_queue_try_pop(MB->queue);
	if (! data) {
		M_LOCK(MB->lock);
		for (size_t i = 0; i<MB->size; i++) {
			data = g_malloc0(MB->blocksize);
			g_async_queue_push(MB->queue, data);
		}
		MB->size *= 2;
		M_UNLOCK(MB->lock);
		data = g_async_queue_pop(MB->queue);
	}
	return data;
}

void bucket_push(B MB, void *block)
{
	g_async_queue_push(MB->queue, block);
}

void bucket_free(B *MB)
{
	B mb = *MB;
	void *data;
	M_LOCK(mb->lock);
	data = g_async_queue_try_pop(mb->queue);
	while (data) {
		g_free(data);
		data = g_async_queue_try_pop(mb->queue);
	}
	g_async_queue_unref(mb->queue);
	M_UNLOCK(mb->lock);
	pthread_mutex_destroy(&mb->lock);
	g_free(mb);
	mb = NULL;
}

struct M {
	pthread_mutex_t lock;
	GTree *buckets;
};


gint sizecmp(gconstpointer a, gconstpointer b, gpointer UNUSED data)
{
	size_t sa = *(size_t *)a;
	size_t sb = *(size_t *)b;
	return (sa<sb)?-1:((sa>sb)?1:0);
}

M mempool_init(void)
{
	M MP;
	NEW(MP);
	if (pthread_mutex_init(&MP->lock, NULL)) {
		perror("pthread_mutex_init failed");
		g_free(MP);
		return NULL;
	}
	MP->buckets = g_tree_new_full(sizecmp, NULL, g_free, NULL);
	return MP;
}

void * mempool_pop(M MP, size_t blocksize)
{
	M_LOCK(MP->lock);
	B MB = g_tree_lookup(MP->buckets, &blocksize);
	if (MB == NULL) {
		MB = bucket_new(INIT_BUCKETSIZE, blocksize);
		assert(MB);
		size_t *key = g_new0(size_t,1);
		*key = blocksize;
		g_tree_insert(MP->buckets, key, MB);
	}
	M_UNLOCK(MP->lock);
	return bucket_pop(MB);
}

void mempool_push(M MP, void *block, size_t blocksize)
{
	B MB = g_tree_lookup(MP->buckets, &blocksize);
	assert(MB);
	bucket_push(MB, block);
}

static gboolean _bucket_free(gpointer UNUSED key, gpointer value, gpointer UNUSED data)
{
	B MB = (B)value;
	bucket_free(&MB);
	return FALSE;
}

void mempool_close(M *MP) 
{
	M mp = *MP;
	M_LOCK(mp->lock);
	g_tree_foreach(mp->buckets, _bucket_free, NULL);
	g_tree_destroy(mp->buckets);
	mp->buckets = NULL;
	M_UNLOCK(mp->lock);
	pthread_mutex_destroy(&mp->lock);
	g_free(mp);
	mp = NULL;
}

