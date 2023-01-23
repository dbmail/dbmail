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

/* 
 * ADT interface for Strings allocated from Memory Pool
 *
 */


#ifndef DM_STRING_H
#define DM_STRING_H

#include <stdint.h>
#include "dm_mempool.h"

typedef struct String_T *String_T;

extern String_T        p_string_new(Mempool_T, const char *);
extern String_T        p_string_assign(String_T, const char *);
extern void            p_string_printf(String_T, const char *, ...);
extern void            p_string_append_printf(String_T, const char *, ...);
extern void            p_string_append_vprintf(String_T, const char *, va_list);
extern void            p_string_append_len(String_T, const char *, size_t);
extern String_T        p_string_erase(String_T, size_t, int);
extern String_T        p_string_truncate(String_T, size_t);
extern uint64_t        p_string_len(String_T);
extern const char *    p_string_str(String_T);
extern void            p_string_unescape(String_T);
extern char *          p_string_free(String_T, gboolean);
extern char *          p_trim(char *str, const char *seps);

#define p_string_append(S, s) p_string_append_len(S, s, strlen(s))

#endif
