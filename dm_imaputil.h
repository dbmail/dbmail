/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl
 Copyright (c) 2005-2006 NFG Net Facilities Group BV support@nfg.nl

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

/* $Id: dm_imaputil.h 1878 2005-09-04 06:34:44Z paul $
 * dm_imaputil.h
 *
 * utility functions for IMAP server
 */

#ifndef _IMAPUTIL_H
#define _IMAPUTIL_H

#include "dbmail.h"

int haystack_find(int haystacklen, char **haystack, const char *needle);

int next_fetch_item(char **args, int idx, fetch_items_t * fi);
int is_textplain(struct dm_list *hdr);

size_t stridx(const char *s, char ch);
int checkchars(const char *s);
int checktag(const char *s);
int binary_search(const u64_t * array, unsigned arraysize, u64_t key, unsigned int *key_idx);
void send_data(FILE * to, MEM * from, int cnt);

int build_imap_search(char **search_keys, struct dm_list *sl, int *idx, int sorted);
int perform_imap_search(unsigned int *rset, int setlen, search_key_t * sk,
			mailbox_t * mb, int sorted, int condition);
void free_searchlist(struct dm_list *sl);

void invert_set(unsigned int *set, int setlen);
void combine_sets(unsigned int *dest, unsigned int *sec, int setlen, int type);

void build_set(unsigned int *set, unsigned int setlen, char *cset);
void build_uid_set(unsigned int *set, unsigned int setlen, char *cset,
		   mailbox_t * mb);
void dumpsearch(search_key_t * sk, int level);

int init_cache(void);
void close_cache(void);

char * dbmail_imap_astring_as_string(const char *s);
char * dbmail_imap_plist_as_string(GList *plist);
GList* dbmail_imap_append_alist_as_plist(GList *list, const InternetAddressList *ialist);
char * dbmail_imap_plist_collapse(const char *in);
void dbmail_imap_plist_free(GList *l);

char * imap_get_structure(GMimeMessage *message, gboolean extension);
char * imap_get_envelope(GMimeMessage *message);
GMimeObject * imap_get_partspec(const GMimeObject *message, const char *partspec);
char * imap_get_logical_part(const GMimeObject *object, const char * specifier);

char * imap_message_fetch_headers(u64_t physid, const GList *headers, gboolean not);

char * imap_cleanup_address(const char *a);
	
int mime_unwrap(char *to, const char *from); 
int sort_search(struct dm_list *searchlist);

#endif
