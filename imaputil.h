/*
 Copyright (C) 1999-2004 IC & S  dbmail@ic-s.nl

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

/* $Id$
 * imaputil.h
 *
 * utility functions for IMAP server
 */

#ifndef _IMAPUTIL_H
#define _IMAPUTIL_H

#include "dbmail.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "imap4.h"
#include "db.h"
#include "memblock.h"
#include "dbmailtypes.h"
#include <stdio.h>

int retrieve_structure(FILE * outstream, mime_message_t * msg,
		       int show_extension_data);
int retrieve_envelope(FILE * outstream, struct dm_list *rfcheader);
int show_address_list(FILE * outstream, struct mime_record *mr);
int show_mime_parameter_list(FILE * outstream, struct mime_record *mr,
			     int force_subtype, int only_extension);

mime_message_t *get_part_by_num(mime_message_t * msg, const char *part);

u64_t rfcheader_dump(MEM * outmem, struct dm_list *rfcheader,
		     char **fieldnames, int nfields, int equal_type);
u64_t mimeheader_dump(MEM * outmem, struct dm_list *mimeheader);

int haystack_find(int haystacklen, char **haystack, const char *needle);

int next_fetch_item(char **args, int idx, fetch_items_t * fi);
int is_textplain(struct dm_list *hdr);

char *date_sql2imap(const char *sqldate);
char *date_imap2sql(const char *imapdate);

size_t stridx(const char *s, char ch);
int checkchars(const char *s);
int checktag(const char *s);
int checkmailboxname(const char *s);
int check_msg_set(const char *s);
int check_date(const char *date);
void clarify_data(char *str);

void base64encode(char *in, char *out);
void base64decode(char *in, char *out);
int binary_search(const u64_t * array, unsigned arraysize, u64_t key,
		  unsigned int *key_idx);


int quoted_string_out(FILE * outstream, const char *s);
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
void dbmail_imap_plist_free(GList *l);

int mime_unwrap(char *to, const char *from); 
int sort_search(struct dm_list *searchlist);


#endif
