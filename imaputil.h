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
#include "clientinfo.h"
#include <stdio.h>

int retrieve_structure(FILE * outstream, mime_message_t * msg,
		       int show_extension_data);
int retrieve_envelope(FILE * outstream, struct list *rfcheader);
int show_address_list(FILE * outstream, struct mime_record *mr);
int show_mime_parameter_list(FILE * outstream, struct mime_record *mr,
			     int force_subtype, int only_extension);

mime_message_t *get_part_by_num(mime_message_t * msg, const char *part);

u64_t rfcheader_dump(MEM * outmem, struct list *rfcheader,
		     char **fieldnames, int nfields, int equal_type);
u64_t mimeheader_dump(MEM * outmem, struct list *mimeheader);

int haystack_find(int haystacklen, char **haystack, const char *needle);

int next_fetch_item(char **args, int idx, fetch_items_t * fi);
int is_textplain(struct list *hdr);

char *date_sql2imap(const char *sqldate);
char *date_imap2sql(const char *imapdate);

size_t stridx(const char *s, char ch);
int checkchars(const char *s);
int checktag(const char *s);
int checkmailboxname(const char *s);
int check_msg_set(const char *s);
int check_date(const char *date);
void clarify_data(char *str);
char **build_args_array(const char *s);
char **build_args_array_ext(const char *originalString, clientinfo_t * ci);

void base64encode(char *in, char *out);
void base64decode(char *in, char *out);
int binary_search(const u64_t * array, unsigned arraysize, u64_t key,
		  unsigned int *key_idx);

char **give_chunks(const char *str, char delimiter);
void free_chunks(char **chunks);
int quoted_string_out(FILE * outstream, const char *s);
void send_data(FILE * to, MEM * from, int cnt);

int build_imap_search(char **search_keys, struct list *sl, int *idx, int sorted);
int perform_imap_search(int *rset, int setlen, search_key_t * sk,
			mailbox_t * mb, int sorted);
void free_searchlist(struct list *sl);

void invert_set(int *set, int setlen);
void combine_sets(int *dest, int *sec, int setlen, int type);

void build_set(int *set, unsigned int setlen, char *cset);
void build_uid_set(int *set, unsigned int setlen, char *cset,
		   mailbox_t * mb);
void dumpsearch(search_key_t * sk, int level);

int init_cache(void);
void close_cache(void);

char * dbmail_imap_astring_as_string(const char *s);
char * dbmail_imap_plist_as_string(GList *plist);

GString * g_list_join(GList * list, char * sep);
GList * g_string_split(GString * string, char * sep);
GList * g_list_append_printf(GList * list, char * format, ...);

#endif
