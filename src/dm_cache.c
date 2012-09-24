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
#include "dm_cache.h"
#include <assert.h>
#include <sys/queue.h>
#include <pthread.h>

#define THIS_MODULE "Cache"
/*
 * cached raw message data
 *
 * implement a global message cache as a LIST of Mem_T objects
 * with reference bookkeeping and TTL
 *
 */
#define T Cache_T

#define TTL_SECONDS 30
#define GC_INTERVAL 10

#define CACHE_LOCK(a) if (pthread_mutex_lock(&(a))) { perror("pthread_mutex_lock failed"); }
#define CACHE_UNLOCK(a) if (pthread_mutex_unlock(&(a))) { perror("pthread_mutex_unlock failed"); }

LIST_HEAD(listhead, element) head;
struct listhead *headp;
struct element {
	uint64_t id;
	long ttl;
	uint64_t ref;
	uint64_t size;
	uint64_t header_size;
	Mem_T mem;
	LIST_ENTRY(element) elements;
};

struct T {
	pthread_mutex_t lock;
	pthread_cond_t cond;
	struct listhead elements;
	volatile int done; // exit flag
};

pthread_t gc_thread_id;

static void * _gc_callback(void *data)
{
	TRACE(TRACE_DEBUG, "start Cache GC thread. sweep interval [%d]",
			GC_INTERVAL);
	T C = (T)data;
	while (1) {
		CACHE_LOCK(C->lock);
		if (C->done) {
			CACHE_UNLOCK(C->lock);
			return NULL;
		}
		CACHE_UNLOCK(C->lock);
		Cache_gc(C);
		sleep(GC_INTERVAL);
	}
	return NULL;
}

/* */
T Cache_new(void)
{
	T C;
	
	C = (T)calloc(1, sizeof(*C));
	assert(C);

	if (pthread_mutex_init(&C->lock, NULL)) {
		perror("pthread_mutex_init failed");
		free(C);
		return NULL;
	}
	if (pthread_cond_init(&C->cond, NULL)) {
		perror("pthread_cond_init failed");
		free(C);
		return NULL;
	}

	LIST_INIT(&C->elements);

	if (pthread_create(&gc_thread_id, NULL, _gc_callback, C)) {
		perror("GC thread create failed");
		free(C);
		return NULL;
	}

	return C;
}

struct element * Cache_find(T C, uint64_t id)
{
	assert(C);
	struct element *E;
	E = LIST_FIRST(&C->elements);
	while (E) {
		if (E->id == id) return E;
		E = LIST_NEXT(E, elements);
	}
	return NULL;
}



void Cache_clear(T C, uint64_t id)
{
	struct element *E;
	CACHE_LOCK(C->lock);
	if (! (E = Cache_find(C, id))) {
		CACHE_UNLOCK(C->lock);
		return;
	}

	LIST_REMOVE(E, elements);
	CACHE_UNLOCK(C->lock);
	Mem_close(&E->mem);
	free(E);
}

uint64_t Cache_update(T C, DbmailMessage *message)
{
	uint64_t outcnt = 0;
	char *crlf = NULL;
	struct element *E;
	time_t now = time(NULL);

	TRACE(TRACE_DEBUG, "message [%lu]", message->id);
	CACHE_LOCK(C->lock);
	E = Cache_find(C, message->id);
	if (E) {
		uint64_t size = E->size;
		E->ttl = now + TTL_SECONDS;
		CACHE_UNLOCK(C->lock);
		return size;
	}

	crlf = get_crlf_encoded(message->raw_content);
	E = (struct element *)calloc(1, sizeof(struct element));
	assert(E);

	outcnt = strlen(crlf);

	E->ref = 0;
	E->id = message->id;
	E->size = outcnt;
	E->ttl = now + TTL_SECONDS;
	E->mem = Mem_open();

	Mem_rewind(E->mem);
	Mem_write(E->mem, crlf, outcnt);
	Mem_rewind(E->mem);

	LIST_INSERT_HEAD(&C->elements, E, elements);
	CACHE_UNLOCK(C->lock);

	g_free(crlf);
	return outcnt;
}

uint64_t Cache_get_size(T C, uint64_t id)
{
	uint64_t size;
	struct element *E;
	assert(C);
	CACHE_LOCK(C->lock);
	E = Cache_find(C, id);
	if (! E) {
		CACHE_UNLOCK(C->lock);
		return 0;
	}
	size = E->size;
	CACHE_UNLOCK(C->lock);
	return size;
}

void Cache_get_mem(T C, uint64_t id, Mem_T M)
{
	time_t now = time(NULL);
	struct element *E;
	assert(C);
	CACHE_LOCK(C->lock);
	E = Cache_find(C, id);
	assert(E);
	E->ref++;
	E->ttl = now + TTL_SECONDS;
	Mem_ref(E->mem, M);
	CACHE_UNLOCK(C->lock);
}

void Cache_unref_mem(T C, uint64_t id, Mem_T *M)
{
	Mem_T m = *M;
	struct element *E;
	assert(C);
	assert(m);
	CACHE_LOCK(C->lock);
	E = Cache_find(C, id);
	assert(E);
	E->ref--;
	CACHE_UNLOCK(C->lock);
	g_free(m);
	m = NULL;
}

/*
 * garbage collection: sweep the cache for unref-ed 
 * or expired objects
 */

void Cache_gc(T C)
{
	assert(C);
	struct element *E;
	time_t now = time(NULL);
	CACHE_LOCK(C->lock);
	E = LIST_FIRST(&C->elements);
	while (E) {
		if (E->ttl < now && E->ref <= 0) {
		       	LIST_REMOVE(E, elements);
			Mem_close(&E->mem);
			free(E);
		}
		E = LIST_NEXT(E, elements);
	}
	CACHE_UNLOCK(C->lock);
}
/*
 * closes the msg cache
 */
void Cache_free(T *C)
{
	struct element *E;
	T c = *C;
	CACHE_LOCK(c->lock);
	while (! LIST_EMPTY(&c->elements)) {
		E = LIST_FIRST(&c->elements);
		LIST_REMOVE(E, elements);
		Mem_close(&E->mem);
		free(E);
	}
	CACHE_UNLOCK(c->lock);
	pthread_mutex_destroy(&c->lock);
	pthread_cond_destroy(&c->cond);
	g_free(c);
	c = NULL;
	
}

#undef T
#undef CACHE_LOCK
#undef CACHE_UNLOCK

