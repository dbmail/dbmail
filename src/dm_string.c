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

#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <glib.h>

#include "dm_string.h"
#include "dm_debug.h"

#define THIS_MODULE "String"

/*
 * implements the Sorted Set interface using GTree
 */

#define T String_T

#define SIZE(s) (sizeof(char) * (s->len + 1))

#define FREE(s) mempool_push(s->pool, s->str, SIZE(s))

struct T {
	Mempool_T pool;
	char *str;
	size_t len;
	size_t used;
};

#define STRLEN 255
static inline void append(T S, const char *s, va_list ap)
{
	va_list ap_copy;
	size_t oldsize;

	while (true) {
		va_copy(ap_copy, ap);
		int n = vsnprintf((char *)(S->str + S->used), S->len - S->used, s, ap_copy);
		va_end(ap_copy);
		if ((S->used + n) < S->len) {
			S->used += n;
			break;
		}
		oldsize = SIZE(S);
		S->len += STRLEN + n;
		S->str = mempool_resize(S->pool, S->str, oldsize, SIZE(S));
	}
}

T p_string_new(Mempool_T pool, const char * s)
{
	T S;
	assert(pool);
	assert(s);
	size_t l = strlen(s);
	S = mempool_pop(pool, sizeof(*S));
	S->pool = pool;
	S->len = l;
	S->str = (char *)mempool_pop(S->pool, SIZE(S));
	memcpy(S->str, s, l);
	S->used = l;
	return S;
}

T p_string_assign(T S, const char *s)
{
	size_t oldsize, newsize;
	size_t l = strlen(s);
	S->used = 0;
	memset(S->str, 0, SIZE(S));
	oldsize = SIZE(S);
	newsize = (sizeof(char) * (l + 1));
	if (newsize > oldsize) {
		S->len = l;
		S->str = mempool_resize(S->pool, S->str, oldsize, SIZE(S));
	}
	memset(S->str, 0, SIZE(S));
	memcpy(S->str, s, l);
	S->used = l;
	return S;
}

void  p_string_printf(T S, const char * s, ...)
{
	S->used = 0;
	memset(S->str, 0, SIZE(S));
	va_list ap;
	va_start(ap, s);
	append(S, s, ap);
	va_end(ap);
}

void p_string_append_printf(T S, const char *s, ...)
{
	va_list ap;
	va_start(ap, s);
	append(S, s, ap);
	va_end(ap);
}

void p_string_append_vprintf(T S, const char *s, va_list ap)
{
	va_list ap_copy;
	va_copy(ap_copy, ap);
	append(S, s, ap_copy);
	va_end(ap_copy);
}

void p_string_append_len(T S, const char *s, size_t l)
{
	if ((S->used + l) > S->len) {
		size_t oldsize = SIZE(S);
		S->len += l;
		S->str = mempool_resize(S->pool, S->str, oldsize, SIZE(S));
		assert(S->str);
	}
	char *dest = S->str;
	dest += S->used;
	memcpy(dest, s, l);
	S->used += l;
	S->str[S->used] = '\0';
}

uint64_t p_string_len(T S)
{
	return S->used;
}

const char * p_string_str(T S)
{
	return S->str;
}

T p_string_erase(T S, size_t pos, int len)
{
	assert(S);
	assert(pos <= S->used);

	if (len < 0)
		len = S->used - pos;
	else {
		assert (pos + len <= S->used);

		if (pos + len < S->used)
			memmove (S->str + pos, S->str + pos + len, S->used - (pos + len));
	}

	S->used -= len;

	S->str[S->used] = 0;

	return S;
}

T p_string_truncate(T S, size_t l)
{
	if (l >= S->used)
		return S;
	S->str[l] = '\0';
	S->used = l;
	return S;
}

void p_string_unescape(T S)
{
	char *s = S->str;
	char *head = s, *this = s, *next = s;
	char found_escape = 0;
	while (*this) {
		next = this+1;
		if (!found_escape && *this && *next && (*this == '\\') && (*next == '"' || *next == '\\')) {
			found_escape = 1;
			S->used--;
			this++;
			continue;
		}
		found_escape = 0;
		*head++ = *this++;
	}
	*head = 0;
}

char * p_string_free(T S, gboolean free_block)
{
	char *s = NULL;
	Mempool_T pool = S->pool;
	if (free_block) {
		FREE(S);
	} else {
		s = S->str;
	}
	mempool_push(pool, S, sizeof(*S));
	return s;
}


char *p_ltrim(char *str, const char *seps)
{
    size_t totrim;
    if (seps == NULL) {
        seps = "\t\n\v\f\r ";
    }
    totrim = strspn(str, seps);
    if (totrim > 0) {
        size_t len = strlen(str);
        if (totrim == len) {
            str[0] = '\0';
        }
        else {
            memmove(str, str + totrim, len + 1 - totrim);
        }
    }
    return str;
}

char *p_rtrim(char *str, const char *seps)
{
    int i;
    if (seps == NULL) {
        seps = "\t\n\v\f\r ";
    }
    i = strlen(str) - 1;
    while (i >= 0 && strchr(seps, str[i]) != NULL) {
        str[i] = '\0';
        i--;
    }
    return str;
}

char *p_trim(char *str, const char *seps)
{
    return p_ltrim(p_rtrim(str, seps), seps);
}

#undef T
