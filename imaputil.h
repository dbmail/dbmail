/* $Id$
 * (c) 2000-2001 IC&S, The Netherlands
 * 
 * imaputil.h
 *
 * utility functions for IMAP server
 */

#ifndef _IMAPUTIL_H
#define _IMAPUTIL_H

#include "serverservice.h"
#include "imap4.h"
#include "dbmysql.h"
#include "memblock.h"
#include <stdio.h>

int retrieve_structure(FILE *outstream, mime_message_t *msg, int show_extension_data);
int retrieve_envelope(FILE *outstream, struct list *rfcheader);
int show_address_list(FILE *outstream, struct mime_record *mr);
int show_mime_parameter_list(FILE *outstream, struct mime_record *mr, 
			     int force_subtype,
			     int only_extension);

mime_message_t* get_part_by_num(mime_message_t *msg, const char *part);

long rfcheader_dump(MEM *outmem, struct list *rfcheader, char **fieldnames, int nfields,
		    int equal_type);
long mimeheader_dump(MEM *outmem, struct list *mimeheader);

int haystack_find(int haystacklen, char **haystack, const char *needle);

int check_state_and_args(const char *command, const char *tag, char **args, 
			 int nargs, int state, ClientInfo *ci);
int next_fetch_item(char **args, int idx, fetch_items_t *fi);
int is_textplain(struct list *hdr);

char *date_sql2imap(const char *sqldate);
char *date_imap2sql(const char *imapdate);

int stridx(const char *s, char ch);
int checkchars(const char *s);
int checktag(const char *s);
int checkmailboxname(const char *s);
int check_msg_set(const char *s);
int check_date(const char *date);
void clarify_data(char *str);
char **build_args_array(const char *s);

void base64encode(char *in,char *out);
void base64decode(char *in,char *out);
int binary_search(const unsigned long *array, int arraysize, unsigned long key);

char **give_chunks(const char *str, char delimiter);
void free_chunks(char **chunks);
int quoted_string_out(FILE *outstream, const char *s);

int build_imap_search(char **search_keys, struct list *sl, int *idx);
int perform_imap_search(int *rset, int setlen, search_key_t *sk, mailbox_t *mb);
void free_searchlist(struct list *sl);

void invert_set(int *set, int setlen);
void combine_sets(int *dest, int *sec, int setlen, int type);

void build_set(int *set, int setlen, char *cset);
void build_uid_set(int *set, int setlen, char *cset, mailbox_t *mb);
void dumpsearch(search_key_t *sk, int level);

int init_cache();
void close_cache();

#endif
